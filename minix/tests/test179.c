/* Test 179 - the number of CPUs.
 *
 * sysctl hw.ncpu, sysconf(_SC_NPROCESSORS_CONF), is the number of CPUs the
 * system has, as hw.ncpuonline, sysconf(_SC_NPROCESSORS_ONLN), is: without
 * CPU hot-plugging they are the same.  hw.ncpu said CONFIG_MAX_CPUS, the most
 * the kernel supports, instead: programs that size their thread pools with it
 * (and jemalloc, its arenas) took every machine for an 8-CPU one.
 *
 * kern.cp_time must agree with hw.ncpu as well: top(1) and sysctl(8) size the
 * kern.cp_time buffer from hw.ncpu, and per-CPU queries kern.cp_time.<cpu>
 * must accept 0..ncpu-1 and reject out-of-range CPUs.
 */
#include <sys/types.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

int
main(int argc, char **argv)
{
	int mib[3], ncpu = 0, online = 0, cpu;
	size_t len, one, want;
	long conf, onln;
	uint64_t ticks[CPUSTATES], *buf;

	start(179);

	subtest = 1;
	mib[0] = CTL_HW;
	mib[1] = HW_NCPU;
	len = sizeof(ncpu);
	if (sysctl(mib, 2, &ncpu, &len, NULL, 0) != 0) e(1);
	if (len != sizeof(ncpu)) e(2);
	mib[1] = HW_NCPUONLINE;
	len = sizeof(online);
	if (sysctl(mib, 2, &online, &len, NULL, 0) != 0) e(3);
	if (len != sizeof(online)) e(4);
	if (online < 1) e(5);
	if (ncpu != online) e(6);

	conf = sysconf(_SC_NPROCESSORS_CONF);
	onln = sysconf(_SC_NPROCESSORS_ONLN);
	if (onln != online) e(7);
	if (conf != ncpu) e(8);

	if (ncpu < 1)
		quit();

	subtest = 2;
	one = sizeof(ticks);
	want = (size_t)ncpu * one;
	mib[0] = CTL_KERN;
	mib[1] = KERN_CP_TIME;

	/* Aggregate size query and aggregate read. */
	len = 0;
	if (sysctl(mib, 2, NULL, &len, NULL, 0) != 0) e(1);
	if (len != one) e(2);
	len = one;
	if (sysctl(mib, 2, ticks, &len, NULL, 0) != 0) e(3);
	if (len != one) e(4);

	/*
	 * Per-CPU array read, both with exact size (top(1)) and with space for
	 * ncpu + 1 entries (sysctl -A, which also exercises the non-aggregate
	 * path when ncpu == 1).
	 */
	buf = calloc((size_t)(ncpu + 1), one);
	if (buf == NULL) {
		e(5);
		quit();
	}
	len = (size_t)(ncpu + 1) * one;
	if (sysctl(mib, 2, buf, &len, NULL, 0) != 0) e(6);
	if (len != want) e(7);
	len = want;
	if (sysctl(mib, 2, buf, &len, NULL, 0) != 0) e(8);
	if (len != want) e(9);
	free(buf);

	/* Individual per-CPU queries: 0..ncpu-1 succeed, ncpu and -1 fail. */
	for (cpu = 0; cpu < ncpu; cpu++) {
		mib[2] = cpu;
		len = one;
		if (sysctl(mib, 3, ticks, &len, NULL, 0) != 0) e(10);
		if (len != one) e(11);
	}
	mib[2] = ncpu;
	len = one;
	errno = 0;
	if (sysctl(mib, 3, ticks, &len, NULL, 0) != -1 || errno != EINVAL)
		e(12);
	mib[2] = -1;
	len = one;
	errno = 0;
	if (sysctl(mib, 3, ticks, &len, NULL, 0) != -1 || errno != EINVAL)
		e(13);

	quit();
	return 0;
}
