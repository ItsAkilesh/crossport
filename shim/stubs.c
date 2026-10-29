/*
 * File: shim/stubs.c
 * Purpose: Linux entry points with no Windows counterpart, answered honestly: ENOSYS, "nothing there", or a harmless success
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Programs name these functions even when they never need them, or they ask and fall back when told no. A
 * missing symbol would stop the translation, so each is present. The groups:
 *   STUB_FAIL: returns -1 with errno ENOSYS (the feature does not exist here),
 *   STUB_NULL: returns NULL with errno ENOSYS,
 *   STUB_ZERO: returns 0, for requests that are safe to accept and ignore (hints, permissions, tuning).
 */

#include "libc_abi.h"

/* Every symbol the translator was told to stub (crossport translate --stub-missing) is bound to this: it
 * ignores its arguments and returns 0, which reads as NULL, "no", or an empty result. */
LIBC_FN int64_t sysv_crossport_unavailable(void)
{
    return 0;
}

#define STUB_FAIL(name) \
    LIBC_FN int64_t sysv_##name(void) \
    { \
        return shim_fail(LINUX_ENOSYS); \
    }

#define STUB_NULL(name) \
    LIBC_FN void *sysv_##name(void) \
    { \
        shim_set_errno(LINUX_ENOSYS); \
        return NULL; \
    }

#define STUB_ZERO(name) \
    LIBC_FN int64_t sysv_##name(void) \
    { \
        return 0; \
    }

/* File notification, mounts, namespaces, capabilities, SysV IPC and the other kernel interfaces. */
STUB_FAIL(inotify_add_watch)
STUB_FAIL(inotify_init)
STUB_FAIL(inotify_init1)
STUB_FAIL(inotify_rm_watch)
STUB_FAIL(mount)
STUB_FAIL(mount_setattr)
STUB_FAIL(move_mount)
STUB_FAIL(open_tree)
STUB_FAIL(umount)
STUB_FAIL(umount2)
STUB_FAIL(memfd_create)
STUB_FAIL(prctl)
STUB_FAIL(capget)
STUB_FAIL(capset)
STUB_FAIL(chroot)
STUB_FAIL(clone)
STUB_FAIL(close_range)
STUB_FAIL(copy_file_range)
STUB_FAIL(semctl)
STUB_FAIL(semget)
STUB_FAIL(semop)
STUB_FAIL(ftok)
STUB_FAIL(mq_getattr)
STUB_FAIL(name_to_handle_at)
STUB_FAIL(renameat2)
STUB_FAIL(epoll_pwait2)
STUB_FAIL(fts64_open)
STUB_FAIL(fts64_read)
STUB_FAIL(fts64_close)
STUB_FAIL(fts64_set)
STUB_FAIL(makecontext)
STUB_FAIL(getcontext)
STUB_FAIL(swapcontext)
STUB_FAIL(setcontext)
STUB_FAIL(recvmmsg)
STUB_FAIL(sendmmsg)
STUB_FAIL(pthread_cancel)
STUB_FAIL(sched_getaffinity)
STUB_FAIL(setgid)
STUB_FAIL(setgroups)
STUB_FAIL(parse_printf_format)
STUB_FAIL(regcomp)
STUB_FAIL(regexec)
STUB_ZERO(regfree)
STUB_FAIL(setns)
STUB_FAIL(unshare)
STUB_FAIL(shmat)
STUB_FAIL(shmctl)
STUB_FAIL(shmdt)
STUB_FAIL(shmget)
STUB_FAIL(splice)
STUB_FAIL(signalfd)
STUB_FAIL(statx)
STUB_FAIL(waitid)
STUB_FAIL(initstate_r)
STUB_FAIL(random_r)

/* Extended attributes. */
STUB_FAIL(fgetxattr)
STUB_FAIL(flistxattr)
STUB_FAIL(fremovexattr)
STUB_FAIL(fsetxattr)
STUB_FAIL(getxattr)
STUB_FAIL(lgetxattr)
STUB_FAIL(listxattr)
STUB_FAIL(llistxattr)
STUB_FAIL(lremovexattr)
STUB_FAIL(lsetxattr)
STUB_FAIL(removexattr)
STUB_FAIL(setxattr)

/* The DNS resolver library: name lookups go through getaddrinfo, so the low-level resolver is absent. */
STUB_FAIL(res_query)
STUB_FAIL(res_nquery)
STUB_FAIL(res_nsearch)
STUB_FAIL(res_ninit)
STUB_FAIL(__res_ninit)
STUB_FAIL(__res_nclose)
STUB_FAIL(res_nclose)
STUB_FAIL(dn_expand)
STUB_FAIL(ns_initparse)
STUB_FAIL(ns_name_uncompress)
STUB_FAIL(ns_parserr)

/* Interface lists and the mount table. */
STUB_FAIL(getifaddrs)
STUB_NULL(getmntent)
STUB_NULL(getmntent_r)
STUB_NULL(setmntent)
STUB_NULL(fmemopen)
STUB_NULL(open_memstream)
STUB_NULL(if_indextoname)
STUB_ZERO(if_nametoindex)
STUB_ZERO(freeifaddrs)
STUB_ZERO(endmntent)
STUB_ZERO(hasmntopt)

/* Requests that are safe to accept and ignore. */
STUB_ZERO(posix_fadvise)
STUB_ZERO(posix_madvise)
STUB_ZERO(flock)
STUB_ZERO(lchown)
STUB_ZERO(lchmod)
STUB_ZERO(chown)
STUB_ZERO(fchownat)
STUB_ZERO(pthread_setcancelstate)
STUB_ZERO(pthread_testcancel)
STUB_ZERO(pthread_setconcurrency)
STUB_ZERO(pthread_getconcurrency)
STUB_ZERO(pthread_sigmask)
STUB_ZERO(pthread_setname_np)
STUB_ZERO(pthread_setaffinity_np)
STUB_ZERO(pthread_getaffinity_np)
STUB_ZERO(pthread_setschedparam)
STUB_ZERO(pthread_getschedparam)
STUB_ZERO(pthread_attr_setaffinity_np)
STUB_ZERO(pthread_attr_setguardsize)
STUB_ZERO(pthread_attr_setinheritsched)
STUB_ZERO(pthread_attr_setschedparam)
STUB_ZERO(pthread_attr_setschedpolicy)
STUB_ZERO(pthread_rwlockattr_init)
STUB_ZERO(pthread_rwlockattr_destroy)
STUB_ZERO(pthread_rwlockattr_setkind_np)
STUB_ZERO(sched_get_priority_max)
STUB_ZERO(sched_get_priority_min)
STUB_ZERO(openlog)
STUB_ZERO(closelog)
STUB_ZERO(setlogmask)
STUB_ZERO(syslog)
STUB_ZERO(__syslog_chk)
STUB_ZERO(vsyslog)
STUB_ZERO(__vsyslog_chk)
STUB_ZERO(endpwent)
STUB_ZERO(endservent)
STUB_ZERO(__register_atfork)
STUB_ZERO(backtrace)
STUB_ZERO(setrlimit)
STUB_ZERO(setrlimit64)
