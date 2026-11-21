/* Test 178 - a statically linked program with libpthread.
 *
 * _libc_init() takes the stack protector's guard from getentropy(3) before
 * it sets up TLS.  getentropy() went through libpthread's cancellation-point
 * stubs for open/read/close, which read the thread pointer, so every
 * statically linked program with libpthread died before main() on a read of
 * address 0.  This one is linked statically: starting at all is the first
 * check.  Then: getentropy() and threads work, and every thread, the first
 * one too, starts with the initial value of a thread-local smaller than a
 * pointer: the static TLS allocator copied it to the start of the block,
 * rounded up to the pointer size, rather than where the linker put it, just
 * below the thread pointer.
 */
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static __thread int tls = 178;		/* initialized: a .tdata section */
static __thread __attribute__((aligned(64))) unsigned char tls64[64] = { 0x5a };
static volatile int noncp_ok;

static void *
worker(void *arg)
{
	if (tls != 178) e(1);		/* each thread has its own copy */
	if (((uintptr_t)tls64 & 63) != 0) e(10);
	if (tls64[0] != 0x5a) e(11);
	tls = (int)(intptr_t)arg;
	tls64[0] = (unsigned char)(intptr_t)arg;
	return (void *)(intptr_t)tls;
}

static void *
noncancel_worker(void *arg)
{
	unsigned char buf[16];

	(void)arg;

	/* Neither getentropy(3) nor arc4random_buf(3) is a cancellation
	 * point: a pending deferred cancel must wait until testcancel(). */
	if (pthread_cancel(pthread_self()) != 0) e(12);
	if (getentropy(buf, sizeof(buf)) != 0) e(13);
	arc4random_buf(buf, sizeof(buf));
	noncp_ok = 1;
	pthread_testcancel();
	e(14);
	return NULL;
}

int
main(int argc, char **argv)
{
	unsigned char buf[64], zero[sizeof(buf)], big[GETENTROPY_MAX + 1];
	pthread_t t[4], tc;
	void *rv;
	int i;

	start(178);

	subtest = 1;
	memset(buf, 0, sizeof(buf));
	memset(zero, 0, sizeof(zero));
	if (getentropy(buf, sizeof(buf)) != 0) e(2);
	if (memcmp(buf, zero, sizeof(buf)) == 0) e(3);
	if (getentropy(big, sizeof(big)) != -1 || errno != EINVAL) e(4);

	subtest = 2;
	if (tls != 178) e(9);
	if (((uintptr_t)tls64 & 63) != 0) e(15);
	if (tls64[0] != 0x5a) e(16);
	tls = 1;
	tls64[0] = 1;
	for (i = 0; i < 4; i++)
		if (pthread_create(&t[i], NULL, worker,
		    (void *)(intptr_t)(i + 10)) != 0) e(5);
	for (i = 0; i < 4; i++) {
		if (pthread_join(t[i], &rv) != 0) e(6);
		if ((intptr_t)rv != i + 10) e(7);
	}
	if (tls != 1 || tls64[0] != 1) e(8);

	subtest = 3;
	noncp_ok = 0;
	if (pthread_create(&tc, NULL, noncancel_worker, NULL) != 0) e(17);
	if (pthread_join(tc, &rv) != 0) e(18);
	if (rv != PTHREAD_CANCELED || !noncp_ok) e(19);

	quit();
	return 0;
}
