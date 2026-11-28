/* LWIP service - cdev.c - character device request dispatch */
/*
 * The LWIP service offers two character devices, with the same major number:
 * /dev/bpf (and its clones), with minor numbers below NPFDEV_MINOR, and
 * /dev/npf (and its clones), with minor numbers from NPFDEV_MINOR up.  Route
 * each request to the right one by its minor number.
 */

#include "lwip.h"
#include "bpfdev.h"
#include "npfdev.h"

#include <minix/chardriver.h>

static const struct chardriver *
cdev_get(devminor_t minor)
{

	return (minor >= NPFDEV_MINOR) ? &npfdev_tab : &bpfdev_tab;
}

static int
cdev_open(devminor_t minor, int access, endpoint_t user_endpt)
{

	return cdev_get(minor)->cdr_open(minor, access, user_endpt);
}

static int
cdev_close(devminor_t minor)
{

	return cdev_get(minor)->cdr_close(minor);
}

static ssize_t
cdev_read(devminor_t minor, uint64_t position, endpoint_t endpt,
	cp_grant_id_t grant, size_t size, int flags, cdev_id_t id)
{

	return cdev_get(minor)->cdr_read(minor, position, endpt, grant, size,
	    flags, id);
}

static ssize_t
cdev_write(devminor_t minor, uint64_t position, endpoint_t endpt,
	cp_grant_id_t grant, size_t size, int flags, cdev_id_t id)
{

	return cdev_get(minor)->cdr_write(minor, position, endpt, grant, size,
	    flags, id);
}

static int
cdev_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
	cp_grant_id_t grant, int flags, endpoint_t user_endpt, cdev_id_t id)
{

	return cdev_get(minor)->cdr_ioctl(minor, request, endpt, grant, flags,
	    user_endpt, id);
}

static int
cdev_cancel(devminor_t minor, endpoint_t endpt, cdev_id_t id)
{

	return cdev_get(minor)->cdr_cancel(minor, endpt, id);
}

static int
cdev_select(devminor_t minor, unsigned int ops, endpoint_t endpt)
{

	return cdev_get(minor)->cdr_select(minor, ops, endpt);
}

static const struct chardriver cdev_tab = {
	.cdr_open		= cdev_open,
	.cdr_close		= cdev_close,
	.cdr_read		= cdev_read,
	.cdr_write		= cdev_write,
	.cdr_ioctl		= cdev_ioctl,
	.cdr_cancel		= cdev_cancel,
	.cdr_select		= cdev_select
};

/*
 * Process a character driver request.
 */
void
cdev_process(message * m_ptr, int ipc_status)
{

	chardriver_process(&cdev_tab, m_ptr, ipc_status);
}
