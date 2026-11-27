/* LWIP service - npfdev.c - /dev/npf, the NPF packet filter control device */
/*
 * On NetBSD, npfctl(8) controls NPF through ioctls on /dev/npf.  Most of
 * them pass a packed nvlist through an nvlist_ref_t argument, which points
 * to the actual buffers.  MINIX ioctls cannot carry pointers to further
 * data, so this device offers the following interface instead:
 *
 * - ioctl IOC_NPF_VERSION (int out) and IOC_NPF_SWITCH (int in, nonzero to
 *   switch filtering on), as on NetBSD;
 * - for every other operation, a write of a request: a uint64_t holding the
 *   NetBSD ioctl number of the operation (IOC_NPF_LOAD, IOC_NPF_SAVE,
 *   IOC_NPF_RULE, IOC_NPF_CONN_LOOKUP, IOC_NPF_TABLE_REPLACE or
 *   IOC_NPF_STATS), followed by the packed nvlist request, if any.  The
 *   write performs the operation.  It fails with the operation's error, if
 *   any, and otherwise returns the number of bytes written;
 * - reads then return the response: a packed nvlist, or the raw statistics
 *   array for IOC_NPF_STATS.  It is read as a file, until end-of-file.  A
 *   response is kept even if the operation failed, because it may hold
 *   error details.  The next write replaces it.
 *
 * Opening /dev/npf yields a cloned device, so each open has its own response.
 * /dev/npf shares the major device number of /dev/bpf: see cdev.c.
 */

#include "lwip.h"
#include "npfdev.h"

#include <minix/chardriver.h>

#include <npfkern_minix.h>

#define NR_NPFDEV	8		/* number of concurrent opens */

/* The largest request accepted, including the operation code. */
#define NPFDEV_MAX_REQ	(sizeof(uint64_t) + 4 * 1024 * 1024)

static struct npfdev {
	int nd_inuse;			/* is this device open? */
	void *nd_resp;			/* response, or NULL */
	size_t nd_resplen;		/* size of the response */
	size_t nd_respoff;		/* how much of it has been read */
} npfdev_array[NR_NPFDEV];

/*
 * Given a minor number, return the corresponding cloned device, or NULL if
 * the minor number does not identify one.
 */
static struct npfdev *
npfdev_get_by_minor(devminor_t minor)
{
	struct npfdev *nd;

	if (minor < NPFDEV_BASE_MINOR ||
	    (unsigned int)minor >= NPFDEV_BASE_MINOR + NR_NPFDEV)
		return NULL;

	nd = &npfdev_array[minor - NPFDEV_BASE_MINOR];

	return nd->nd_inuse ? nd : NULL;
}

/*
 * Free the response of a device, if any.
 */
static void
npfdev_free_resp(struct npfdev * nd)
{

	free(nd->nd_resp);

	nd->nd_resp = NULL;
	nd->nd_resplen = 0;
	nd->nd_respoff = 0;
}

/*
 * Open /dev/npf, returning a cloned device.
 */
static int
npfdev_open(devminor_t minor, int access __unused,
	endpoint_t user_endpt __unused)
{
	unsigned int i;

	/* Disallow opening cloned devices through device nodes. */
	if (minor != NPFDEV_MINOR)
		return ENXIO;

	for (i = 0; i < NR_NPFDEV; i++) {
		if (!npfdev_array[i].nd_inuse)
			break;
	}

	if (i == NR_NPFDEV)
		return ENOBUFS;

	memset(&npfdev_array[i], 0, sizeof(npfdev_array[i]));
	npfdev_array[i].nd_inuse = TRUE;

	return CDEV_CLONED | (NPFDEV_BASE_MINOR + i);
}

/*
 * Close a cloned device.
 */
static int
npfdev_close(devminor_t minor)
{
	struct npfdev *nd;

	if ((nd = npfdev_get_by_minor(minor)) == NULL)
		return EINVAL;

	npfdev_free_resp(nd);

	nd->nd_inuse = FALSE;

	return OK;
}

/*
 * Read (the rest of) the response to the last request.
 */
static ssize_t
npfdev_read(devminor_t minor, uint64_t position __unused, endpoint_t endpt,
	cp_grant_id_t grant, size_t size, int flags __unused,
	cdev_id_t id __unused)
{
	struct npfdev *nd;
	size_t left;
	int r;

	if ((nd = npfdev_get_by_minor(minor)) == NULL)
		return EINVAL;

	left = nd->nd_resplen - nd->nd_respoff;
	if (size > left)
		size = left;
	if (size == 0)
		return 0;

	if ((r = sys_safecopyto(endpt, grant, 0,
	    (vir_bytes)nd->nd_resp + nd->nd_respoff, size)) != OK)
		return r;

	nd->nd_respoff += size;

	return size;
}

/*
 * Perform an operation, given as a request written to the device.
 */
static ssize_t
npfdev_write(devminor_t minor, uint64_t position __unused, endpoint_t endpt,
	cp_grant_id_t grant, size_t size, int flags __unused,
	cdev_id_t id __unused)
{
	struct npfdev *nd;
	uint64_t op;
	char *buf;
	int r;

	if ((nd = npfdev_get_by_minor(minor)) == NULL)
		return EINVAL;

	npfdev_free_resp(nd);

	if (size < sizeof(op))
		return EINVAL;
	if (size > NPFDEV_MAX_REQ)
		return E2BIG;

	if ((buf = malloc(size)) == NULL)
		return ENOMEM;

	if ((r = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)buf,
	    size)) != OK) {
		free(buf);

		return r;
	}

	memcpy(&op, buf, sizeof(op));

	r = npf_minix_ctl(pfil_get_npf(), (unsigned long)op,
	    buf + sizeof(op), size - sizeof(op), &nd->nd_resp,
	    &nd->nd_resplen);

	free(buf);

	/* libnpfkern's error numbers are positive; ours are negative. */
	return (r != 0) ? -r : (ssize_t)size;
}

/*
 * Process an I/O control request.
 */
static int
npfdev_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
	cp_grant_id_t grant, int flags __unused, endpoint_t user_endpt __unused,
	cdev_id_t id __unused)
{
	int val, r;

	if (npfdev_get_by_minor(minor) == NULL)
		return EINVAL;

	switch (request) {
	case NPF_MINIX_IOC_VERSION:
		val = NPF_MINIX_VERSION;

		return sys_safecopyto(endpt, grant, 0, (vir_bytes)&val,
		    sizeof(val));

	case NPF_MINIX_IOC_SWITCH:
		if ((r = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)&val,
		    sizeof(val))) != OK)
			return r;

		pfil_set_active(val != 0);

		return OK;

	default:
		return ENOTTY;
	}
}

/*
 * Nothing ever blocks, so there is nothing to cancel.
 */
static int
npfdev_cancel(devminor_t minor __unused, endpoint_t endpt __unused,
	cdev_id_t id __unused)
{

	return EDONTREPLY;
}

/*
 * Reads and writes never block.
 */
static int
npfdev_select(devminor_t minor, unsigned int ops, endpoint_t endpt __unused)
{

	if (npfdev_get_by_minor(minor) == NULL)
		return EINVAL;

	return ops & (CDEV_OP_RD | CDEV_OP_WR);
}

const struct chardriver npfdev_tab = {
	.cdr_open		= npfdev_open,
	.cdr_close		= npfdev_close,
	.cdr_read		= npfdev_read,
	.cdr_write		= npfdev_write,
	.cdr_ioctl		= npfdev_ioctl,
	.cdr_cancel		= npfdev_cancel,
	.cdr_select		= npfdev_select
};
