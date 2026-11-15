/* Test 174 - dup2(2) and dup3(2).
 *
 * Both are one VFS request (fcntl F_DUP2FD / F_DUP2FD_CLOEXEC), so the target
 * descriptor is replaced atomically: it is never free, so a concurrent open()
 * in another thread cannot get it, and dup2() always returns the descriptor
 * it was asked for.  Also checks the POSIX details: close-on-exec, fd == fd2,
 * bad arguments leave fd2 alone, and the old file of fd2 really is closed.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static int
cloexec(int fd)
{
	int fl;

	if ((fl = fcntl(fd, F_GETFD)) == -1) e(90);
	return (fl & FD_CLOEXEC) != 0;
}

/* Basic dup2(): the new fd shares the open file and has FD_CLOEXEC clear. */
static void
test174a(void)
{
	int a, b;

	subtest = 1;

	if ((a = open("t174.a", O_RDWR | O_CREAT | O_TRUNC, 0644)) == -1) e(1);
	if ((b = open("t174.b", O_RDWR | O_CREAT | O_TRUNC, 0644)) == -1) e(2);
	if (fcntl(b, F_SETFD, FD_CLOEXEC) == -1) e(3);

	if (dup2(a, b) != b) e(4);
	if (cloexec(b)) e(5);
	/* Same open file: the offset is shared. */
	if (write(a, "hello", 5) != 5) e(6);
	if (lseek(b, 0, SEEK_CUR) != 5) e(7);

	/* fd == fd2: returns fd2 and leaves FD_CLOEXEC alone. */
	if (fcntl(a, F_SETFD, FD_CLOEXEC) == -1) e(8);
	if (dup2(a, a) != a) e(9);
	if (!cloexec(a)) e(10);

	/* fd2 already a duplicate of fd (same filp). */
	if (dup2(a, b) != b) e(11);
	if (lseek(a, 0, SEEK_CUR) != 5 || lseek(b, 0, SEEK_CUR) != 5) e(12);

	/* Bad arguments fail with EBADF and leave fd2 open. */
	if (dup2(OPEN_MAX + 3, b) != -1 || errno != EBADF) e(13);
	if (dup2(a, -1) != -1 || errno != EBADF) e(14);
	if (dup2(a, OPEN_MAX) != -1 || errno != EBADF) e(15);
	if (fcntl(b, F_GETFL) == -1) e(16);

	/* A closed fd2 is fine too. */
	if (close(b) != 0) e(17);
	if (dup2(a, b) != b) e(18);
	if (lseek(b, 0, SEEK_CUR) != 5) e(19);

	(void)close(a);
	(void)close(b);
	(void)unlink("t174.a");
	(void)unlink("t174.b");
}

/* dup3(). */
static void
test174b(void)
{
	int a, b;

	subtest = 2;

	if ((a = open("t174.a", O_RDWR | O_CREAT | O_TRUNC, 0644)) == -1) e(1);
	if ((b = dup(a)) == -1) e(2);

	if (dup3(a, a, 0) != -1 || errno != EINVAL) e(3);
	if (dup3(a, b, ~O_CLOEXEC) != -1 || errno != EINVAL) e(4);

	if (dup3(a, b, O_CLOEXEC) != b) e(5);
	if (!cloexec(b)) e(6);
	if (dup3(a, b, 0) != b) e(7);
	if (cloexec(b)) e(8);

	if (dup3(OPEN_MAX + 3, b, 0) != -1 || errno != EBADF) e(9);
	if (fcntl(b, F_GETFL) == -1) e(10);

	(void)close(a);
	(void)close(b);
	(void)unlink("t174.a");
}

/* The file fd2 referred to is closed: dup2() over the only write end of a
 * pipe gives the reader end-of-file.
 */
static void
test174c(void)
{
	int p[2], n;
	char c;

	subtest = 3;

	if (pipe(p) != 0) e(1);
	if ((n = open("/dev/null", O_RDONLY)) == -1) e(2);
	if (dup2(n, p[1]) != p[1]) e(3);
	if (read(p[0], &c, 1) != 0) e(4);

	(void)close(n);
	(void)close(p[0]);
	(void)close(p[1]);
}

/* Atomicity.  One thread keeps dup2()ing onto a target descriptor while
 * another keeps opening and closing files.  The target is open throughout,
 * so open() must never return it, and dup2() must always return it.  With
 * dup2() done as close() + fcntl(F_DUPFD), open() could take the target in
 * between, and dup2() would then return some other descriptor.
 */
#define ROUNDS	3000

static volatile int stop;
static int target, src[2];

static void *
opener(void *arg)
{
	int fd, *bad = arg;

	while (!stop) {
		if ((fd = open("/dev/null", O_RDONLY)) == -1)
			continue;
		if (fd == target)
			(*bad)++;
		(void)close(fd);
	}
	return NULL;
}

static void
test174d(void)
{
	pthread_t t;
	int i, bad = 0, wrong = 0;

	subtest = 4;

	if ((src[0] = open("/dev/null", O_RDONLY)) == -1) e(1);
	if ((src[1] = open("/dev/zero", O_RDONLY)) == -1) e(2);
	/* The lowest free descriptor: if dup2() ever left it free, the
	 * opener's open() would be handed it.
	 */
	if ((target = dup(src[1])) == -1) e(3);

	if (pthread_create(&t, NULL, opener, &bad) != 0) e(4);
	for (i = 0; i < ROUNDS; i++) {
		if (dup2(src[i & 1], target) != target)
			wrong++;
		if (fcntl(target, F_GETFL) == -1)
			wrong++;
	}
	stop = 1;
	if (pthread_join(t, NULL) != 0) e(5);

	if (wrong != 0) e(6);
	if (bad != 0) e(7);

	(void)close(target);
	(void)close(src[0]);
	(void)close(src[1]);
}

int
main(int argc, char **argv)
{

	start(174);

	test174a();
	test174b();
	test174c();
	test174d();

	quit();
	return 0;
}
