/* Test 179 - the number of CPUs.
 *
 * sysctl hw.ncpu, sysconf(_SC_NPROCESSORS_CONF), is the number of CPUs the
 * system has, as hw.ncpuonline, sysconf(_SC_NPROCESSORS_ONLN), is: without
 * CPU hot-plugging they are the same.  hw.ncpu said CONFIG_MAX_CPUS, the most
 * the kernel supports, instead: programs that size their thread pools with it
 * (and jemalloc, its arenas) took every machine for an 8-CPU one.
 */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

int
main(int argc, char **argv)
{
	int mib[2], ncpu, online;
	size_t len;
	long conf, onln;

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
	if (online < 1) e(4);
	if (ncpu != online) e(5);

	conf = sysconf(_SC_NPROCESSORS_CONF);
	onln = sysconf(_SC_NPROCESSORS_ONLN);
	if (onln != online) e(6);
	if (conf != ncpu) e(7);

	quit();
	return 0;
}
