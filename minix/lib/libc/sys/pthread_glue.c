/*
 * pthread_glue.c — MINIX libc support for libpthread.
 *
 * libpthread references a number of NetBSD primitives MINIX libc does not yet
 * implement.  This file provides them: NetBSD-internal LWP and scheduler calls
 * as stubs, _sys_<call> raw-syscall variants as thin wrappers over the normal
 * libc entry points, and a few public POSIX functions MINIX libc is missing.
 *
 * NOT covered here: _lwp_wait(2) and _lwp_detach(2) (real PM calls, see
 * _lwp_wait.c) and per-thread signals/_lwp_kill (lwp_stubs.c).
 *
 * TODO: CPU affinity (_sched_[gs]etaffinity).
 */
#include <sys/cdefs.h>
/*
 * NB: intentionally no "namespace.h" — these definitions must export the public
 * symbol names (sched_yield, pthread_atfork, mprotect, ...) that libpthread
 * references; namespace.h would rename some of them to their _-prefixed forms.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <lib.h>
#include <minix/callnr.h>
#include <lwp.h>
#include <sys/lwpctl.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* --- NetBSD-internal LWP control: not yet supported on MINIX. --- */
int _lwp_suspend(lwpid_t l)			{ (void)l; errno = ENOSYS; return -1; }
int _lwp_continue(lwpid_t l)			{ (void)l; errno = ENOSYS; return -1; }
int _lwp_setname(lwpid_t l, const char *n)	{ (void)l; (void)n; return 0; }

/*
 * _lwp_ctl: hand libpthread a valid lwpctl page so it can read lc_curcpu.
 * We have no kernel-maintained per-CPU state, so we point every caller at a
 * single shared, immutable struct reporting LWPCTL_CPU_NONE.  That value tells
 * the mutex/rwlock spin paths "the holder is not currently on a CPU", so they
 * skip the spin-while-running optimization and park immediately -- correct,
 * just without the adaptive-spin speedup.  Returning failure here is not an
 * option: pthread__init() treats it as fatal (err(3)).
 */
static lwpctl_t __lwpctl_shared = { .lc_curcpu = LWPCTL_CPU_NONE, .lc_pctr = 0 };
int _lwp_ctl(int f, struct lwpctl **a)
{
	(void)f;
	if (a != NULL)
		*a = &__lwpctl_shared;
	return 0;
}



/* --- Public POSIX functions MINIX libc is missing. --- */

/*
 * mprotect(2): VM changes the protection of the page-aligned range.  Among
 * others, libpthread relies on it for thread stack guard pages.
 */
int
mprotect(void *addr, size_t len, int prot)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_vm_mprotect.addr = addr;
	m.m_lc_vm_mprotect.len = len;
	m.m_lc_vm_mprotect.prot = prot;
	return _syscall(VM_PROC_NR, VM_MPROTECT, &m);
}

int _sys___nanosleep50(const struct timespec *, struct timespec *);
int _sys_clock_nanosleep(clockid_t, int, const struct timespec *,
    struct timespec *);

/* clock_nanosleep(2).  A time from now is a nanosleep(2).  A time on the
 * clock is a timed _lwp_park(): PM waits for it on the clock as it is set
 * meanwhile (clock_settime(2)).  An unpark that comes meanwhile is not ours,
 * but for a wait of libpthread's this thread is about to make, or makes in a
 * signal handler; it is passed on.
 * A cancellation point: libpthread's clock_nanosleep overrides the name. */
#ifdef __weak_alias
__weak_alias(clock_nanosleep, _sys_clock_nanosleep)
__weak_alias(_clock_nanosleep, _sys_clock_nanosleep)
#endif
int
_sys_clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *rqtp,
    struct timespec *rmtp)
{
	struct timespec res;
	int r, saved, unparked;

	saved = errno;
	switch (clock_id) {
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
		break;
	case CLOCK_THREAD_CPUTIME_ID:	/* that never moves while we sleep */
		return EINVAL;
	default:
		/* Another known clock: a CPU-time one. */
		r = (clock_getres(clock_id, &res) == 0) ? ENOTSUP : EINVAL;
		errno = saved;
		return r;
	}
	if (rqtp->tv_sec < 0 || rqtp->tv_nsec < 0 ||
	    rqtp->tv_nsec >= 1000000000L)
		return EINVAL;

	if (!(flags & TIMER_ABSTIME)) {
		r = (_sys___nanosleep50(rqtp, rmtp) == -1) ? errno : 0;
		errno = saved;
		return r;
	}

	unparked = 0;
	for (;;) {
		if (_lwp_park(clock_id, TIMER_ABSTIME, rqtp, 0, NULL,
		    NULL) == 0) {
			unparked = 1;
			continue;
		}
		r = (errno == ETIMEDOUT) ? 0 : errno;
		break;
	}
	if (unparked)
		(void) _lwp_unpark(_lwp_self(), NULL);
	errno = saved;
	return r;
}

/* rasctl(2): restartable atomic sequences are NetBSD-specific; unsupported. */
int
rasctl(void *addr, size_t len, int op)
{
	(void)addr; (void)len; (void)op;
	errno = ENOSYS;
	return -1;
}

/*
 * pthread_atfork(3) is implemented in fork.c (it has to wrap the PM fork
 * message), so no stub is needed here.
 */

/* --- _sys_<call>: raw (non-cancellable) syscall variants libpthread calls
 *     directly.  Those of the cancellation points are where the calls are
 *     (read.c, write.c, ...). --- */

/*
 * Raw sigprocmask: issue the PM syscall directly.  We must NOT call the public
 * sigprocmask()/__sigprocmask14(): libpthread strong-aliases __sigprocmask14 to
 * pthread_sigmask (== __libc_thr_sigsetmask), which itself calls back into
 * _sys___sigprocmask14 -- forwarding to the public name would recurse forever.
 */
int
_sys___sigprocmask14(int how, const sigset_t *set, sigset_t *oset)
{
	message m;

	memset(&m, 0, sizeof(m));
	if (set == NULL) {
		m.m_lc_pm_sigset.how = SIG_INQUIRE;
		sigemptyset(&m.m_lc_pm_sigset.set);
	} else {
		/* SIG_INQUIRE is ours, not the caller's. */
		if (how != SIG_BLOCK && how != SIG_UNBLOCK &&
		    how != SIG_SETMASK) {
			errno = EINVAL;
			return -1;
		}
		m.m_lc_pm_sigset.how = how;
		m.m_lc_pm_sigset.set = *set;
	}
	if (_syscall(PM_PROC_NR, PM_SIGPROCMASK, &m) < 0)
		return -1;
	if (oset != NULL)
		*oset = m.m_pm_lc_sigset.set;
	return m.m_type;
}

/*
 * Raw setcontext: call libc's strong _setcontext.  The public setcontext is a
 * weak alias overridden by libpthread's pthread_setcontext, which calls back
 * into _sys_setcontext -- so forwarding to the public name would recurse.
 */
extern int _setcontext(const ucontext_t *);
int
_sys_setcontext(const ucontext_t *ucp)
{
	return _setcontext(ucp);
}

