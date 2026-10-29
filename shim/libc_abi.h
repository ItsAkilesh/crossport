/*
 * File: shim/libc_abi.h
 * Purpose: Conventions every public C library function of the shim follows, plus Linux errno values and shared helpers
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Translated code calls the C library with the System V convention. The compiler, however, generates its own
 * calls (memcpy for struct copies, for example) with the Windows convention, and the Windows C runtime exports
 * the same names. A public function therefore must never share a symbol name with anything the shim calls
 * itself: it is defined as `sysv_<name>` and exported as `<name>` through a .def file that
 * tools/gen-shim-def.py generates by scanning for LIBC_FN and LIBC_DATA.
 */

#ifndef CROSSPORT_LIBC_ABI_H
#define CROSSPORT_LIBC_ABI_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "internal.h"

/* C `long` is 64 bits for translated code (LP64) but 32 bits for this compiler (LLP64). */
typedef int64_t c_long;
typedef uint64_t c_ulong;

/* Marks a public C library function. Write the definition on one line: `LIBC_FN int sysv_name(...)`. */
#define LIBC_FN SYSV

/* Declares a public data symbol (for example stdout). It is exported as `name`. */
#define LIBC_DATA(type, name) type crossport_data_##name

/* Linux x86-64 errno values. glibc and the translated programs agree on these numbers. */
enum {
    LINUX_EPERM = 1, LINUX_ENOENT = 2, LINUX_ESRCH = 3, LINUX_EINTR = 4, LINUX_EIO = 5, LINUX_ENXIO = 6,
    LINUX_E2BIG = 7, LINUX_ENOEXEC = 8, LINUX_EBADF = 9, LINUX_ECHILD = 10, LINUX_EAGAIN = 11,
    LINUX_ENOMEM = 12, LINUX_EACCES = 13, LINUX_EFAULT = 14, LINUX_EBUSY = 16, LINUX_EEXIST = 17,
    LINUX_EXDEV = 18, LINUX_ENODEV = 19, LINUX_ENOTDIR = 20, LINUX_EISDIR = 21, LINUX_EINVAL = 22,
    LINUX_ENFILE = 23, LINUX_EMFILE = 24, LINUX_ENOTTY = 25, LINUX_EFBIG = 27, LINUX_ENOSPC = 28,
    LINUX_ESPIPE = 29, LINUX_EROFS = 30, LINUX_EMLINK = 31, LINUX_EPIPE = 32, LINUX_EDOM = 33,
    LINUX_ERANGE = 34, LINUX_EDEADLK = 35, LINUX_ENAMETOOLONG = 36, LINUX_ENOSYS = 38,
    LINUX_ENOTEMPTY = 39, LINUX_ELOOP = 40, LINUX_EOVERFLOW = 75, LINUX_EILSEQ = 84,
    LINUX_ENOTSOCK = 88, LINUX_EMSGSIZE = 90, LINUX_EPROTOTYPE = 91, LINUX_ENOPROTOOPT = 92,
    LINUX_EPROTONOSUPPORT = 93, LINUX_EOPNOTSUPP = 95, LINUX_EAFNOSUPPORT = 97, LINUX_EADDRINUSE = 98,
    LINUX_EADDRNOTAVAIL = 99, LINUX_ENETDOWN = 100, LINUX_ENETUNREACH = 101, LINUX_ECONNABORTED = 103,
    LINUX_ECONNRESET = 104, LINUX_ENOBUFS = 105, LINUX_EISCONN = 106, LINUX_ENOTCONN = 107,
    LINUX_ETIMEDOUT = 110, LINUX_ECONNREFUSED = 111, LINUX_EHOSTUNREACH = 113, LINUX_EALREADY = 114,
    LINUX_EINPROGRESS = 115
};

/* The calling thread's errno, usable on any thread. */
int *shim_errno_ptr(void);

static inline void shim_set_errno(int value)
{
    *shim_errno_ptr() = value;
}

/* Linux errno for a Win32 error code. */
int shim_errno_from_win(DWORD code);

/* Sets errno from GetLastError() and returns -1, the common failure return of POSIX calls. */
int shim_fail_from_win(void);

/* Sets errno and returns -1. */
static inline int shim_fail(int value)
{
    shim_set_errno(value);
    return -1;
}

/* The message glibc gives for an errno value. Unknown values get "Unknown error N" in `scratch`. */
const char *shim_strerror(int value, char *scratch, size_t scratch_size);

/* Runs registered exit handlers, flushes stdio, runs destructors and ends the process. Never returns. */
NORETURN void shim_exit(int status);

/* Linux paths to Windows paths; see path.c. Returns 0, or -1 with errno set. */
int shim_path_to_windows(const char *linux_path, char *out, size_t out_size);

#endif
