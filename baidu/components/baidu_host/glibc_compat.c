/* Compatibility shims for glibc symbols that don't exist in xenial (2.23).
 *
 * Ubuntu 22.04 headers redirect standard POSIX calls to their 64-bit
 * variants (fcntl64, stat64, ...) when _FILE_OFFSET_BITS=64 is defined.
 * The target device has glibc 2.23, which pre-dates the separate versioned
 * 64-bit symbols added in glibc 2.28/2.33.
 *
 * We implement missing symbols via direct syscalls so the stub itself
 * doesn't get caught in the same header-level redirect loop.
 */

#include <sys/syscall.h>
#include <unistd.h>
#include <stdarg.h>
/* ARM 32-bit syscall numbers (stable Linux ABI) */
#ifndef __NR_fcntl64
#define __NR_fcntl64  221
#endif
#ifndef __NR_stat64
#define __NR_stat64   195
#endif
#ifndef __NR_lstat64
#define __NR_lstat64  196
#endif
#ifndef __NR_fstat64
#define __NR_fstat64  197
#endif

/* fcntl64: added as a separate versioned symbol in glibc 2.28.
 * On ARM, the kernel fcntl64 syscall handles 64-bit file offsets.
 * The Ubuntu 22.04 headers #define fcntl → fcntl64 when
 * _FILE_OFFSET_BITS=64, which we use to compile the rest of the project. */
int fcntl64(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    long arg = va_arg(ap, long);
    va_end(ap);
    return (int)syscall(__NR_fcntl64, (long)fd, (long)cmd, arg);
}

/* stat64 / lstat64 / fstat64: added as versioned symbols in glibc 2.33.
 * Pass through to kernel; the kernel struct layout matches glibc's stat64. */
int stat64(const char *path, void *buf) {
    return (int)syscall(__NR_stat64, path, buf);
}

int lstat64(const char *path, void *buf) {
    return (int)syscall(__NR_lstat64, path, buf);
}

int fstat64(int fd, void *buf) {
    return (int)syscall(__NR_fstat64, (long)fd, buf);
}

/* __libc_single_threaded: glibc 2.32 variable used by GCC 11's static
 * libstdc++ to skip mutex locks in single-threaded programs.
 * 0 = never single-threaded → locks are always taken (safe default). */
char __libc_single_threaded = 0;

/* pthread_cond_clockwait was added in glibc 2.30; GCC 11's libstdc++ uses it
 * for std::condition_variable::wait_for/wait_until on steady_clock. Convert
 * the deadline to CLOCK_REALTIME and use pthread_cond_timedwait. */
#include <errno.h>
#include <pthread.h>
#include <time.h>

int pthread_cond_clockwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           clockid_t clockid, const struct timespec *abstime)
{
    struct timespec now, rt;
    long long ns;

    if (clockid == CLOCK_REALTIME)
        return pthread_cond_timedwait(cond, mutex, abstime);
    if (clockid != CLOCK_MONOTONIC)
        return EINVAL;
    clock_gettime(CLOCK_MONOTONIC, &now);
    ns = (long long)(abstime->tv_sec - now.tv_sec) * 1000000000LL +
         (abstime->tv_nsec - now.tv_nsec);
    if (ns < 0)
        ns = 0;
    clock_gettime(CLOCK_REALTIME, &rt);
    ns += rt.tv_nsec;
    rt.tv_sec += ns / 1000000000LL;
    rt.tv_nsec = ns % 1000000000LL;
    return pthread_cond_timedwait(cond, mutex, &rt);
}
