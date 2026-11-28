/*
 * Included before every source file of this library (-include).
 *
 * NPF's standalone layer (stand/npf_stand.h) maps kernel types onto POSIX
 * ones with macros, e.g. kmutex_t to pthread_mutex_t.  MINIX's <net/if.h>
 * pulls in NetBSD's <sys/mutex.h>, whose own kmutex_t typedef would then
 * clash; include it first, before the macros exist.
 */
#include <sys/types.h>
#include <sys/mutex.h>
