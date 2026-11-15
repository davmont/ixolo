/* Test 175 - pselect(2), pollts(2) and paccept(2) are atomic.
 *
 * Each installs a signal mask for the duration of the call.  A signal that
 * the mask unblocks, and that is caught before the call blocks, must make
 * the call fail with EINTR: the classic race is that the handler runs and
 * the call then blocks anyway.  The test makes the race certain: the signal
 * is already pending (blocked) when the call starts, so it is caught as soon
 * as the mask is installed, before the call reaches VFS.  The handler also
 * makes a VFS call of its own, which must not disturb this.  An alarm stops
 * a call that blocks anyway.
 */
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static volatile sig_atomic_t caught, alarmed;
static int devnull;

static void
on_usr1(int sig)
{
	caught++;
	/* A VFS call from the handler, between the arming and the call. */
	(void)write(devnull, "x", 1);
}

static void
on_alrm(int sig)
{
	alarmed++;
}

/* Block SIGUSR1 and make it pending; 'unblocked' gets the mask without it. */
static void
arm_signal(sigset_t *unblocked)
{
	sigset_t set;

	caught = alarmed = 0;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &set, NULL) != 0) e(80);
	if (kill(getpid(), SIGUSR1) != 0) e(81);
	if (caught != 0) e(82);
	if (sigprocmask(SIG_BLOCK, NULL, unblocked) != 0) e(83);
	sigdelset(unblocked, SIGUSR1);
	alarm(3);
}

static void
check_result(int r, int err)
{
	alarm(0);
	if (alarmed) e(84);		/* it blocked: the race */
	if (r != -1 || err != EINTR) e(85);
	if (caught != 1) e(86);
}

static void
test175a(void)
{
	sigset_t m;
	int r;

	subtest = 1;

	arm_signal(&m);
	r = pselect(0, NULL, NULL, NULL, NULL, &m);
	check_result(r, errno);
}

static void
test175b(void)
{
	sigset_t m;
	int r, p[2];
	fd_set rd;

	subtest = 2;

	/* On a pipe that never becomes readable. */
	if (pipe(p) != 0) e(1);
	FD_ZERO(&rd);
	FD_SET(p[0], &rd);
	arm_signal(&m);
	r = pselect(p[0] + 1, &rd, NULL, NULL, NULL, &m);
	check_result(r, errno);

	/* And when a descriptor is ready, no signal: the result. */
	if (write(p[1], "y", 1) != 1) e(2);
	FD_ZERO(&rd);
	FD_SET(p[0], &rd);
	sigemptyset(&m);
	if (pselect(p[0] + 1, &rd, NULL, NULL, NULL, &m) != 1) e(3);
	if (!FD_ISSET(p[0], &rd)) e(4);
	(void)close(p[0]);
	(void)close(p[1]);
}

static void
test175c(void)
{
	sigset_t m;
	struct pollfd pfd;
	int r, p[2];

	subtest = 3;

	if (pipe(p) != 0) e(1);
	pfd.fd = p[0];
	pfd.events = POLLIN;
	arm_signal(&m);
	r = pollts(&pfd, 1, NULL, &m);
	check_result(r, errno);
	(void)close(p[0]);
	(void)close(p[1]);
}

static void
test175d(void)
{
	sigset_t m;
	struct sockaddr_un sun;
	int r, s;

	subtest = 4;

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		/* No uds service: nothing to test. */
		return;
	}
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, "t175.sock", sizeof(sun.sun_path));
	(void)unlink(sun.sun_path);
	if (bind(s, (struct sockaddr *)&sun, sizeof(sun)) != 0) e(1);
	if (listen(s, 1) != 0) e(2);
	arm_signal(&m);
	r = paccept(s, NULL, NULL, &m, 0);
	check_result(r, errno);
	(void)close(s);
	(void)unlink(sun.sun_path);
}

int
main(int argc, char **argv)
{
	struct sigaction sa;

	start(175);

	if ((devnull = open("/dev/null", O_WRONLY)) == -1) e(90);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_usr1;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) e(91);
	sa.sa_handler = on_alrm;
	if (sigaction(SIGALRM, &sa, NULL) != 0) e(92);

	test175a();
	test175b();
	test175c();
	test175d();

	quit();
	return 0;
}
