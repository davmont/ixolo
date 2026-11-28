# Test of the NPF packet filter in the LWIP service, through /dev/npf;
# run by testnpf.sh.

.include <bsd.own.mk>

NV=	${NETBSDSRCDIR}/sys/external/bsd/libnv/dist
NPF=	${NETBSDSRCDIR}/external/bsd/npf/dist/kern

PROG=		npfdev_test
# As NetBSD's libnv (external/bsd/libnv/lib/srcs.mk).
SRCS=		npfdev_test.c dnvlist.c msgio.c nvlist.c nvpair.c \
		nv_kern_netbsd.c

.PATH:		${NV}

CPPFLAGS+=	-D_DEFAULT_SOURCE
CPPFLAGS+=	-D_VA_LIST_DEFINED -D__FBSDID\(x\)=
CPPFLAGS+=	-I${NV} -I${NPF}
CSTD=		gnu11

MAN=
WARNS?=		3
BINDIR?=	/usr/tests/minix-posix/npf

.include <bsd.prog.mk>
