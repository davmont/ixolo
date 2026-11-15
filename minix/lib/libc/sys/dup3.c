#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/*
 * dup3() is dup2() that can also set close-on-exec on the new descriptor in one
 * step, and that refuses -- rather than quietly succeeding -- when the two
 * descriptors are the same.  Qt uses it: its process code wants a CLOEXEC dup
 * without a window in which the fd could leak across a concurrent exec.
 *
 * VFS does it in one request (F_DUP2FD_CLOEXEC), so that window does not exist.
 */
int
dup3(int from, int to, int flags)
{
	message m;

	/* Unlike dup2(), dup3() is required to fail here rather than no-op. */
	if (from == to) {
		errno = EINVAL;
		return -1;
	}

	if ((flags & ~O_CLOEXEC) != 0) {
		errno = EINVAL;
		return -1;
	}

	memset(&m, 0, sizeof(m));
	m.m_lc_vfs_fcntl.fd = from;
	m.m_lc_vfs_fcntl.cmd = (flags & O_CLOEXEC) ? F_DUP2FD_CLOEXEC : F_DUP2FD;
	m.m_lc_vfs_fcntl.arg_int = to;
	return _syscall(VFS_PROC_NR, VFS_FCNTL, &m);
}
