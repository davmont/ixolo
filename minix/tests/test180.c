/*
 * test180: malloc(3) from more threads than there are CPUs does not stall.
 *
 * jemalloc's locks on MINIX are spinlocks.  A thread that waits for one while
 * the holder cannot run -- the holder was preempted, or blocked in mmap(2)
 * with the lock held, and the waiter now has the CPU the holder needs -- must
 * give the CPU up instead of spinning until its quantum (200 ms) runs out.
 * The scheduler keeps user processes off the boot CPU when there are others,
 * so on two CPUs every thread of a program shares one, and a new process
 * whose threads start allocating stalled for a whole quantum (~220 ms) where
 * it takes a millisecond or so.
 *
 * The stall happens while the heap of a process is still growing, so each
 * round is a new process (this program again, with the argument "child"): it
 * starts ncpu + 1 threads, more than the user CPUs on any machine, that each
 * malloc and free a few blocks, and exits with SLOW if that took more than
 * SLOW_MS.  Only the threads are timed, not fork(2) and exec(2).  Before the
 * fix every round on two CPUs was slow; now none should be, and the test
 * allows a few for a loaded host.
 */
#include <sys/wait.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

#define ROUNDS		20
#define MAX_SLOW	(ROUNDS / 4)
#define ALLOCS		10
#define MAX_THREADS	17
#define SLOW_MS		100

#define FAST		0
#define SLOW		1
#define BROKEN		2

int max_error = 4;

static void *
worker(void *arg)
{
	void *p[ALLOCS];
	long i;

	for (i = 0; i < ALLOCS; i++) {
		if ((p[i] = malloc(64 + ((long) arg + i) * 48)) == NULL)
			return (void *) 1;
		*(volatile char *) p[i] = 1;
	}
	for (i = 0; i < ALLOCS; i++)
		free(p[i]);
	return NULL;
}

static int
child(void)
{
	pthread_t t[MAX_THREADS];
	struct timespec a, b;
	long ncpu, n, i, ms;
	void *ret;

	ncpu = sysconf(_SC_NPROCESSORS_ONLN);
	n = (ncpu < 1) ? 2 : ncpu + 1;
	if (n > MAX_THREADS)
		n = MAX_THREADS;

	if (clock_gettime(CLOCK_MONOTONIC, &a) != 0)
		return BROKEN;
	for (i = 0; i < n; i++)
		if (pthread_create(&t[i], NULL, worker, (void *) i) != 0)
			return BROKEN;
	for (i = 0; i < n; i++)
		if (pthread_join(t[i], &ret) != 0 || ret != NULL)
			return BROKEN;
	if (clock_gettime(CLOCK_MONOTONIC, &b) != 0)
		return BROKEN;

	ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
	return (ms > SLOW_MS) ? SLOW : FAST;
}

int
main(int argc, char **argv)
{
	char path[PATH_MAX];
	int r, status, slow;
	pid_t pid;

	if (argc == 2 && strcmp(argv[1], "child") == 0)
		return child();

	/* start() changes directory; exec ourselves by an absolute path. */
	if (argv[0][0] == '/')
		strlcpy(path, argv[0], sizeof(path));
	else if (getcwd(path, sizeof(path)) == NULL ||
	    strlcat(path, "/", sizeof(path)) >= sizeof(path) ||
	    strlcat(path, argv[0], sizeof(path)) >= sizeof(path))
		path[0] = '\0';

	start(180);
	if (path[0] == '\0') {
		e(1);
		quit();
	}

	slow = 0;
	for (r = 0; r < ROUNDS; r++) {
		if ((pid = fork()) == -1) {
			e(2);
			break;
		}
		if (pid == 0) {
			execl(path, path, "child", (char *) NULL);
			_exit(BROKEN);
		}
		if (waitpid(pid, &status, 0) != pid) {
			e(3);
			break;
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status) == BROKEN) {
			e(4);
			break;
		}
		if (WEXITSTATUS(status) == SLOW)
			slow++;
	}
	if (slow > MAX_SLOW) {
		printf("%d of %d rounds took over %d ms\n", slow, ROUNDS,
		    SLOW_MS);
		e(5);
	}

	quit();
	return 0;
}
