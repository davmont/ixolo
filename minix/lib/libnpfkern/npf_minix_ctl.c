/*
 * MINIX: NPF control operations for the LWIP service's /dev/npf device.
 *
 * On NetBSD, npfctl(8) passes configurations to the kernel as packed
 * nvlists, through ioctls whose argument (nvlist_ref_t) points to the
 * buffers.  MINIX ioctls cannot carry pointers to further data, so /dev/npf
 * takes the request as a write and returns the response through reads
 * instead (see minix/net/lwip/npfdev.c).  This file does the NPF side of
 * it: it unpacks the request, runs the operation as npf_os.c's ioctl
 * handler does, and packs the response.
 */

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "npf_impl.h"
#include "npfkern_minix.h"

/* As NetBSD's npf_os.c: the largest request accepted. */
#define	NPF_MINIX_DATA_LIMIT	(4 * 1024 * 1024)

__CTASSERT(NPF_MINIX_VERSION == NPF_VERSION);
__CTASSERT(NPF_MINIX_IOC_VERSION == IOC_NPF_VERSION);
__CTASSERT(NPF_MINIX_IOC_SWITCH == IOC_NPF_SWITCH);
__CTASSERT(NPF_MINIX_IOC_LOAD == IOC_NPF_LOAD);
__CTASSERT(NPF_MINIX_IOC_STATS == IOC_NPF_STATS);
__CTASSERT(NPF_MINIX_IOC_SAVE == IOC_NPF_SAVE);
__CTASSERT(NPF_MINIX_IOC_RULE == IOC_NPF_RULE);
__CTASSERT(NPF_MINIX_IOC_CONN_LOOKUP == IOC_NPF_CONN_LOOKUP);
__CTASSERT(NPF_MINIX_IOC_TABLE_REPLACE == IOC_NPF_TABLE_REPLACE);

int
npf_minix_ctl(npf_t *npf, unsigned long op, const void *req, size_t reqlen,
    void **respp, size_t *resplenp)
{
	nvlist_t *reqnv, *respnv;
	void *resp;
	size_t resplen;
	int error;

	*respp = NULL;
	*resplenp = 0;

	switch (op) {
	case IOC_NPF_STATS:
		/* No request; the response is the raw statistics array. */
		if ((resp = malloc(NPF_STATS_SIZE)) == NULL)
			return ENOMEM;
		npfk_stats(npf, resp);
		*respp = resp;
		*resplenp = NPF_STATS_SIZE;
		return 0;

	case IOC_NPF_LOAD:
	case IOC_NPF_SAVE:
	case IOC_NPF_RULE:
	case IOC_NPF_CONN_LOOKUP:
	case IOC_NPF_TABLE_REPLACE:
		break;

	default:
		return ENOTTY;
	}

	if (reqlen > NPF_MINIX_DATA_LIMIT)
		return E2BIG;

	/* Every request is an nvlist, with at least the NPF version. */
	if ((reqnv = nvlist_unpack(req, reqlen, 0)) == NULL)
		return EINVAL;

	if ((respnv = nvlist_create(0)) == NULL) {
		nvlist_destroy(reqnv);
		return ENOMEM;
	}

	error = npfctl_run_op(npf, op, reqnv, respnv);

	resp = nvlist_pack(respnv, &resplen);
	nvlist_destroy(respnv);
	nvlist_destroy(reqnv);

	if (resp == NULL)
		return error ? error : ENOMEM;

	*respp = resp;
	*resplenp = resplen;
	return error;
}
