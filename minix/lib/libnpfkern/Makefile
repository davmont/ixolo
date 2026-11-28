# NPF packet filter core (upstream rmind/npf "npfkern") built for the lwip
# service, with the libraries it needs: libnv, thmap, libqsbr's EBR, liblpm
# and cdbr.  A private library: only the lwip service links it.

.include <bsd.own.mk>

LIBISPRIVATE=	yes
LIB=		npfkern

NPF=	${NETBSDSRCDIR}/external/bsd/npf/dist/kern
NV=	${NETBSDSRCDIR}/sys/external/bsd/libnv/dist
THMAP=	${NETBSDSRCDIR}/external/bsd/thmap/dist
QSBR=	${NETBSDSRCDIR}/external/bsd/libqsbr/dist
LPM=	${NETBSDSRCDIR}/external/bsd/liblpm/dist

.PATH:	${NPF} ${NPF}/stand ${NV} ${THMAP} ${QSBR} ${LPM} \
	${NETBSDSRCDIR}/common/lib/libc/cdb \
	${NETBSDSRCDIR}/common/lib/libc/stdlib \
	${NETBSDSRCDIR}/lib/libc/stdlib

# As upstream's src/kern/Makefile: files.npf minus npf_os, npf_ext_log,
# npf_ifaddr, if_npflog and lpm (liblpm is used instead), plus stand/.
NPFSRCS=	npf.c npf_conf.c npf_ctl.c npf_handler.c npf_mbuf.c npf_bpf.c \
	npf_params.c npf_ruleset.c npf_rproc.c npf_tableset.c npf_if.c \
	npf_inet.c npf_conn.c npf_connkey.c npf_conndb.c npf_state.c \
	npf_state_tcp.c npf_nat.c npf_portmap.c npf_alg.c npf_sendpkt.c \
	npf_worker.c npf_ext_normalize.c npf_ext_ratelimit.c \
	npf_ext_rndblock.c npf_alg_icmp.c npf_alg_pptp.c
NPFSRCS+=	npfkern.c bpf_filter.c murmurhash.c tls_pth.c ebr_wrappers.c
SRCS=	${NPFSRCS}
SRCS+=	nvlist.c nvpair.c dnvlist.c nv_kern_netbsd.c
SRCS+=	thmap.c thmap_murmurhash.c ebr.c lpm.c cdbr.c mi_vector_hash.c
SRCS+=	qsort.c bsearch.c
SRCS+=	npf_minix_libc.c npf_minix_ctl.c

CPPFLAGS+=	-D_NPF_STANDALONE -D__RCSID\(x\)= -D__KERNEL_RCSID\(x,y\)=
CPPFLAGS+=	-I${NPF}/stand -I${NPF} -I${NV} -I${THMAP} -I${QSBR} -I${LPM}
CPPFLAGS+=	-I${.CURDIR}
CPPFLAGS+=	-include ${.CURDIR}/npf_minix_pre.h
# stand/cext.h maps C11 atomics onto __atomic builtins (plain, not _Atomic,
# objects) but leaves out memory_order_consume.
.for f in ${NPFSRCS}
CPPFLAGS.${f}+=	-Dmemory_order_consume=__ATOMIC_CONSUME
.endfor
# The host (the LWIP service) says whether filtering is on: see npfkern_minix.h.
CPPFLAGS.npfkern.c+=	-Dnpf_active_p=npf_stand_active_p
# cdbr.c is libc's: it wants libc's private headers.
CPPFLAGS.cdbr.c+=	-I${NETBSDSRCDIR}/lib/libc/include
CPPFLAGS.mi_vector_hash.c+=	-I${NETBSDSRCDIR}/lib/libc/include
# npf_stand.h redefines __diagused, which MINIX's <sys/cdefs.h> has.
CWARNFLAGS+=	-Wno-macro-redefined
# lwip has its own bpf_filter() and friends, with other signatures.
CPPFLAGS+=	-Dbpf_filter=npf_bpf_filter_impl \
		-Dbpf_filter_ext=npf_bpf_filter_ext \
		-Dbpf_validate=npf_bpf_validate_impl \
		-Dbpf_validate_ext=npf_bpf_validate_ext
CSTD=		gnu11
WARNS?=		0
NOMAN=		yes

.include <bsd.lib.mk>
