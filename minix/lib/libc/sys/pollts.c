/*
 * pollts(2) for MINIX: install the caller's signal mask, run poll(2) (itself
 * layered on select) with the timespec converted to a millisecond timeout,
 * then restore the mask.  As in pselect(2), the select() is armed first so
 * that a signal caught before VFS has it makes it fail with EINTR rather than
 * block.  (Only the first select() poll() makes is armed: poll() makes a
 * second one only after the first failed with EBADF.)
 */

#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/poll.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

void __minix_sigarm(int);
int _sys___pollts50(struct pollfd * __restrict, nfds_t, const struct timespec * __restrict, const sigset_t * __restrict);

int
_sys___pollts50(struct pollfd * __restrict fds, nfds_t nfds,
	const struct timespec * __restrict timeout,
	const sigset_t * __restrict sigmask)
{
	sigset_t omask;
	int r, smask = 0, ms;

	if (timeout == NULL) {
		ms = -1;			/* block indefinitely */
	} else {
		long long msll;

		/* Round up to the next millisecond, clamp to INT_MAX. */
		msll = (long long)timeout->tv_sec * 1000 +
		    (timeout->tv_nsec + 999999) / 1000000;
		ms = (msll > INT_MAX) ? INT_MAX : (int)msll;
	}

	if (sigmask != NULL) {
		/* poll() is select(): arm it, as in pselect(). */
		__minix_sigarm(VFS_SELECT);
		if (sigprocmask(SIG_SETMASK, sigmask, &omask) != 0) {
			__minix_sigarm(0);
			return -1;
		}
		smask = 1;
	}

	r = poll(fds, nfds, ms);

	if (smask) {
		int saved_errno = errno;
		(void)sigprocmask(SIG_SETMASK, &omask, NULL);
		errno = saved_errno;
	}

	return r;
}

/* POSIX.1-2024 ppoll() is pollts(), as in NetBSD. */

#ifdef __weak_alias
/* libpthread's cancellation points (pthread_cancelstub.c) override these. */
__weak_alias(__pollts50, _sys___pollts50)
__weak_alias(pollts, _sys___pollts50)
__weak_alias(ppoll, _sys___pollts50)
#endif
