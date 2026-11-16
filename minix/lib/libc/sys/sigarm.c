#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>

/*
 * Arm the next VFS call 'callnr' (VFS_SELECT or VFS_ACCEPT) of this process
 * for pselect(2), pollts(2) and paccept(2): a signal caught from now on makes
 * that call fail with EINTR instead of blocking, even if it was caught before
 * VFS received the call.  See do_sigarm() in VFS.  Callers arm, install their
 * signal mask, and then make the call.
 *
 * Failure (a VFS without the call) is not reported: the caller then works as
 * before, with the race this closes.
 */
void __minix_sigarm(int);

void
__minix_sigarm(int callnr)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_vfs_sigarm.callnr = callnr;
	(void)_syscall(VFS_PROC_NR, VFS_SIGARM, &m);
}
