# Unit tests of the libraries NPF is built on (nvlist, thmap, libqsbr,
# liblpm), from their upstream sources; run by testnpfdeps.sh.

.include <bsd.own.mk>

NV=	${NETBSDSRCDIR}/sys/external/bsd/libnv/dist
THMAP=	${NETBSDSRCDIR}/external/bsd/thmap/dist
QSBR=	${NETBSDSRCDIR}/external/bsd/libqsbr/dist
LPM=	${NETBSDSRCDIR}/external/bsd/liblpm/dist

NVTESTS=	nvlist_add_test nvlist_append_test nvlist_exists_test nvlist_free_test \
		nvlist_get_test nvlist_move_test
PROGS=		${NVTESTS} t_thmap thmap_stress t_gc qsbr_stress t_lpm

# As NetBSD's libnv (external/bsd/libnv/lib/srcs.mk).
NVSRCS=		dnvlist.c msgio.c nvlist.c nvpair.c nv_kern_netbsd.c
.for t in ${NVTESTS}
SRCS.${t}=	${t}.c ${NVSRCS}
.endfor
SRCS.t_thmap=	t_thmap.c thmap.c murmurhash.c
SRCS.thmap_stress= thmap_stress.c thmap.c murmurhash.c
SRCS.t_gc=	t_gc.c gc.c ebr.c
SRCS.qsbr_stress= qsbr_stress.c ebr.c qsbr.c gc.c
SRCS.t_lpm=	t_lpm.c lpm.c

.PATH:		${NV} ${THMAP} ${QSBR} ${LPM}

# The tests check with assert().
CPPFLAGS+=	-UNDEBUG
CPPFLAGS+=	-D_DEFAULT_SOURCE
CPPFLAGS+=	-D_VA_LIST_DEFINED -D__FBSDID\(x\)=
CPPFLAGS+=	-I${NV}
CPPFLAGS.thmap_stress.c+=	-I${THMAP}
CPPFLAGS.qsbr_stress.c+=	-I${QSBR}
CSTD=		gnu11

LDADD.thmap_stress+=	-lpthread
LDADD.qsbr_stress+=	-lpthread
DPADD.thmap_stress+=	${LIBPTHREAD}
DPADD.qsbr_stress+=	${LIBPTHREAD}

MAN=
.for p in ${PROGS}
MAN.${p}=
.endfor
WARNS?=		3
BINDIR?=	/usr/tests/minix-posix/npfdeps

.include <bsd.prog.mk>
