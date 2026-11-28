/*
 * MINIX: the NPF kernel interface (npfkern), for the LWIP service.
 *
 * NPF's own <npf.h> is meant for code built as part of NPF and pulls in the
 * standalone kernel compatibility layer, which does not mix with lwIP's
 * headers.  The LWIP service includes this header instead: it provides the
 * types that <npfkern.h> needs, as opaque types, and the MINIX-local glue.
 */
#ifndef _NPFKERN_MINIX_H_
#define _NPFKERN_MINIX_H_

#include <stddef.h>
#include <sys/ioccom.h>

#ifndef _NPF_STANDALONE		/* not when built as part of libnpfkern */
typedef struct npf npf_t;
typedef struct npf_minix_error npf_error_t;	/* opaque here; unused */
#endif

#include "npfkern.h"

/*
 * The NPF version and ioctl numbers that /dev/npf deals in, for code that
 * cannot include <npf.h>.  npf_minix_ctl.c checks that they match.  The
 * nvlist ioctl numbers serve as operation codes of /dev/npf write requests.
 */
struct npf_minix_nvref {		/* same layout as nvlist_ref_t */
	void *		buf;
	size_t		len;
	int		flags;
};

#define	NPF_MINIX_VERSION	22
#define	NPF_MINIX_IOC_VERSION	_IOR('N', 100, int)
#define	NPF_MINIX_IOC_SWITCH	_IOW('N', 101, int)
#define	NPF_MINIX_IOC_LOAD	_IOWR('N', 102, struct npf_minix_nvref)
#define	NPF_MINIX_IOC_STATS	_IOW('N', 104, void *)
#define	NPF_MINIX_IOC_SAVE	_IOR('N', 105, struct npf_minix_nvref)
#define	NPF_MINIX_IOC_RULE	_IOWR('N', 107, struct npf_minix_nvref)
#define	NPF_MINIX_IOC_CONN_LOOKUP _IOWR('N', 108, struct npf_minix_nvref)
#define	NPF_MINIX_IOC_TABLE_REPLACE _IOWR('N', 109, struct npf_minix_nvref)

/*
 * Provided by the host: IPv4 reassembly for NPF, see the MINIX-local part
 * of stand/npf_stand.h.
 */
int	npf_minix_ip_reass_packet(struct mbuf **);

/*
 * Provided by libnpfkern (npf_minix_ctl.c): run an NPF control operation
 * (one of the IOC_NPF_* requests that take an nvlist) on a packed nvlist
 * request.  On success, *resp is a malloc'ed packed nvlist response of
 * *resplen bytes, which the caller frees.  Returns 0 or an error number.
 *
 * Error numbers in libnpfkern are positive, as in userland: unlike the rest
 * of the LWIP service, it is not built with _SYSTEM (<minix/drivers.h>),
 * which would make them negative.  The responses carry them to userland.
 */
int	npf_minix_ctl(npf_t *, unsigned long, const void *, size_t, void **,
	    size_t *);

/*
 * Provided by the host: whether packet filtering is enabled ("npfctl start").
 * The standalone stub, which always returns true, is renamed away by the
 * libnpfkern Makefile.
 */
#include <stdbool.h>
bool	npf_active_p(void);

#endif /* !_NPFKERN_MINIX_H_ */
