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
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static __thread int tls = 178;		/* initialized: a .tdata section */

static void *
worker(void *arg)
{
	if (tls != 178) e(1);		/* each thread has its own copy */
	tls = (int)(intptr_t)arg;
	return (void *)(intptr_t)tls;
}

int
main(int argc, char **argv)
{
	unsigned char buf[64], zero[sizeof(buf)], big[GETENTROPY_MAX + 1];
	pthread_t t[4];
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
	tls = 1;
	for (i = 0; i < 4; i++)
		if (pthread_create(&t[i], NULL, worker,
		    (void *)(intptr_t)(i + 10)) != 0) e(5);
	for (i = 0; i < 4; i++) {
		if (pthread_join(t[i], &rv) != 0) e(6);
		if ((intptr_t)rv != i + 10) e(7);
	}
	if (tls != 1) e(8);

	quit();
	return 0;
}
