/*
 * File: shim/gnu_ext.c
 * Purpose: GNU C library extensions that stock GNU command-line tools call: error, rawmemchr, freopen, mbrtoc32, the stdio
 *          introspection calls (__freading, __fwriting, __fpurge) and sysinfo
 * Author: Akilesh
 * Date: 2026-11-17
 *
 * Found by translating Ubuntu's own coreutils: du, wc, cat, head, tr and xargs each failed only on these symbols. Every function
 * follows glibc's documented behaviour, and tests/corpus/gnu_ext compares the results with Linux.
 */

#include <string.h>

#include "libc_abi.h"
#include "startinfo.h"
#include "stdio_internal.h"

#define MILLISECONDS_PER_SECOND 1000u
#define SINGLE_PROCESS 1

/* Linux struct sysinfo on x86-64: 112 bytes, every field the width glibc gives it. */
typedef struct {
    c_long uptime;
    c_ulong loads[3];
    c_ulong totalram;
    c_ulong freeram;
    c_ulong sharedram;
    c_ulong bufferram;
    c_ulong totalswap;
    c_ulong freeswap;
    uint16_t procs;
    uint16_t pad;
    c_ulong totalhigh;
    c_ulong freehigh;
    uint32_t mem_unit;
} SysInfo;

_Static_assert(sizeof(SysInfo) == 112, "SysInfo must match the Linux layout");

extern char *crossport_data_program_invocation_name;

LIBC_FN ShimFile *sysv_freopen64(const char *path, const char *mode, ShimFile *file);
LIBC_FN size_t sysv_mbrtowc(int32_t *out, const char *text, size_t count, void *state);
LIBC_FN int sysv_fflush(ShimFile *file);
LIBC_FN int sysv_vfprintf(ShimFile *file, const char *format, __builtin_sysv_va_list ap);
LIBC_FN int sysv_fputs(const char *text, ShimFile *file);
LIBC_FN int sysv_fputc(int byte, ShimFile *file);
LIBC_FN char *sysv_strerror(int value);
LIBC_FN NORETURN void sysv_exit(int status);

LIBC_FN ShimFile *sysv_freopen(const char *path, const char *mode, ShimFile *file)
{
    return sysv_freopen64(path, mode, file);
}

/* char32_t is 32 bits and the locale is UTF-8, so a UTF-8 sequence converts exactly as for wchar_t. */
LIBC_FN size_t sysv_mbrtoc32(uint32_t *out, const char *text, size_t count, void *state)
{
    return sysv_mbrtowc((int32_t *)out, text, count, state);
}

/* memchr without a length: the caller guarantees the byte is there. */
LIBC_FN void *sysv_rawmemchr(const void *block, int byte)
{
    const unsigned char *at = block;
    while (*at != (unsigned char)byte) {
        at++;
    }
    return (void *)at;
}

LIBC_FN int sysv___freading(ShimFile *file)
{
    return file->state == STATE_READING || (file->is_readable && !file->is_writable);
}

LIBC_FN int sysv___fwriting(ShimFile *file)
{
    return file->state == STATE_WRITING || (file->is_writable && !file->is_readable);
}

/* Throws away whatever the stream has buffered, read or written, without writing it. */
LIBC_FN void sysv___fpurge(ShimFile *file)
{
    file_lock(file);
    file->read_ptr = file->read_end = file->read_base = NULL;
    file->write_base = file->write_ptr = file->write_end = NULL;
    file->state = STATE_IDLE;
    file_unlock(file);
}

/* error(3): "program: message", then ": reason" for a non-zero errnum, a newline, and exit when status is non-zero. */
LIBC_FN void sysv_error(int status, int errnum, const char *format, ...)
{
    ShimFile *stream = crossport_data_stderr;
    const char *name = *(char **)shim_data_home(&crossport_data_program_invocation_name);
    sysv_fflush(crossport_data_stdout);
    sysv_fputs(name != NULL ? name : "", stream);
    sysv_fputs(": ", stream);
    __builtin_sysv_va_list arguments;
    __builtin_sysv_va_start(arguments, format);
    sysv_vfprintf(stream, format, arguments);
    __builtin_sysv_va_end(arguments);
    if (errnum != 0) {
        sysv_fputs(": ", stream);
        sysv_fputs(sysv_strerror(errnum), stream);
    }
    sysv_fputc('\n', stream);
    sysv_fflush(stream);
    if (status != 0) {
        sysv_exit(status);
    }
}

LIBC_FN int sysv_sysinfo(SysInfo *info)
{
    MEMORYSTATUSEX memory = {.dwLength = sizeof(memory)};
    if (info == NULL) {
        shim_set_errno(LINUX_EFAULT);
        return -1;
    }
    GlobalMemoryStatusEx(&memory);
    *info = (SysInfo){0};
    info->uptime = (c_long)(GetTickCount64() / MILLISECONDS_PER_SECOND);
    info->totalram = memory.ullTotalPhys;
    info->freeram = memory.ullAvailPhys;
    info->totalswap = memory.ullTotalPageFile > memory.ullTotalPhys ? memory.ullTotalPageFile - memory.ullTotalPhys : 0;
    info->freeswap = memory.ullAvailPageFile > memory.ullAvailPhys ? memory.ullAvailPageFile - memory.ullAvailPhys : 0;
    info->procs = SINGLE_PROCESS;
    info->mem_unit = 1;
    return 0;
}
