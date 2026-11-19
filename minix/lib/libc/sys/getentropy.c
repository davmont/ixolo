#include <sys/cdefs.h>
#include "namespace.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

/*
 * The system calls themselves, not open(2)/read(2)/close(2): those are
 * cancellation points, which libpthread replaces with stubs that read the
 * thread pointer, and getentropy(3) is not one.  It also runs before there is
 * a thread pointer: _libc_init() takes the stack protector's guard from it
 * before it sets up TLS, and the stubs then read through a null %fs base, so
 * that every statically linked program with libpthread died on start-up.
 */
int _sys_open(const char *, int, ...);
ssize_t _sys_read(int, void *, size_t);
int _sys_close(int);

/*
 * Fill 'buf' with 'len' (at most GETENTROPY_MAX) bytes of entropy, from the
 * random driver, as arc4random(3) does here (MINIX has no KERN_ARND).
 */
int
getentropy(void *buf, size_t len)
{
	char *p = buf;
	ssize_t r;
	int fd, err;

	if (len > GETENTROPY_MAX) {
		errno = EINVAL;
		return -1;
	}

	if ((fd = _sys_open("/dev/urandom", O_RDONLY | O_CLOEXEC)) < 0) {
		errno = EIO;
		return -1;
	}

	while (len > 0) {
		if ((r = _sys_read(fd, p, len)) <= 0) {
			if (r < 0 && errno == EINTR)
				continue;
			err = (r < 0) ? errno : EIO;
			(void)_sys_close(fd);
			errno = (err == EFAULT) ? EFAULT : EIO;
			return -1;
		}
		p += r;
		len -= r;
	}

	(void)_sys_close(fd);
	return 0;
}
