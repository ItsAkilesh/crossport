/*
 * File: shim/errno.c
 * Purpose: errno storage on any thread, Win32-to-Linux error mapping and glibc's strerror messages
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <stdio.h>
#include <winsock2.h>

#include "libc_abi.h"
#include "tls.h"

static int g_fallback_errno;

int *shim_errno_ptr(void)
{
    uint8_t *thread_pointer = tls_thread_pointer();
    return thread_pointer != NULL ? (int *)(thread_pointer + TCB_ERRNO_OFFSET) : &g_fallback_errno;
}

typedef struct {
    DWORD win;
    int linux_errno;
} ErrorMapping;

static const ErrorMapping g_mappings[] = {
    {ERROR_FILE_NOT_FOUND, LINUX_ENOENT}, {ERROR_PATH_NOT_FOUND, LINUX_ENOENT},
    {ERROR_INVALID_DRIVE, LINUX_ENOENT}, {ERROR_BAD_PATHNAME, LINUX_ENOENT},
    {ERROR_ACCESS_DENIED, LINUX_EACCES}, {ERROR_SHARING_VIOLATION, LINUX_EACCES},
    {ERROR_LOCK_VIOLATION, LINUX_EACCES}, {ERROR_WRITE_PROTECT, LINUX_EROFS},
    {ERROR_INVALID_HANDLE, LINUX_EBADF}, {ERROR_NOT_ENOUGH_MEMORY, LINUX_ENOMEM},
    {ERROR_OUTOFMEMORY, LINUX_ENOMEM}, {ERROR_FILE_EXISTS, LINUX_EEXIST},
    {ERROR_ALREADY_EXISTS, LINUX_EEXIST}, {ERROR_DIRECTORY, LINUX_ENOTDIR},
    {ERROR_DIR_NOT_EMPTY, LINUX_ENOTEMPTY}, {ERROR_DISK_FULL, LINUX_ENOSPC},
    {ERROR_HANDLE_DISK_FULL, LINUX_ENOSPC}, {ERROR_BROKEN_PIPE, LINUX_EPIPE},
    {ERROR_NO_DATA, LINUX_EPIPE}, {ERROR_INVALID_PARAMETER, LINUX_EINVAL},
    {ERROR_NEGATIVE_SEEK, LINUX_EINVAL}, {ERROR_NOT_SAME_DEVICE, LINUX_EXDEV},
    {ERROR_TOO_MANY_OPEN_FILES, LINUX_EMFILE}, {ERROR_FILENAME_EXCED_RANGE, LINUX_ENAMETOOLONG},
    {ERROR_BUFFER_OVERFLOW, LINUX_ENAMETOOLONG}, {ERROR_NOT_SUPPORTED, LINUX_ENOSYS},
    {ERROR_CALL_NOT_IMPLEMENTED, LINUX_ENOSYS}, {ERROR_BUSY, LINUX_EBUSY},
    {WSAEINTR, LINUX_EINTR}, {WSAEBADF, LINUX_EBADF}, {WSAEACCES, LINUX_EACCES}, {WSAEFAULT, LINUX_EFAULT},
    {WSAEINVAL, LINUX_EINVAL}, {WSAEMFILE, LINUX_EMFILE}, {WSAEWOULDBLOCK, LINUX_EAGAIN},
    {WSAEINPROGRESS, LINUX_EINPROGRESS}, {WSAEALREADY, LINUX_EALREADY}, {WSAENOTSOCK, LINUX_ENOTSOCK},
    {WSAEMSGSIZE, LINUX_EMSGSIZE}, {WSAEPROTOTYPE, LINUX_EPROTOTYPE}, {WSAENOPROTOOPT, LINUX_ENOPROTOOPT},
    {WSAEPROTONOSUPPORT, LINUX_EPROTONOSUPPORT}, {WSAEOPNOTSUPP, LINUX_EOPNOTSUPP},
    {WSAEAFNOSUPPORT, LINUX_EAFNOSUPPORT}, {WSAEADDRINUSE, LINUX_EADDRINUSE},
    {WSAEADDRNOTAVAIL, LINUX_EADDRNOTAVAIL}, {WSAENETDOWN, LINUX_ENETDOWN}, {WSAENETUNREACH, LINUX_ENETUNREACH},
    {WSAECONNABORTED, LINUX_ECONNABORTED}, {WSAECONNRESET, LINUX_ECONNRESET}, {WSAENOBUFS, LINUX_ENOBUFS},
    {WSAEISCONN, LINUX_EISCONN}, {WSAENOTCONN, LINUX_ENOTCONN}, {WSAETIMEDOUT, LINUX_ETIMEDOUT},
    {WSAECONNREFUSED, LINUX_ECONNREFUSED}, {WSAEHOSTUNREACH, LINUX_EHOSTUNREACH},
};

int shim_errno_from_win(DWORD code)
{
    for (size_t i = 0; i < sizeof(g_mappings) / sizeof(g_mappings[0]); i++) {
        if (g_mappings[i].win == code) {
            return g_mappings[i].linux_errno;
        }
    }
    return LINUX_EIO;
}

int shim_fail_from_win(void)
{
    return shim_fail(shim_errno_from_win(GetLastError()));
}

typedef struct {
    int value;
    const char *message;
} ErrorMessage;

/* The exact strings glibc's strerror returns, which programs print and tests compare. */
static const ErrorMessage g_messages[] = {
    {0, "Success"}, {LINUX_EPERM, "Operation not permitted"}, {LINUX_ENOENT, "No such file or directory"},
    {LINUX_ESRCH, "No such process"}, {LINUX_EINTR, "Interrupted system call"},
    {LINUX_EIO, "Input/output error"}, {LINUX_ENXIO, "No such device or address"},
    {LINUX_E2BIG, "Argument list too long"}, {LINUX_ENOEXEC, "Exec format error"},
    {LINUX_EBADF, "Bad file descriptor"}, {LINUX_ECHILD, "No child processes"},
    {LINUX_EAGAIN, "Resource temporarily unavailable"}, {LINUX_ENOMEM, "Cannot allocate memory"},
    {LINUX_EACCES, "Permission denied"}, {LINUX_EFAULT, "Bad address"},
    {LINUX_EBUSY, "Device or resource busy"}, {LINUX_EEXIST, "File exists"},
    {LINUX_EXDEV, "Invalid cross-device link"}, {LINUX_ENODEV, "No such device"},
    {LINUX_ENOTDIR, "Not a directory"}, {LINUX_EISDIR, "Is a directory"},
    {LINUX_EINVAL, "Invalid argument"}, {LINUX_ENFILE, "Too many open files in system"},
    {LINUX_EMFILE, "Too many open files"}, {LINUX_ENOTTY, "Inappropriate ioctl for device"},
    {LINUX_EFBIG, "File too large"}, {LINUX_ENOSPC, "No space left on device"},
    {LINUX_ESPIPE, "Illegal seek"}, {LINUX_EROFS, "Read-only file system"},
    {LINUX_EMLINK, "Too many links"}, {LINUX_EPIPE, "Broken pipe"},
    {LINUX_EDOM, "Numerical argument out of domain"}, {LINUX_ERANGE, "Numerical result out of range"},
    {LINUX_EDEADLK, "Resource deadlock avoided"}, {LINUX_ENAMETOOLONG, "File name too long"},
    {LINUX_ENOSYS, "Function not implemented"}, {LINUX_ENOTEMPTY, "Directory not empty"},
    {LINUX_ELOOP, "Too many levels of symbolic links"},
    {LINUX_EOVERFLOW, "Value too large for defined data type"},
    {LINUX_EILSEQ, "Invalid or incomplete multibyte or wide character"},
    {LINUX_ENOTSOCK, "Socket operation on non-socket"}, {LINUX_EMSGSIZE, "Message too long"},
    {LINUX_EPROTOTYPE, "Protocol wrong type for socket"}, {LINUX_ENOPROTOOPT, "Protocol not available"},
    {LINUX_EPROTONOSUPPORT, "Protocol not supported"}, {LINUX_EOPNOTSUPP, "Operation not supported"},
    {LINUX_EAFNOSUPPORT, "Address family not supported by protocol"}, {LINUX_EADDRINUSE, "Address already in use"},
    {LINUX_EADDRNOTAVAIL, "Cannot assign requested address"}, {LINUX_ENETDOWN, "Network is down"},
    {LINUX_ENETUNREACH, "Network is unreachable"}, {LINUX_ECONNABORTED, "Software caused connection abort"},
    {LINUX_ECONNRESET, "Connection reset by peer"}, {LINUX_ENOBUFS, "No buffer space available"},
    {LINUX_EISCONN, "Transport endpoint is already connected"}, {LINUX_ENOTCONN, "Transport endpoint is not connected"},
    {LINUX_ETIMEDOUT, "Connection timed out"}, {LINUX_ECONNREFUSED, "Connection refused"},
    {LINUX_EHOSTUNREACH, "No route to host"}, {LINUX_EALREADY, "Operation already in progress"},
    {LINUX_EINPROGRESS, "Operation now in progress"},
};

const char *shim_strerror(int value, char *scratch, size_t scratch_size)
{
    for (size_t i = 0; i < sizeof(g_messages) / sizeof(g_messages[0]); i++) {
        if (g_messages[i].value == value) {
            return g_messages[i].message;
        }
    }
    snprintf(scratch, scratch_size, "Unknown error %d", value);
    return scratch;
}
