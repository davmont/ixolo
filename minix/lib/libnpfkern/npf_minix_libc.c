/*
 * The libc parts NPF and its libraries need that a MINIX system service does
 * not have: services link libminc, not libc, and are single-threaded.
 *
 * - pthreads: NPF's standalone layer and libqsbr use mutexes, condition
 *   variables and thread-specific data, which libc lowers to __libc_*()
 *   calls.  The lwip service has one thread, so mutexes do nothing, data
 *   keys are a small table, and nothing may ever wait or start a thread:
 *   NPF runs without G/C workers (npfk_sysinit(0)), its G/C being driven by
 *   a timer instead.
 * - clock_gettime(): the kernel's clock page, or the uptime clock.
 * - a little stdio, vasprintf() and stubs for what libnv and liblpm have
 *   but NPF does not use here (descriptors, sockets, dumps, string parsing).
 */

#include <sys/types.h>
#include <sys/time.h>
#include <minix/sysutil.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Mutexes: one thread, so there is never anyone to exclude. */

int __libc_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __libc_mutex_lock(pthread_mutex_t *);
int __libc_mutex_unlock(pthread_mutex_t *);
int __libc_mutex_destroy(pthread_mutex_t *);

int
__libc_mutex_init(pthread_mutex_t *m __unused,
	const pthread_mutexattr_t *a __unused)
{
	return 0;
}

int
__libc_mutex_lock(pthread_mutex_t *m __unused)
{
	return 0;
}

int
__libc_mutex_unlock(pthread_mutex_t *m __unused)
{
	return 0;
}

int
__libc_mutex_destroy(pthread_mutex_t *m __unused)
{
	return 0;
}

/*
 * Condition variables: signalling is harmless, but a wait could only end by
 * another thread's signal, which cannot come.  Only NPF's G/C worker threads
 * wait, and there are none.
 */

int __libc_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __libc_cond_signal(pthread_cond_t *);
int __libc_cond_broadcast(pthread_cond_t *);
int __libc_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int __libc_cond_timedwait(pthread_cond_t *, pthread_mutex_t *,
	const struct timespec *);
int __libc_cond_destroy(pthread_cond_t *);

int
__libc_cond_init(pthread_cond_t *c __unused,
	const pthread_condattr_t *a __unused)
{
	return 0;
}

int
__libc_cond_signal(pthread_cond_t *c __unused)
{
	return 0;
}

int
__libc_cond_broadcast(pthread_cond_t *c __unused)
{
	return 0;
}

int
__libc_cond_wait(pthread_cond_t *c __unused, pthread_mutex_t *m __unused)
{
	panic("npf: condition wait in a single-threaded service");
}

int
__libc_cond_timedwait(pthread_cond_t *c __unused, pthread_mutex_t *m __unused,
	const struct timespec *ts __unused)
{
	panic("npf: condition wait in a single-threaded service");
}

int
__libc_cond_destroy(pthread_cond_t *c __unused)
{
	return 0;
}

/* Thread-specific data, for the one thread. */

#define NPF_MINIX_KEYS	16

static struct {
	int used;
	void *value;
} keys[NPF_MINIX_KEYS];

int __libc_thr_keycreate(pthread_key_t *, void (*)(void *));
int __libc_thr_keydelete(pthread_key_t);
int __libc_thr_setspecific(pthread_key_t, const void *);
void *__libc_thr_getspecific(pthread_key_t);
int __libc_thr_yield(void);
void __libc_thr_exit(void *) __dead;

int
__libc_thr_keycreate(pthread_key_t *key, void (*destructor)(void *) __unused)
{
	unsigned int i;

	/* The destructor would run at thread exit, which never happens. */
	for (i = 0; i < NPF_MINIX_KEYS; i++) {
		if (!keys[i].used) {
			keys[i].used = 1;
			keys[i].value = NULL;
			*key = (pthread_key_t)i;
			return 0;
		}
	}
	return EAGAIN;
}

int
__libc_thr_keydelete(pthread_key_t key)
{
	if ((unsigned int)key >= NPF_MINIX_KEYS || !keys[key].used)
		return EINVAL;
	keys[key].used = 0;
	keys[key].value = NULL;
	return 0;
}

int
__libc_thr_setspecific(pthread_key_t key, const void *value)
{
	if ((unsigned int)key >= NPF_MINIX_KEYS || !keys[key].used)
		return EINVAL;
	keys[key].value = __UNCONST(value);
	return 0;
}

void *
__libc_thr_getspecific(pthread_key_t key)
{
	if ((unsigned int)key >= NPF_MINIX_KEYS || !keys[key].used)
		return NULL;
	return keys[key].value;
}

/* thmap and libqsbr yield while spinning on contention, which needs two. */
int
__libc_thr_yield(void)
{
	return 0;
}

void
__libc_thr_exit(void *value __unused)
{
	panic("npf: thread exit in a single-threaded service");
}

int
pthread_create(pthread_t *thread __unused, const pthread_attr_t *attr __unused,
	void *(*func)(void *) __unused, void *arg __unused)
{
	return ENOSYS;
}

int
pthread_join(pthread_t thread __unused, void **value __unused)
{
	return ESRCH;
}

/* Time: the kernel's clock page if it has one, or else the uptime clock. */

int __minix_clock_fast(clockid_t, struct timespec *);
int __clock_gettime50(clockid_t, struct timespec *);

int
__clock_gettime50(clockid_t clock_id, struct timespec *ts)
{
	clock_t ticks, realtime;
	time_t boottime;
	u32_t hz;

	if (clock_id != CLOCK_MONOTONIC && clock_id != CLOCK_REALTIME) {
		errno = EINVAL;
		return -1;
	}
	if (__minix_clock_fast(clock_id, ts) == 0)
		return 0;

	if (getuptime(&ticks, &realtime, &boottime) != 0) {
		errno = EINVAL;
		return -1;
	}
	hz = sys_hz();
	if (clock_id == CLOCK_REALTIME) {
		ts->tv_sec = boottime + realtime / hz;
		ts->tv_nsec = (long)((realtime % hz) * (1000000000ULL / hz));
	} else {
		ts->tv_sec = ticks / hz;
		ts->tv_nsec = (long)((ticks % hz) * (1000000000ULL / hz));
	}
	return 0;
}

/* stdio, for libnv's nvlist_dump() and nvlist_fdump(): to the log. */

FILE __sF[3];

int
fprintf(FILE *fp __unused, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vprintf(fmt, ap);
	va_end(ap);
	return r;
}

int
dprintf(int fd __unused, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vprintf(fmt, ap);
	va_end(ap);
	return r;
}


int
fputc(int c, FILE *fp __unused)
{
	kputc(c);
	return c;
}

int
fflush(FILE *fp __unused)
{
	return 0;
}

int
vasprintf(char **ret, const char *fmt, va_list ap)
{
	va_list ap2;
	char *buf;
	int len;

	va_copy(ap2, ap);
	len = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	if (len < 0 || (buf = malloc((size_t)len + 1)) == NULL) {
		*ret = NULL;
		return -1;
	}
	(void)vsnprintf(buf, (size_t)len + 1, fmt, ap);
	*ret = buf;
	return len;
}

/*
 * Not supported in the service: descriptors in nvlists (libnv), nvlists over
 * sockets or ioctls (libnv, NPF's npfk_socket_load()), and text addresses
 * (liblpm's lpm_strtobin(), for npfctl, not the kernel side).
 */

int
fcntl(int fd __unused, int cmd __unused, ...)
{
	errno = EBADF;
	return -1;
}

int
ioctl(int fd __unused, unsigned long request __unused, ...)
{
	errno = ENOTTY;
	return -1;
}

int
inet_pton(int af __unused, const char *src __unused, void *dst __unused)
{
	return 0;
}

struct nvlist;
struct nvlist *nvlist_recv(int, int);
int nvlist_send(int, const struct nvlist *);

struct nvlist *
nvlist_recv(int sock __unused, int flags __unused)
{
	errno = ENOTSUP;
	return NULL;
}

int
nvlist_send(int sock __unused, const struct nvlist *nvl __unused)
{
	errno = ENOTSUP;
	return -1;
}
