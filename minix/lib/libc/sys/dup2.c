#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/*
 * dup2(fd, fd2): VFS does the whole job in one request (F_DUP2FD), so fd2 is
 * closed and reused atomically.  It used to be close(fd2) followed by
 * fcntl(F_DUPFD, fd2), which could hand fd2 to another thread's open() in
 * between, and then return a different descriptor than fd2.
 */
int
dup2(int fd, int fd2)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_vfs_fcntl.fd = fd;
	m.m_lc_vfs_fcntl.cmd = F_DUP2FD;
	m.m_lc_vfs_fcntl.arg_int = fd2;
	return _syscall(VFS_PROC_NR, VFS_FCNTL, &m);
}
