/* Test 176 - waiting for a time on CLOCK_REALTIME while the clock is set.
 *
 * An absolute clock_nanosleep(2), a POSIX timer set with TIMER_ABSTIME and an
 * absolute pthread_cond_timedwait(3) on CLOCK_REALTIME wait for that time on
 * the clock as it is set meanwhile: sooner if it is set ahead, later if it is
 * set back.  Waits on CLOCK_MONOTONIC and relative ones do not move.  Also
 * the EINVAL and ENOTSUP cases of clock_nanosleep() and clock_settime() (an
 * absolute time before the epoch is not one: it has passed), and
 * an absolute clock_nanosleep() ending with EINTR, as sem_wait(3),
 * sem_timedwait(3) and mq_timedreceive(3) do, also with a time as far off as
 * time_t goes.  The test is setuid root,
 * for clock_settime(); it puts the clock back as it was, with the time that
 * has gone by.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mqueue.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define NSEC	1000000000LL
#define MSEC	1000000LL

static int64_t rt_ahead;	/* realtime - monotonic, as we found it */

static int64_t
now(clockid_t clk)
{
	struct timespec ts;

	if (clock_gettime(clk, &ts) != 0) e(90);
	return (int64_t)ts.tv_sec * NSEC + ts.tv_nsec;
}

static struct timespec
ts_of(int64_t ns)
{
	struct timespec ts;

	ts.tv_sec = ns / NSEC;
	ts.tv_nsec = ns % NSEC;
	return ts;
}

/* Set the realtime clock 'by' ns away from where it was when we began. */
static void
set_clock(int64_t by)
{
	struct timespec ts;

	ts = ts_of(now(CLOCK_MONOTONIC) + rt_ahead + by);
	if (clock_settime(CLOCK_REALTIME, &ts) != 0) e(91);
}

/* The bad cases. */
static void
test176a(void)
{
	struct timespec ts;

	subtest = 1;

	ts.tv_sec = 0;
	ts.tv_nsec = 1000;
	if (clock_nanosleep(99999, 0, &ts, NULL) != EINVAL) e(1);
	if (clock_nanosleep(CLOCK_THREAD_CPUTIME_ID, 0, &ts, NULL) != EINVAL)
		e(2);
	if (clock_nanosleep(CLOCK_PROCESS_CPUTIME_ID, 0, &ts, NULL) !=
	    ENOTSUP) e(3);
	ts.tv_nsec = NSEC;
	if (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL) !=
	    EINVAL) e(4);
	ts.tv_nsec = -1;
	if (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL) != EINVAL) e(5);

	/* A time gone by: at once. */
	ts.tv_sec = 1;
	ts.tv_nsec = 0;
	if (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL) != 0)
		e(6);

	/* Before the epoch: in the past too, not invalid (only the
	 * nanoseconds are); a negative relative time is. */
	{
		sem_t sem;

		ts.tv_sec = -1;
		ts.tv_nsec = 0;
		if (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts,
		    NULL) != 0) e(10);
		if (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL) != EINVAL)
			e(11);
		if (sem_init(&sem, 0, 0) != 0) e(12);
		errno = 0;
		if (sem_timedwait(&sem, &ts) != -1 || errno != ETIMEDOUT)
			e(13);
		if (sem_destroy(&sem) != 0) e(14);
	}

	ts = ts_of(now(CLOCK_REALTIME));
	ts.tv_nsec = NSEC;
	errno = 0;
	if (clock_settime(CLOCK_REALTIME, &ts) != -1 || errno != EINVAL) e(7);
	ts.tv_nsec = -1;
	errno = 0;
	if (clock_settime(CLOCK_REALTIME, &ts) != -1 || errno != EINVAL) e(8);
	ts.tv_nsec = 0;
	errno = 0;
	if (clock_settime(CLOCK_MONOTONIC, &ts) != -1 || errno != EINVAL)
		e(9);
}

/* An absolute clock_nanosleep() for 'wait' ns, in a child, with the clock set
 * 'by' ns 'after' ns into it: how long it slept on the monotonic clock.
 */
static int64_t
sleep_while_set(clockid_t clk, int64_t wait, int64_t after, int64_t by)
{
	struct timespec ts;
	int64_t m0;
	pid_t pid;
	int status;

	m0 = now(CLOCK_MONOTONIC);
	ts = ts_of(now(clk) + wait);
	if ((pid = fork()) == -1) e(92);
	if (pid == 0) {
		if (clock_nanosleep(clk, TIMER_ABSTIME, &ts, NULL) != 0)
			_exit(1);
		_exit(0);
	}
	usleep(after / 1000);
	set_clock(by);
	if (waitpid(pid, &status, 0) != pid) e(93);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(94);
	set_clock(0);
	return now(CLOCK_MONOTONIC) - m0;
}

static void
test176b(void)
{
	int64_t t;

	subtest = 2;

	/* 2 s; at 0.5 s the clock goes 5 s ahead: it is time. */
	t = sleep_while_set(CLOCK_REALTIME, 2 * NSEC, 500 * MSEC, 5 * NSEC);
	if (t < 400 * MSEC || t > 1200 * MSEC) e(1);

	/* 1 s; at 0.5 s the clock goes 1.5 s back: 2.5 s in all. */
	t = sleep_while_set(CLOCK_REALTIME, NSEC, 500 * MSEC, -1500 * MSEC);
	if (t < 2400 * MSEC || t > 3200 * MSEC) e(2);

	/* On the monotonic clock, 1.5 s whatever. */
	t = sleep_while_set(CLOCK_MONOTONIC, 1500 * MSEC, 500 * MSEC,
	    5 * NSEC);
	if (t < 1500 * MSEC || t > 2200 * MSEC) e(3);
}

/* A POSIX timer for a time on the realtime clock. */
static void
test176c(void)
{
	struct sigevent ev;
	struct itimerspec its;
	timer_t tid;
	sigset_t set;
	int64_t m0, t;
	int sig;

	subtest = 3;

	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &set, NULL) != 0) e(1);
	memset(&ev, 0, sizeof(ev));
	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = SIGUSR1;
	if (timer_create(CLOCK_REALTIME, &ev, &tid) != 0) e(2);

	/* 1 s; at 0.5 s the clock goes 1.5 s back: 2.5 s in all. */
	m0 = now(CLOCK_MONOTONIC);
	memset(&its, 0, sizeof(its));
	its.it_value = ts_of(now(CLOCK_REALTIME) + NSEC);
	if (timer_settime(tid, TIMER_ABSTIME, &its, NULL) != 0) e(3);
	usleep(500000);
	set_clock(-1500 * MSEC);
	if (sigwait(&set, &sig) != 0 || sig != SIGUSR1) e(4);
	t = now(CLOCK_MONOTONIC) - m0;
	set_clock(0);
	if (t < 2400 * MSEC || t > 3200 * MSEC) e(5);

	/* 3 s; at 0.5 s the clock goes 5 s ahead: it is time. */
	m0 = now(CLOCK_MONOTONIC);
	its.it_value = ts_of(now(CLOCK_REALTIME) + 3 * NSEC);
	if (timer_settime(tid, TIMER_ABSTIME, &its, NULL) != 0) e(6);
	usleep(500000);
	set_clock(5 * NSEC);
	if (sigwait(&set, &sig) != 0 || sig != SIGUSR1) e(7);
	t = now(CLOCK_MONOTONIC) - m0;
	set_clock(0);
	if (t < 400 * MSEC || t > 1200 * MSEC) e(8);

	/* A relative one does not move: 1.5 s. */
	m0 = now(CLOCK_MONOTONIC);
	its.it_value = ts_of(1500 * MSEC);
	if (timer_settime(tid, 0, &its, NULL) != 0) e(9);
	usleep(500000);
	set_clock(5 * NSEC);
	if (sigwait(&set, &sig) != 0 || sig != SIGUSR1) e(10);
	t = now(CLOCK_MONOTONIC) - m0;
	set_clock(0);
	if (t < 1500 * MSEC || t > 2200 * MSEC) e(11);

	if (timer_delete(tid) != 0) e(12);
	if (sigprocmask(SIG_UNBLOCK, &set, NULL) != 0) e(13);
}

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static volatile int waiting;

static void *
waiter(void *arg)
{
	struct timespec ts;
	intptr_t r;

	ts = ts_of(now(CLOCK_REALTIME) + (intptr_t)arg * NSEC);
	pthread_mutex_lock(&mtx);
	waiting = 1;
	do
		r = pthread_cond_timedwait(&cv, &mtx, &ts);
	while (r == 0);
	pthread_mutex_unlock(&mtx);
	return (void *)r;
}

/* An absolute pthread_cond_timedwait() on the realtime clock (the default). */
static void
test176d(void)
{
	pthread_t t;
	void *r;
	int64_t m0, d;

	subtest = 4;

	/* 3 s; at 0.5 s the clock goes 5 s ahead: it is time. */
	m0 = now(CLOCK_MONOTONIC);
	waiting = 0;
	if (pthread_create(&t, NULL, waiter, (void *)3) != 0) e(1);
	usleep(500000);
	if (!waiting) e(2);
	set_clock(5 * NSEC);
	if (pthread_join(t, &r) != 0) e(3);
	d = now(CLOCK_MONOTONIC) - m0;
	set_clock(0);
	if ((intptr_t)r != ETIMEDOUT) e(4);
	if (d < 400 * MSEC || d > 1200 * MSEC) e(5);

	/* 1 s; at 0.5 s the clock goes 1.5 s back: 2.5 s in all. */
	m0 = now(CLOCK_MONOTONIC);
	if (pthread_create(&t, NULL, waiter, (void *)1) != 0) e(6);
	usleep(500000);
	set_clock(-1500 * MSEC);
	if (pthread_join(t, &r) != 0) e(7);
	d = now(CLOCK_MONOTONIC) - m0;
	set_clock(0);
	if ((intptr_t)r != ETIMEDOUT) e(8);
	if (d < 2400 * MSEC || d > 3200 * MSEC) e(9);
}

static void
on_alarm(int sig)
{
}

/* An absolute clock_nanosleep() is interrupted by a signal. */
static void
test176e(void)
{
	struct sigaction sa;
	struct timespec ts;
	int64_t m0, t;

	subtest = 5;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm;
	sa.sa_flags = SA_RESTART;	/* still: it is not restarted */
	if (sigaction(SIGALRM, &sa, NULL) != 0) e(1);
	m0 = now(CLOCK_MONOTONIC);
	ts = ts_of(now(CLOCK_REALTIME) + 5 * NSEC);
	alarm(1);
	if (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL) != EINTR)
		e(2);
	t = now(CLOCK_MONOTONIC) - m0;
	if (t < 900 * MSEC || t > 2 * NSEC) e(3);
}

/* A caught signal ends a wait with EINTR: an alarm comes after 1 s. */
static int
interrupted(int64_t m0)
{
	int64_t t;

	t = now(CLOCK_MONOTONIC) - m0;
	return (errno == EINTR && t >= 900 * MSEC && t <= 2 * NSEC);
}

static void
test176f(void)
{
	struct sigaction sa;
	struct timespec far;
	struct mq_attr attr;
	char name[32], buf[16];
	sem_t sem;
	mqd_t mq;
	int64_t m0;

	subtest = 6;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm;
	if (sigaction(SIGALRM, &sa, NULL) != 0) e(1);
	far.tv_sec = (time_t)INT64_MAX;
	far.tv_nsec = 0;

	if (sem_init(&sem, 0, 0) != 0) e(2);
	m0 = now(CLOCK_MONOTONIC);
	alarm(1);
	if (sem_wait(&sem) != -1 || !interrupted(m0)) e(3);
	m0 = now(CLOCK_MONOTONIC);
	alarm(1);
	if (sem_timedwait(&sem, &far) != -1 || !interrupted(m0)) e(4);
	/* Still there to post to and take. */
	if (sem_post(&sem) != 0 || sem_wait(&sem) != 0) e(5);
	if (sem_destroy(&sem) != 0) e(6);

	snprintf(name, sizeof(name), "/test176.%d", (int)getpid());
	memset(&attr, 0, sizeof(attr));
	attr.mq_maxmsg = 2;
	attr.mq_msgsize = sizeof(buf);
	if ((mq = mq_open(name, O_CREAT | O_RDWR, 0600, &attr)) ==
	    (mqd_t)-1) e(7);
	m0 = now(CLOCK_MONOTONIC);
	alarm(1);
	if (mq_timedreceive(mq, buf, sizeof(buf), NULL, &far) != -1 ||
	    !interrupted(m0)) e(8);
	if (mq_close(mq) != 0 || mq_unlink(name) != 0) e(9);
}

int
main(int argc, char **argv)
{

	start(176);

	rt_ahead = now(CLOCK_REALTIME) - now(CLOCK_MONOTONIC);

	test176a();
	test176b();
	test176c();
	test176d();
	test176e();
	test176f();

	quit();
	return 0;
}
