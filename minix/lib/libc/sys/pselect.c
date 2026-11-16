/*
 * pselect(2) for MINIX: install the caller's signal mask (PM), run select(2)
 * with the timespec converted to a timeval (VFS), and restore the mask.
 *
 * These are separate calls, so a signal that the new mask unblocks could be
 * caught after the mask is installed but before VFS has the select(); it would
 * then block regardless -- the race pselect() exists to avoid.  So the select()
 * is armed first (__minix_sigarm(), VFS's do_sigarm()): VFS notes a signal
 * caught in between and fails the select() with EINTR instead of blocking.
 */

#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

void __minix_sigarm(int);
int _sys___pselect50(int, fd_set * __restrict, fd_set * __restrict, fd_set * __restrict, const struct timespec * __restrict, const sigset_t * __restrict);

int
_sys___pselect50(int nfds, fd_set * __restrict readfds, fd_set * __restrict writefds,
	fd_set * __restrict exceptfds,
	const struct timespec * __restrict timeout,
	const sigset_t * __restrict sigmask)
{
	struct timeval tv, *tvp = NULL;
	sigset_t omask;
	int r, smask = 0;

	if (timeout != NULL) {
		TIMESPEC_TO_TIMEVAL(&tv, timeout);
		tvp = &tv;
	}

	if (sigmask != NULL) {
		__minix_sigarm(VFS_SELECT);
		if (sigprocmask(SIG_SETMASK, sigmask, &omask) != 0) {
			__minix_sigarm(0);
			return -1;
		}
		smask = 1;
	}

	r = select(nfds, readfds, writefds, exceptfds, tvp);

	if (smask) {
		int saved_errno = errno;
		(void)sigprocmask(SIG_SETMASK, &omask, NULL);
		errno = saved_errno;
	}

	return r;
}

#ifdef __weak_alias
/* libpthread's cancellation points (pthread_cancelstub.c) override these. */
__weak_alias(__pselect50, _sys___pselect50)
__weak_alias(pselect, _sys___pselect50)
#endif
