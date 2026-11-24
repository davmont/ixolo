/*
 * Upstream libqsbr stress test; a wrapper so its name does not clash.
 *
 * Upstream runs one thread per CPU: thread 0 writes, the others read.  On a
 * single CPU no reader would run at all, so run one more thread, as thmap's
 * stress test does.  sysconf() is used nowhere else in t_stress.c.
 */
#include <unistd.h>
#define	sysconf(name)	(sysconf(name) + 1)
#include "t_stress.c"
