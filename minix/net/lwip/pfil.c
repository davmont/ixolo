/* LWIP service - pfil.c - packet filter (NPF) hooks */
/*
 * This module connects the NPF packet filter (libnpfkern, upstream rmind/npf
 * built in its standalone form) to lwIP.  NPF sees IPv4 and IPv6 packets
 * through four lwIP hooks:
 *
 * - incoming packets: LWIP_HOOK_IP4_INPUT and LWIP_HOOK_IP6_INPUT, which lwIP
 *   calls at the start of ip4_input() and ip6_input(), before it processes
 *   the packet in any other way (including reassembly);
 * - outgoing packets: LWIP_HOOK_IP4_OUTPUT and LWIP_HOOK_IP6_OUTPUT, a
 *   MINIX-local addition to lwIP (liblwip patch 0005), called for locally
 *   generated and forwarded packets once the outgoing interface is known and
 *   the IP header is complete, but before fragmentation.
 *
 * NPF works on "mbufs" through a table of operations; here an mbuf is an lwIP
 * pbuf chain, and an NPF "ifnet" is an ifdev.  Packets that are looped back
 * to a local address of a non-loopback interface are filtered on that
 * interface, not on lo0, in both directions.  Packets that BPF devices write
 * are not filtered, as on NetBSD.
 *
 * Like NPF on NetBSD, filtering is off until it is switched on ("npfctl
 * start"); when on and no ruleset has been loaded, NPF passes all packets.
 * A loaded ruleset blocks whatever it does not pass.
 *
 * The LWIP service is single-threaded: NPF runs on the service's only thread,
 * which is registered for NPF's epoch-based reclamation once.  NPF's garbage
 * collection of expired connections runs from a timer here, as NPF has no
 * worker threads in this setup.
 */

#include "lwip.h"

#include "lwip/inet_chksum.h"
#include "lwip/ip4_frag.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/ip6.h"

#include <npfkern_minix.h>

#define PFIL_GC_INTERVAL	1	/* seconds between NPF G/C runs */

static npf_t *pfil_npf;		/* the NPF instance */
static int pfil_active;		/* is packet filtering on? */
static int pfil_dir;		/* direction of the packet in NPF */
static int pfil_reassembled;	/* did NPF reassemble the packet? */
static int pfil_reinject;	/* is lwIP processing a reassembled packet? */
static minix_timer_t pfil_timer;	/* NPF garbage collection timer */

#define PFIL_PBUF(m)	((struct pbuf *)(void *)(m))
#define PFIL_CPBUF(m)	((const struct pbuf *)(const void *)(m))
#define PFIL_MBUF(p)	((struct mbuf *)(void *)(p))

/*
 * NPF mbuf operations on lwIP pbuf chains.
 */

/*
 * Allocate a packet.  NPF uses this only to build TCP RST and ICMP replies
 * for "return" rules, which its standalone form cannot send anyway (it has no
 * ip_output()).  Fail the allocation, which makes NPF skip the reply.
 */
static struct mbuf *
pfil_m_alloc(npf_t * npf __unused, unsigned int type __unused,
	size_t len __unused)
{

	return NULL;
}

static void
pfil_m_free(struct mbuf * m)
{

	pbuf_free(PFIL_PBUF(m));
}

static void *
pfil_m_getdata(const struct mbuf * m)
{

	return PFIL_CPBUF(m)->payload;
}

static struct mbuf *
pfil_m_getnext(struct mbuf * m)
{

	return PFIL_MBUF(PFIL_PBUF(m)->next);
}

static size_t
pfil_m_getlen(const struct mbuf * m)
{

	return PFIL_CPBUF(m)->len;
}

static size_t
pfil_m_getchainlen(const struct mbuf * m)
{

	return PFIL_CPBUF(m)->tot_len;
}

/*
 * Make sure that the first 'len' bytes of the packet are contiguous in memory.
 * lwIP pbufs cannot be rearranged in place, so if the first pbuf is too short,
 * replace the whole chain with a contiguous copy.  The headers that NPF looks
 * at practically always fit in the first pbuf, so this should be rare.  NPF
 * requires that *mp remain valid on failure.
 */
static bool
pfil_m_ensure_contig(struct mbuf ** mp, size_t len)
{
	struct pbuf *p, *q;

	p = PFIL_PBUF(*mp);

	if (len <= p->len)
		return true;
	if (len > p->tot_len)
		return false;

	if ((q = pbuf_clone(PBUF_RAW, PBUF_RAM, p)) == NULL)
		return false;

	pbuf_free(p);
	*mp = PFIL_MBUF(q);
	return true;
}

/*
 * Make sure that the first 'len' bytes of the packet can be written to.  The
 * standalone NPF never calls this, because it considers all mbufs writable.
 * All pbuf data that NPF gets to see is indeed writable.
 */
static bool
pfil_m_ensure_writable(struct mbuf ** mp __unused, size_t len __unused)
{

	return true;
}

/*
 * Packet tags are not supported.  Note that error numbers returned to NPF must
 * be positive: libnpfkern is not built with _SYSTEM, unlike this service.
 */
static int
pfil_m_get_tag(const struct mbuf * m __unused, uint32_t * tag __unused)
{

	return -ENOTSUP;
}

static int
pfil_m_set_tag(struct mbuf * m __unused, uint32_t tag __unused)
{

	return -ENOTSUP;
}

static const npf_mbufops_t pfil_mbufops = {
	.alloc			= pfil_m_alloc,
	.free			= pfil_m_free,
	.getdata		= pfil_m_getdata,
	.getnext		= pfil_m_getnext,
	.getlen			= pfil_m_getlen,
	.getchainlen		= pfil_m_getchainlen,
	.ensure_contig		= pfil_m_ensure_contig,
	.ensure_writable	= pfil_m_ensure_writable,
	.get_tag		= pfil_m_get_tag,
	.set_tag		= pfil_m_set_tag,
};

/*
 * NPF interface operations on ifdevs.  NPF keeps a small per-interface value
 * (an interface ID of its own) in the "meta" field.
 */

static const char *
pfil_if_getname(npf_t * npf __unused, struct ifnet * ifp)
{

	return ifdev_get_name((struct ifdev *)ifp);
}

static struct ifnet *
pfil_if_lookup(npf_t * npf __unused, const char * name)
{

	return (struct ifnet *)ifdev_find_by_name(name);
}

static void
pfil_if_flush(npf_t * npf __unused, void * arg)
{
	struct ifdev *ifdev;

	for (ifdev = NULL; (ifdev = ifdev_enum(ifdev)) != NULL; )
		ifdev->ifdev_npf = arg;
}

static void *
pfil_if_getmeta(npf_t * npf __unused, const struct ifnet * ifp)
{

	return ((const struct ifdev *)ifp)->ifdev_npf;
}

static void
pfil_if_setmeta(npf_t * npf __unused, struct ifnet * ifp, void * arg)
{

	((struct ifdev *)ifp)->ifdev_npf = arg;
}

static const npf_ifops_t pfil_ifops = {
	.getname	= pfil_if_getname,
	.lookup		= pfil_if_lookup,
	.flush		= pfil_if_flush,
	.getmeta	= pfil_if_getmeta,
	.setmeta	= pfil_if_setmeta,
};

/*
 * IPv4 reassembly for NPF, with lwIP's reassembly code.  NPF calls this for an
 * incoming fragment when its "ip4.reassembly" parameter is set (the default).
 * On return, *mp is the full datagram, or NULL if the fragment was queued (or
 * dropped) by lwIP.  lwIP then never sees the fragments, only the result.
 *
 * NPF also reassembles outgoing fragments, which would turn them back into a
 * packet too large to send.  The output hook is called before fragmentation,
 * so these are packets that are fragments already when handed to lwIP, from
 * raw sockets with IP_HDRINCL.  Fail those: NPF drops them.
 */
int
npf_minix_ip_reass_packet(struct mbuf ** mp)
{
	struct pbuf *p;

	if (pfil_dir != PFIL_IN)
		return -EINVAL;		/* positive for NPF; see above */

	p = ip4_reass(PFIL_PBUF(*mp));

	*mp = PFIL_MBUF(p);

	if (p != NULL)
		pfil_reassembled = TRUE;

	return 0;
}

/*
 * NPF asks this to report whether packet filtering is on.
 */
bool
npf_active_p(void)
{

	return pfil_active;
}

/*
 * Run a packet through NPF.  The caller keeps its own reference to 'p'.  NPF
 * gets a reference of its own, which it frees if it drops the packet.  Return
 * NULL if NPF dropped (or kept) the packet, or the packet to continue with,
 * holding NPF's reference: this is 'p' itself, or a replacement for it if NPF
 * made a contiguous copy or reassembled the packet.
 */
static struct pbuf *
pfil_run(struct pbuf * p, struct netif * netif, int dir)
{
	struct mbuf *m;

	pbuf_ref(p);

	m = PFIL_MBUF(p);
	pfil_dir = dir;
	pfil_reassembled = FALSE;

	(void)npfk_packet_handler(pfil_npf, &m,
	    (struct ifnet *)netif_get_ifdev(netif), dir);

	return PFIL_PBUF(m);
}

/*
 * Copy back the contents of 'q', a contiguous copy of 'p' that NPF may have
 * changed, into 'p', and free 'q'.  Return TRUE on success, or FALSE if 'q'
 * is not the same size as 'p' anymore.
 */
static int
pfil_copyback(struct pbuf * p, struct pbuf * q)
{
	int r;

	r = (q->tot_len == p->tot_len && pbuf_copy(p, q) == ERR_OK);

	pbuf_free(q);

	return r;
}

/*
 * Filter an incoming packet.  Return 1 if the packet has been dealt with
 * (dropped, or handed to lwIP in reassembled form), or 0 if lwIP should
 * continue processing it.
 */
static int
pfil_input(struct pbuf * p, struct netif * inp)
{
	struct pbuf *q;

	if ((q = pfil_run(p, inp, PFIL_IN)) == p) {
		/* Pass, as is.  Drop NPF's reference. */
		pbuf_free(p);

		return 0;
	}

	if (q == NULL) {
		/* Blocked, or a fragment that lwIP queued for reassembly. */
		pbuf_free(p);

		return 1;
	}

	/* Pass, but NPF made a contiguous copy of the packet. */
	if (!pfil_reassembled) {
		if (pfil_copyback(p, q))
			return 0;

		pbuf_free(p);
		return 1;
	}

	/*
	 * Pass, but this is a packet that NPF has reassembled from fragments,
	 * the last of which was 'p'.  Hand the full packet to lwIP instead,
	 * without filtering it again.
	 */
	pbuf_free(p);

	pfil_reinject = TRUE;

	if (ip_input(q, inp) != ERR_OK)
		pbuf_free(q);

	pfil_reinject = FALSE;

	return 1;
}

/*
 * Filter an outgoing packet.  Return ERR_OK if the packet should be sent, or
 * an error if it is to be dropped.  The caller remains responsible for the
 * packet either way.
 */
static err_t
pfil_output(struct pbuf * p, struct netif * netif)
{
	struct pbuf *q;

	if ((q = pfil_run(p, netif, PFIL_OUT)) == p) {
		pbuf_free(p);

		return ERR_OK;
	}

	if (q == NULL)
		return ERR_RTE;

	return pfil_copyback(p, q) ? ERR_OK : ERR_BUF;
}

/*
 * lwIP hook for every incoming IPv4 packet, before lwIP processes it.  Return
 * 1 if the packet has been consumed, or 0 if lwIP should process it.
 */
int
lwip_hook_ip4_input(struct pbuf * p, struct netif * inp)
{
	struct ip_hdr *iphdr;
	unsigned int hlen, len;

	if (pfil_active && !pfil_reinject) {
		/*
		 * lwIP validates the IP header only after calling this hook.
		 * Leave packets with a bad header to lwIP, which drops them,
		 * so that NPF and lwIP's reassembly code get only valid ones.
		 * Remove link-layer padding just like lwIP does next.
		 */
		if (p->len < IP_HLEN)
			return 0;
		iphdr = (struct ip_hdr *)p->payload;
		hlen = IPH_HL_BYTES(iphdr);
		len = lwip_ntohs(IPH_LEN(iphdr));
		if (IPH_V(iphdr) != 4 || hlen < IP_HLEN || hlen > p->len ||
		    len < hlen || len > p->tot_len)
			return 0;
		/*
		 * Check the header checksum only where lwIP does: loopback
		 * interfaces may skip checksums altogether (see loopif.c).
		 */
		IF__NETIF_CHECKSUM_ENABLED(inp, NETIF_CHECKSUM_CHECK_IP) {
			if (inet_chksum(iphdr, hlen) != 0)
				return 0;
		}
		if (len < p->tot_len)
			pbuf_realloc(p, len);

		if (pfil_input(p, inp))
			return 1;
	}

	udpsock_ip4_input(p);

	return 0;
}

/*
 * lwIP hook for every incoming IPv6 packet, before lwIP processes it.
 */
int
lwip_hook_ip6_input(struct pbuf * p, struct netif * inp)
{
	struct ip6_hdr *ip6hdr;
	unsigned int len;

	if (pfil_active && !pfil_reinject) {
		/* As for IPv4.  lwIP does not reassemble before this hook. */
		if (p->len < IP6_HLEN)
			return 0;
		ip6hdr = (struct ip6_hdr *)p->payload;
		len = IP6_HLEN + IP6H_PLEN(ip6hdr);
		if (IP6H_V(ip6hdr) != 6 || len > p->tot_len)
			return 0;
		if (len < p->tot_len)
			pbuf_realloc(p, len);

		if (pfil_input(p, inp))
			return 1;
	}

	udpsock_ip6_input(p);

	return 0;
}

/*
 * lwIP hook for every outgoing IPv4 packet: see the top of this file.
 */
err_t
lwip_hook_ip4_output(struct pbuf * p, struct netif * netif)
{

	if (!pfil_active)
		return ERR_OK;

	return pfil_output(p, netif);
}

/*
 * lwIP hook for every outgoing IPv6 packet: see the top of this file.
 */
err_t
lwip_hook_ip6_output(struct pbuf * p, struct netif * netif)
{

	if (!pfil_active)
		return ERR_OK;

	return pfil_output(p, netif);
}

/*
 * Garbage-collect expired NPF connections, periodically.
 */
static void
pfil_gc(int arg __unused)
{

	npfk_gc(pfil_npf);

	set_timer(&pfil_timer, PFIL_GC_INTERVAL * sys_hz(), pfil_gc, 0);
}

/*
 * A new interface has been added.
 */
void
pfil_ifattach(struct ifdev * ifdev)
{

	npfk_ifmap_attach(pfil_npf, (struct ifnet *)ifdev);
}

/*
 * An interface is about to be removed.
 */
void
pfil_ifdetach(struct ifdev * ifdev)
{

	npfk_ifmap_detach(pfil_npf, (struct ifnet *)ifdev);
}

/*
 * Switch packet filtering on or off.
 */
void
pfil_set_active(int active)
{

	pfil_active = active;
}

/*
 * Return the NPF instance, for the /dev/npf device.
 */
npf_t *
pfil_get_npf(void)
{

	return pfil_npf;
}

/*
 * Initialize the packet filter module.  This must be done before any
 * interfaces are added.
 */
void
pfil_init(void)
{

	/*
	 * NPF uses random() to randomize NAT ports and IP IDs.  Seed it from
	 * lwIP's random source; see the lwip_hook_rand() comment on why that
	 * one is good enough for now.
	 */
	srandom((unsigned int)lrand48());

	if (npfk_sysinit(0 /*nworkers*/) != 0)
		panic("unable to initialize NPF");

	if ((pfil_npf = npfk_create(NPF_NO_GC, &pfil_mbufops, &pfil_ifops,
	    NULL)) == NULL)
		panic("unable to create NPF instance");

	/* This is the only thread that ever enters NPF. */
	npfk_thread_register(pfil_npf);

	/* The extensions and ALGs, as NetBSD's npf_init() sets them up. */
	(void)npf_ext_normalize_init(pfil_npf);
	(void)npf_ext_rndblock_init(pfil_npf);
	(void)npf_ext_ratelimit_init(pfil_npf);
	(void)npf_alg_icmp_init(pfil_npf);

	pfil_active = FALSE;
	pfil_reinject = FALSE;

	init_timer(&pfil_timer);
	set_timer(&pfil_timer, PFIL_GC_INTERVAL * sys_hz(), pfil_gc, 0);
}
