/*
 * Test the NPF packet filter in the LWIP service through /dev/npf, without
 * npfctl: version and switch ioctls, configuration loads (an empty ruleset
 * blocks everything; a flush passes everything again), the configuration
 * save operation, statistics, errors, and that packets keep flowing through
 * the filter, including IPv4 and IPv6 datagrams fragmented on lo0, which NPF
 * reassembles (IPv4) or passes fragment by fragment (IPv6).
 *
 * Whatever happens, the filter is flushed and switched off on exit, and the
 * MTU of lo0 is restored, so as not to break the tests that come after.
 *
 * Prints "ok" at the end; exits nonzero on failure.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nv.h"
#include "dnv.h"
#include "npf.h"

static int npf_fd = -1;
static int saved_mtu = -1;

/*
 * Perform a /dev/npf operation.  Return 0 or the error of the operation.
 * The response is stored in *resp (NULL if none), or, for IOC_NPF_STATS, in
 * the buffer 'raw' of 'rawlen' bytes.
 */
static int
npf_op(unsigned long op, nvlist_t *req, nvlist_t **resp, void *raw,
    size_t rawlen)
{
	char *buf, rbuf[65536];
	void *packed = NULL;
	size_t len = 0, off;
	uint64_t op64 = op;
	ssize_t r;
	int error;

	if (req != NULL && (packed = nvlist_pack(req, &len)) == NULL)
		err(1, "nvlist_pack");
	if ((buf = malloc(sizeof(op64) + len)) == NULL)
		err(1, "malloc");
	memcpy(buf, &op64, sizeof(op64));
	if (len > 0)
		memcpy(buf + sizeof(op64), packed, len);
	free(packed);

	r = write(npf_fd, buf, sizeof(op64) + len);
	error = (r < 0) ? errno : 0;
	if (r >= 0 && (size_t)r != sizeof(op64) + len)
		errx(1, "short write: %zd", r);
	free(buf);

	for (off = 0; off < sizeof(rbuf); off += r) {
		if ((r = read(npf_fd, rbuf + off, sizeof(rbuf) - off)) < 0)
			err(1, "read");
		if (r == 0)
			break;
	}
	if (off == sizeof(rbuf))
		errx(1, "response too large");

	if (resp != NULL) {
		*resp = (off > 0) ? nvlist_unpack(rbuf, off, 0) : NULL;
		if (off > 0 && *resp == NULL)
			errx(1, "bad response nvlist");
	}
	if (raw != NULL) {
		if (off != rawlen)
			errx(1, "response is %zu bytes, not %zu", off, rawlen);
		memcpy(raw, rbuf, off);
	}
	return error;
}

/* Load a configuration with no rules: 'flush' passes all, else blocks all. */
static int
npf_load(bool flush, uint64_t version, nvlist_t **resp)
{
	nvlist_t *req;
	int error;

	req = nvlist_create(0);
	nvlist_add_number(req, "version", version);
	nvlist_add_bool(req, "flush", flush);
	error = npf_op(IOC_NPF_LOAD, req, resp, NULL, 0);
	nvlist_destroy(req);
	return error;
}

static void
npf_switch(int on)
{

	if (ioctl(npf_fd, IOC_NPF_SWITCH, &on) != 0)
		err(1, "IOC_NPF_SWITCH %d", on);
}

static bool
npf_saved_active(void)
{
	nvlist_t *req, *resp;
	bool active;
	int error;

	/* Every nvlist request carries the version, even an otherwise empty one. */
	req = nvlist_create(0);
	nvlist_add_number(req, "version", NPF_VERSION);
	error = npf_op(IOC_NPF_SAVE, req, &resp, NULL, 0);
	nvlist_destroy(req);
	if (error != 0)
		errc(1, error, "IOC_NPF_SAVE");
	if (resp == NULL || !nvlist_exists_bool(resp, "active"))
		errx(1, "IOC_NPF_SAVE: no \"active\" in response");
	active = nvlist_get_bool(resp, "active");
	nvlist_destroy(resp);
	return active;
}

static void
npf_stats(uint64_t st[NPF_STATS_COUNT])
{
	int error;

	if ((error = npf_op(IOC_NPF_STATS, NULL, NULL, st,
	    NPF_STATS_SIZE)) != 0)
		errc(1, error, "IOC_NPF_STATS");
}

/* Get or set the MTU of lo0. */
static int
lo0_mtu(int mtu)
{
	struct ifreq ifr;
	int s;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err(1, "socket");
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, "lo0", sizeof(ifr.ifr_name));
	if (mtu < 0) {
		if (ioctl(s, SIOCGIFMTU, &ifr) != 0)
			err(1, "SIOCGIFMTU");
	} else {
		ifr.ifr_mtu = mtu;
		if (ioctl(s, SIOCSIFMTU, &ifr) != 0)
			err(1, "SIOCSIFMTU");
	}
	close(s);
	return ifr.ifr_mtu;
}

static void
cleanup(void)
{

	if (npf_fd >= 0) {
		(void)npf_load(true, NPF_VERSION, NULL);
		npf_switch(0);
	}
	if (saved_mtu > 0)
		(void)lo0_mtu(saved_mtu);
}

/*
 * Send a UDP datagram of 'len' bytes to ourselves over the loopback address
 * of the given family, and receive it.  Return 0 if it arrived intact, the
 * error of sendto(2) if that failed, or ETIMEDOUT if nothing arrived.
 */
static int
udp_loop(int family, size_t len)
{
	struct sockaddr_storage ss;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
	struct timeval tv = { 3, 0 };
	socklen_t sslen;
	static char out[16384], in[16384];
	int rs, ss_fd, error;
	ssize_t r;
	size_t i;

	if (len > sizeof(out))
		errx(1, "datagram too large");
	for (i = 0; i < len; i++)
		out[i] = (char)(i * 7 + len);

	memset(&ss, 0, sizeof(ss));
	if (family == AF_INET) {
		sin->sin_len = sizeof(*sin);
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		sslen = sizeof(*sin);
	} else {
		sin6->sin6_len = sizeof(*sin6);
		sin6->sin6_family = AF_INET6;
		sin6->sin6_addr = in6addr_loopback;
		sslen = sizeof(*sin6);
	}

	if ((rs = socket(family, SOCK_DGRAM, 0)) < 0 ||
	    (ss_fd = socket(family, SOCK_DGRAM, 0)) < 0)
		err(1, "socket");
	if (bind(rs, (struct sockaddr *)&ss, sslen) != 0)
		err(1, "bind");
	if (getsockname(rs, (struct sockaddr *)&ss, &sslen) != 0)
		err(1, "getsockname");
	if (setsockopt(rs, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
		err(1, "SO_RCVTIMEO");

	error = 0;
	if (sendto(ss_fd, out, len, 0, (struct sockaddr *)&ss, sslen) < 0)
		error = errno;
	else if ((r = recv(rs, in, sizeof(in), 0)) < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			err(1, "recv");
		error = ETIMEDOUT;
	} else if ((size_t)r != len || memcmp(in, out, len) != 0)
		errx(1, "datagram of %zu bytes came back as %zd bytes%s", len,
		    r, (size_t)r == len ? ", different" : "");

	close(rs);
	close(ss_fd);
	return error;
}

static void
expect_udp(int family, size_t len, int expected, const char *what)
{
	char got[64];
	int error;

	if ((error = udp_loop(family, len)) != expected) {
		/* strerror(3) may reuse one buffer: copy the first result. */
		strlcpy(got, error ? strerror(error) : "delivered",
		    sizeof(got));
		errx(1, "%s: IPv%d UDP of %zu bytes: got \"%s\", expected "
		    "\"%s\"", what, family == AF_INET ? 4 : 6, len, got,
		    expected ? strerror(expected) : "delivered");
	}
}

/* Check that a statistics counter went up. */
static void
expect_more(const uint64_t *before, const uint64_t *after, int stat,
    const char *name)
{

	if (after[stat] <= before[stat])
		errx(1, "statistic %s did not go up (%llu)", name,
		    (unsigned long long)after[stat]);
}

int
main(void)
{
	uint64_t s0[NPF_STATS_COUNT], s1[NPF_STATS_COUNT];
	nvlist_t *resp;
	uint64_t op;
	int version, error;

	if ((npf_fd = open("/dev/npf", O_RDWR)) < 0)
		err(1, "/dev/npf");
	atexit(cleanup);

	/* The version, and errors. */
	if (ioctl(npf_fd, IOC_NPF_VERSION, &version) != 0)
		err(1, "IOC_NPF_VERSION");
	if (version != NPF_VERSION)
		errx(1, "NPF version %d, expected %d", version, NPF_VERSION);
	/* NPF rejects a wrong version before it fills in the response. */
	if ((error = npf_load(true, 1, &resp)) != EPROGMISMATCH)
		errx(1, "load of version 1: error %d, expected EPROGMISMATCH",
		    error);
	if (resp != NULL)
		nvlist_destroy(resp);
	op = 0x12345678;
	if (write(npf_fd, &op, sizeof(op)) != -1 || errno != ENOTTY)
		errx(1, "unknown operation: expected ENOTTY");
	if (write(npf_fd, &op, 4) != -1 || errno != EINVAL)
		errx(1, "short request: expected EINVAL");

	/* Off: no filtering, no statistics. */
	if (npf_saved_active())
		errx(1, "NPF active at the start");
	npf_stats(s0);
	expect_udp(AF_INET, 100, 0, "off");
	expect_udp(AF_INET6, 100, 0, "off");
	npf_stats(s1);
	if (memcmp(s0, s1, sizeof(s0)) != 0)
		errx(1, "statistics changed while off");

	/* On, nothing loaded: all passes. */
	npf_switch(1);
	if (!npf_saved_active())
		errx(1, "NPF not active after switching on");
	expect_udp(AF_INET, 100, 0, "on, no ruleset");
	expect_udp(AF_INET6, 100, 0, "on, no ruleset");
	npf_stats(s0);
	expect_more(s1, s0, NPF_STAT_PASS_DEFAULT, "pass-default");

	/* An empty ruleset: all is blocked, on the way out already. */
	if ((error = npf_load(false, NPF_VERSION, &resp)) != 0)
		errc(1, error, "load");
	if (resp == NULL || dnvlist_get_number(resp, "errno", 1) != 0)
		errx(1, "load: bad response");
	nvlist_destroy(resp);
	expect_udp(AF_INET, 100, EHOSTUNREACH, "empty ruleset");
	expect_udp(AF_INET6, 100, EHOSTUNREACH, "empty ruleset");
	npf_stats(s1);
	expect_more(s0, s1, NPF_STAT_BLOCK_DEFAULT, "block-default");

	/* Flushed: all passes again. */
	if ((error = npf_load(true, NPF_VERSION, NULL)) != 0)
		errc(1, error, "flush");
	expect_udp(AF_INET, 100, 0, "flushed");
	expect_udp(AF_INET6, 100, 0, "flushed");

	/*
	 * Fragments: with a 1500-byte MTU on lo0, lwIP fragments these
	 * datagrams.  NPF reassembles the IPv4 ones itself, and passes the
	 * IPv6 fragments for lwIP to reassemble.
	 */
	saved_mtu = lo0_mtu(-1);
	(void)lo0_mtu(1500);
	npf_stats(s0);
	expect_udp(AF_INET, 6000, 0, "fragmented");
	expect_udp(AF_INET6, 6000, 0, "fragmented");
	npf_stats(s1);
	expect_more(s0, s1, NPF_STAT_FRAGMENTS, "fragments");
	expect_more(s0, s1, NPF_STAT_REASSEMBLY, "reassembly");
	if (s1[NPF_STAT_REASSFAIL] != s0[NPF_STAT_REASSFAIL])
		errx(1, "reassembly failures");
	(void)lo0_mtu(saved_mtu);

	/* Off again. */
	npf_switch(0);
	if (npf_saved_active())
		errx(1, "NPF active after switching off");
	expect_udp(AF_INET, 6000, 0, "off again");

	printf("ok\n");
	return 0;
}
