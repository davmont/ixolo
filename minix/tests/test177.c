/* Test 177 - mutex and condition variable corner cases.
 *
 * pthread_mutex_timedlock(3) with a bad abs_timeout fails with EINVAL when it
 * would block (and locks a free mutex without looking at it); a zero-filled
 * mutex and condition variable work as statically initialized ones, as with
 * glibc; and a thread cancelled while blocked in pthread_cond_wait(3) on a
 * private or a process-shared condition variable leaves it, with the mutex
 * locked again for its clean-up handlers.  A signal caught by a thread in
 * pthread_join(3) runs its handler then, and the join goes on.
 */
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static int ret;

static void *
relock(void *arg)
{
	struct timespec *ts = arg;

	if (pthread_mutex_lock(&mtx) != 0) e(1);
	ret = pthread_mutex_timedlock(&mtx, ts);	/* would deadlock */
	if (pthread_mutex_unlock(&mtx) != 0) e(2);
	return NULL;
}

/* A bad abs_timeout: EINVAL only once the mutex is not free. */
static void
test177a(void)
{
	struct timespec ts;
	pthread_t t;
	long bad[2] = { -1, 1000000000L };
	int i;

	subtest = 1;

	for (i = 0; i < 2; i++) {
		if (clock_gettime(CLOCK_REALTIME, &ts) != 0) e(3);
		ts.tv_sec += 3;
		ts.tv_nsec = bad[i];

		/* Free: locked, the timeout not looked at. */
		if (pthread_mutex_timedlock(&mtx, &ts) != 0) e(4);
		if (pthread_mutex_unlock(&mtx) != 0) e(5);

		/* Held: EINVAL, at once. */
		ret = -1;
		if (pthread_create(&t, NULL, relock, &ts) != 0) e(6);
		if (pthread_join(t, NULL) != 0) e(7);
		if (ret != EINVAL) e(8);
	}
}

static struct {
	pthread_mutex_t m;
	pthread_cond_t c;
	int flag;
} *z;

static void *
zsignal(void *arg)
{
	(void)arg;

	if (pthread_mutex_lock(&z->m) != 0) e(10);
	z->flag = 1;
	if (pthread_cond_signal(&z->c) != 0) e(11);
	if (pthread_mutex_unlock(&z->m) != 0) e(12);
	return NULL;
}

/* Zero-filled objects are statically initialized ones. */
static void
test177b(void)
{
	struct timespec ts;
	pthread_t t;

	subtest = 2;

	if ((z = calloc(1, sizeof(*z))) == NULL) e(13);

	if (pthread_mutex_trylock(&z->m) != 0) e(14);
	if (pthread_mutex_unlock(&z->m) != 0) e(15);
	if (pthread_cond_broadcast(&z->c) != 0) e(16);

	/* A timed wait times out... */
	if (pthread_mutex_lock(&z->m) != 0) e(17);
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) e(18);
	ts.tv_nsec += 50000000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000;
	}
	if (pthread_cond_timedwait(&z->c, &z->m, &ts) != ETIMEDOUT) e(19);

	/* ...and a wait is woken. */
	if (pthread_create(&t, NULL, zsignal, NULL) != 0) e(20);
	while (!z->flag)
		if (pthread_cond_wait(&z->c, &z->m) != 0) e(21);
	if (pthread_mutex_unlock(&z->m) != 0) e(22);
	if (pthread_join(t, NULL) != 0) e(23);

	if (pthread_cond_destroy(&z->c) != 0) e(24);
	if (pthread_mutex_destroy(&z->m) != 0) e(25);
	free(z);

	/* A destroyed mutex is not taken for a zero-filled one. */
	{
		pthread_mutex_t m;

		if (pthread_mutex_init(&m, NULL) != 0) e(26);
		if (pthread_mutex_destroy(&m) != 0) e(27);
		if (pthread_mutex_lock(&m) != EINVAL) e(28);
	}

	/* Untouched zero-filled objects can be destroyed directly, and
	 * unlocking an untouched zero-filled mutex fails with EPERM. */
	if ((z = calloc(1, sizeof(*z))) == NULL) e(29);
	if (pthread_mutex_unlock(&z->m) != EPERM) e(60);
	if (pthread_cond_destroy(&z->c) != 0) e(61);
	if (pthread_cond_destroy(&z->c) != EINVAL) e(62);
	if (pthread_mutex_destroy(&z->m) != 0) e(63);
	if (pthread_mutex_destroy(&z->m) != EINVAL) e(64);
	free(z);
}

static pthread_mutex_t cm;
static pthread_cond_t cc;
static volatile int waiting, cleaned, cond_flag;

static void
cancel_cleanup(void *arg)
{
	(void)arg;

	/* The mutex is ours again: an error-checking one says so. */
	if (pthread_mutex_unlock(&cm) != 0) e(30);
	cleaned = 1;
}

static void *
waiter(void *arg)
{
	(void)arg;

	if (pthread_mutex_lock(&cm) != 0) e(31);
	pthread_cleanup_push(cancel_cleanup, NULL);
	waiting++;
	while (!cond_flag)
		(void)pthread_cond_wait(&cc, &cm);
	pthread_cleanup_pop(0);
	if (pthread_mutex_unlock(&cm) != 0) e(65);
	return NULL;
}

static void
timeout(int sig)
{
	(void)sig;

	e(32);					/* the cancel did not get there */
	quit();
}

/* Cancel a thread blocked in pthread_cond_wait(). */
static void
cancel_wait(int pshared)
{
	pthread_mutexattr_t ma;
	pthread_condattr_t ca;
	pthread_t t, t2;
	struct timespec bad_ts;
	void *rv;

	if (pthread_mutexattr_init(&ma) != 0) e(33);
	if (pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_ERRORCHECK) != 0)
		e(34);
	if (pthread_condattr_init(&ca) != 0) e(35);
	if (pshared &&
	    (pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED) != 0 ||
	    pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED) != 0))
		e(36);
	if (pthread_mutex_init(&cm, &ma) != 0) e(37);
	if (pthread_cond_init(&cc, &ca) != 0) e(38);

	/* Invalid abstime in pthread_cond_timedwait(): EINVAL without
	 * releasing the mutex. */
	if (pthread_mutex_lock(&cm) != 0) e(66);
	bad_ts.tv_sec = 0;
	bad_ts.tv_nsec = -1;
	if (pthread_cond_timedwait(&cc, &cm, &bad_ts) != EINVAL) e(67);
	if (pthread_mutex_unlock(&cm) != 0) e(68);

	waiting = cleaned = cond_flag = 0;
	if (pthread_create(&t, NULL, waiter, NULL) != 0) e(39);
	while (waiting < 1)
		usleep(1000);
	/* It has the mutex until it waits. */
	if (pthread_mutex_lock(&cm) != 0) e(40);
	if (pthread_mutex_unlock(&cm) != 0) e(41);
	usleep(50000);

	signal(SIGALRM, timeout);
	alarm(10);
	if (pthread_cancel(t) != 0) e(42);
	if (pthread_join(t, &rv) != 0) e(43);
	alarm(0);
	if (rv != PTHREAD_CANCELED) e(44);
	if (!cleaned) e(45);

	/* Cancelling one of two waiters concurrently with pthread_cond_signal()
	 * must not consume the signal meant for the surviving waiter. */
	{
		pthread_t victim, survivor;

		waiting = cleaned = cond_flag = 0;
		if (pthread_create(&t, NULL, waiter, NULL) != 0) e(69);
		while (waiting < 1)
			usleep(1000);
		if (pthread_mutex_lock(&cm) != 0) e(70);
		if (pthread_mutex_unlock(&cm) != 0) e(71);
		usleep(10000);

		if (pthread_create(&t2, NULL, waiter, NULL) != 0) e(72);
		while (waiting < 2)
			usleep(1000);
		if (pthread_mutex_lock(&cm) != 0) e(73);
		usleep(20000);

		cond_flag = 1;
		if (pthread_cond_signal(&cc) != 0) e(74);
		usleep(20000);
		/* Cancel the waiter that pthread_cond_signal() woke (FIFO for
		 * pshared, LIFO for private) while it is blocked re-locking cm,
		 * so it must forward the signal to the surviving waiter. */
		victim = pshared ? t : t2;
		survivor = pshared ? t2 : t;
		if (pthread_cancel(victim) != 0) e(75);
		if (pthread_mutex_unlock(&cm) != 0) e(76);
		alarm(10);
		if (pthread_join(victim, &rv) != 0) e(77);
		if (rv != PTHREAD_CANCELED) e(78);
		if (pthread_join(survivor, &rv) != 0) e(79);
		if (rv != NULL) e(80);
		alarm(0);
		if (!cleaned) e(81);
	}

	if (pthread_cond_destroy(&cc) != 0) e(46);
	if (pthread_mutex_destroy(&cm) != 0) e(47);
}

static void
test177c(void)
{
	subtest = 3;
	cancel_wait(0);
}

static void
test177d(void)
{
	subtest = 4;
	cancel_wait(1);
}

static int pfd[2];
static volatile int caught;

static void *
blocked(void *arg)
{
	char c;

	(void)arg;

	if (read(pfd[0], &c, 1) != 1) e(50);
	return NULL;
}

static void
release(int sig)
{
	(void)sig;

	caught = 1;
	(void)write(pfd[1], "x", 1);
}

/* A signal handler runs while the thread is in pthread_join(); here it is
 * the one that lets the joined thread end. */
static void
test177e(void)
{
	struct sigaction sa;
	sigset_t set;
	pthread_t t;

	subtest = 5;

	if (pipe(pfd) != 0) e(51);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = release;
	if (sigaction(SIGALRM, &sa, NULL) != 0) e(52);
	caught = 0;
	/* Only the main thread may take it: the new one starts with it
	 * blocked. */
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) e(53);
	if (pthread_create(&t, NULL, blocked, NULL) != 0) e(54);
	if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0) e(57);
	alarm(1);
	if (pthread_join(t, NULL) != 0) e(55);	/* never EINTR */
	if (!caught) e(56);
	close(pfd[0]);
	close(pfd[1]);
}

int
main(int argc, char **argv)
{
	start(177);

	test177a();
	test177b();
	test177c();
	test177d();
	test177e();

	quit();
	return 0;
}
