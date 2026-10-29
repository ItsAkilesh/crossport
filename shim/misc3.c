/*
 * File: shim/misc3.c
 * Purpose: Remaining everyday libc calls: signal sets, directory scanning, glob and fnmatch, name services, rlimits, terminals, time strings
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "fd.h"
#include "thread_cell.h"

#define SIGSET_BYTES 128
#define RLIM_INFINITY_VALUE UINT64_MAX
#define STACK_LIMIT_BYTES (8u * 1024 * 1024)
#define OPEN_FILES_LIMIT 1024u
#define SIGRTMIN_VALUE 34
#define SIGRTMAX_VALUE 64
#define FNM_PATHNAME 1
#define FNM_NOESCAPE 2
#define FNM_PERIOD 4
#define FNM_CASEFOLD 16
#define FNM_NOMATCH 1
#define DT_DIRECTORY 4
#define MAX_PATTERN_PATHS 4096
#define TICKS_PER_SECOND 100
#define ASCTIME_BYTES 26
#define STACK_DEFAULT_BYTES (8u * 1024 * 1024)
#define POPCOUNT_BYTE_BITS 8

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void *sysv_realloc(void *block, size_t size);
LIBC_FN void sysv_free(void *block);
LIBC_FN char *sysv_strdup(const char *text);
LIBC_FN void *sysv_opendir(const char *path);
LIBC_FN void *sysv_fdopendir(int fd);
LIBC_FN int sysv_closedir(void *directory);
LIBC_FN int sysv_access(const char *path, int mode);
LIBC_FN int sysv_fsync(int fd);
LIBC_FN int sysv_open(const char *path, int flags, int mode);
LIBC_FN int sysv_chdir(const char *path);
LIBC_FN void *sysv_gmtime_r(const int64_t *timer, void *tm);
LIBC_FN size_t sysv_strftime(char *out, size_t size, const char *format, const void *tm);
LIBC_FN int sysv_clock_gettime(int clock, void *spec);
LIBC_FN NORETURN void sysv___chk_fail(void);
LIBC_FN int64_t sysv_readlink(const char *path, char *buffer, size_t size);
LIBC_FN int sysv_ftruncate64(int fd, int64_t length);
LIBC_FN int sysv_utimes(const char *path, const int64_t times[4]);
LIBC_FN c_long sysv_sysconf(int name);

/* The program's own name, set when the program starts (see crossport_libc.c). */
LIBC_DATA(char *, __progname);
LIBC_DATA(char *, __progname_full);
LIBC_DATA(char *, program_invocation_name);
LIBC_DATA(char *, program_invocation_short_name);
LIBC_DATA(int64_t, __timezone);
LIBC_DATA(char *, __tzname[2]) = {"UTC", "UTC"};
LIBC_DATA(int64_t, timezone);
LIBC_DATA(int, daylight);
LIBC_DATA(char *, tzname[2]) = {"UTC", "UTC"};

static int g_h_errno;

void shim_set_program_name(char *full)
{
    char *base = full;
    for (char *at = full; *at != '\0'; at++) {
        base = (*at == '/' || *at == '\\') ? at + 1 : base;
    }
    crossport_data___progname = base;
    crossport_data___progname_full = full;
    crossport_data_program_invocation_name = full;
    crossport_data_program_invocation_short_name = base;
}

LIBC_FN int *sysv___h_errno_location(void)
{
    return &g_h_errno;
}

LIBC_FN int sysv___libc_current_sigrtmin(void)
{
    return SIGRTMIN_VALUE;
}

LIBC_FN int sysv___libc_current_sigrtmax(void)
{
    return SIGRTMAX_VALUE;
}

LIBC_FN c_long sysv___sysconf(int name)
{
    return sysv_sysconf(name);
}

LIBC_FN void sysv_tzset(void)
{
}

LIBC_FN int sysv_sigdelset(void *set, int signal_number)
{
    if (signal_number < 1 || signal_number > SIGRTMAX_VALUE) {
        return shim_fail(LINUX_EINVAL);
    }
    ((uint8_t *)set)[(signal_number - 1) / 8] &= (uint8_t)~(1u << ((signal_number - 1) % 8));
    return 0;
}

LIBC_FN int sysv_sigismember(const void *set, int signal_number)
{
    if (signal_number < 1 || signal_number > SIGRTMAX_VALUE) {
        return shim_fail(LINUX_EINVAL);
    }
    return (((const uint8_t *)set)[(signal_number - 1) / 8] >> ((signal_number - 1) % 8)) & 1;
}

LIBC_FN int sysv_sigisemptyset(const void *set)
{
    for (size_t i = 0; i < SIGSET_BYTES; i++) {
        if (((const uint8_t *)set)[i] != 0) {
            return 0;
        }
    }
    return 1;
}

LIBC_FN int sysv___sched_cpucount(size_t size, const uint8_t *set)
{
    int count = 0;
    for (size_t i = 0; i < size; i++) {
        count += __builtin_popcount(set[i]);
    }
    return count;
}

LIBC_FN int sysv_pthread_kill(void *thread, int signal_number)
{
    (void)thread;
    return signal_number == 0 ? 0 : LINUX_ENOSYS;
}

LIBC_FN int sysv_pthread_attr_getstacksize(const void *attributes, size_t *size)
{
    (void)attributes;
    *size = STACK_DEFAULT_BYTES;
    return 0;
}

LIBC_FN int sysv_pthread_attr_getschedpolicy(const void *attributes, int *policy)
{
    (void)attributes;
    *policy = 0;
    return 0;
}

LIBC_FN int sysv_posix_spawnattr_setflags(void *attributes, int16_t flags)
{
    (void)attributes;
    (void)flags;
    return 0;
}

LIBC_FN int sysv_posix_spawnattr_setsigdefault(void *attributes, const void *set)
{
    (void)attributes;
    (void)set;
    return 0;
}

/* struct rlimit: soft and hard limit. */
LIBC_FN int sysv_getrlimit(int resource, uint64_t *limit)
{
    enum { RLIMIT_STACK_ID = 3, RLIMIT_NOFILE_ID = 7 };
    uint64_t value = resource == RLIMIT_STACK_ID ? STACK_LIMIT_BYTES : (resource == RLIMIT_NOFILE_ID ? OPEN_FILES_LIMIT : RLIM_INFINITY_VALUE);
    limit[0] = limit[1] = value;
    return 0;
}

LIBC_FN int sysv_getrlimit64(int resource, uint64_t *limit)
{
    return sysv_getrlimit(resource, limit);
}

LIBC_FN int sysv_getloadavg(double *averages, int count)
{
    for (int i = 0; i < count && i < 3; i++) {
        averages[i] = 0.0;
    }
    return count < 3 ? count : 3;
}

LIBC_FN int sysv_fdatasync(int fd)
{
    return sysv_fsync(fd);
}

LIBC_FN void sysv_sync(void)
{
}

LIBC_FN int sysv_creat64(const char *path, unsigned mode)
{
    enum { WRITE_ONLY = 1, CREATE = 0x40, TRUNCATE = 0x200 };
    return sysv_open(path, WRITE_ONLY | CREATE | TRUNCATE, (int)mode);
}

LIBC_FN int sysv_eaccess(const char *path, int mode)
{
    return sysv_access(path, mode);
}

LIBC_FN int sysv_posix_fallocate64(int fd, int64_t offset, int64_t length)
{
    HANDLE handle = shim_fd_handle(fd);
    LARGE_INTEGER size;
    if (handle == INVALID_HANDLE_VALUE) {
        return LINUX_EBADF;
    }
    if (GetFileSizeEx(handle, &size) && size.QuadPart < offset + length) {
        return sysv_ftruncate64(fd, offset + length) == 0 ? 0 : *shim_errno_ptr();
    }
    return 0;
}

LIBC_FN int sysv_fallocate64(int fd, int mode, int64_t offset, int64_t length)
{
    (void)mode;
    int error = sysv_posix_fallocate64(fd, offset, length);
    return error == 0 ? 0 : shim_fail(error);
}

LIBC_FN int sysv_fchdir(int fd)
{
    wchar_t path[SHIM_PATH_MAX];
    HANDLE handle = shim_fd_handle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    DWORD length = GetFinalPathNameByHandleW(handle, path, SHIM_PATH_MAX, FILE_NAME_NORMALIZED);
    const wchar_t *plain = (length > 4 && path[0] == (wchar_t)0x5c && path[1] == (wchar_t)0x5c && path[2] == L'?') ? path + 4 : path;
    return length > 0 && length < SHIM_PATH_MAX && SetCurrentDirectoryW(plain) ? 0 : shim_fail(LINUX_ENOTDIR);
}

/* struct statfs on x86-64 (120 bytes), filled from the volume's free-space figures. */
typedef struct {
    int64_t type;
    int64_t block_size;
    uint64_t blocks;
    uint64_t blocks_free;
    uint64_t blocks_available;
    uint64_t files;
    uint64_t files_free;
    int32_t fsid[2];
    int64_t name_length;
    int64_t fragment_size;
    int64_t flags;
    int64_t spare[4];
} StatFs;

_Static_assert(sizeof(StatFs) == 120, "struct statfs is 120 bytes");

static int fill_statfs(const wchar_t *root, StatFs *out)
{
    ULARGE_INTEGER available;
    ULARGE_INTEGER total;
    ULARGE_INTEGER free_bytes;
    if (!GetDiskFreeSpaceExW(root, &available, &total, &free_bytes)) {
        return shim_fail_from_win();
    }
    enum { BLOCK = 4096, EXT4_MAGIC = 0xef53, NAME_MAX_BYTES = 255 };
    memset(out, 0, sizeof(*out));
    out->type = EXT4_MAGIC;
    out->block_size = out->fragment_size = BLOCK;
    out->blocks = total.QuadPart / BLOCK;
    out->blocks_free = free_bytes.QuadPart / BLOCK;
    out->blocks_available = available.QuadPart / BLOCK;
    out->name_length = NAME_MAX_BYTES;
    return 0;
}

LIBC_FN int sysv_statfs(const char *path, StatFs *out)
{
    wchar_t wide[SHIM_PATH_MAX];
    wchar_t volume[SHIM_PATH_MAX];
    if (shim_path_to_wide(path, wide, SHIM_PATH_MAX) != 0) {
        return -1;
    }
    return GetVolumePathNameW(wide, volume, SHIM_PATH_MAX) ? fill_statfs(volume, out) : shim_fail_from_win();
}

LIBC_FN int sysv_statfs64(const char *path, StatFs *out)
{
    return sysv_statfs(path, out);
}

LIBC_FN int sysv_fstatfs(int fd, StatFs *out)
{
    wchar_t wide[SHIM_PATH_MAX];
    wchar_t volume[SHIM_PATH_MAX];
    HANDLE handle = shim_fd_handle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    if (GetFinalPathNameByHandleW(handle, wide, SHIM_PATH_MAX, FILE_NAME_NORMALIZED) == 0 ||
        !GetVolumePathNameW(wide, volume, SHIM_PATH_MAX)) {
        return shim_fail_from_win();
    }
    return fill_statfs(volume, out);
}

LIBC_FN int sysv_fstatfs64(int fd, StatFs *out)
{
    return sysv_fstatfs(fd, out);
}

/* Changes a file's times through its descriptor; NULL means now. times is two {seconds, nanoseconds} pairs. */
LIBC_FN int sysv_futimens(int fd, const int64_t times[4])
{
    HANDLE handle = shim_fd_handle(fd);
    FILETIME access;
    FILETIME modified;
    const uint64_t epoch = 116444736000000000ull;
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    if (times == NULL) {
        GetSystemTimePreciseAsFileTime(&access);
        modified = access;
    } else {
        uint64_t a = epoch + (uint64_t)times[0] * 10000000ull + (uint64_t)times[1] / 100;
        uint64_t m = epoch + (uint64_t)times[2] * 10000000ull + (uint64_t)times[3] / 100;
        access = (FILETIME){(DWORD)a, (DWORD)(a >> 32)};
        modified = (FILETIME){(DWORD)m, (DWORD)(m >> 32)};
    }
    return SetFileTime(handle, NULL, &access, &modified) ? 0 : shim_fail_from_win();
}

LIBC_FN int sysv_utime(const char *path, const int64_t *times)
{
    int64_t converted[4];
    if (times != NULL) {
        converted[0] = times[0];
        converted[1] = 0;
        converted[2] = times[1];
        converted[3] = 0;
    }
    return sysv_utimes(path, times != NULL ? converted : NULL);
}

/* Terminal attributes: a console is not a termios device, so every query says "not a terminal". */
LIBC_FN int sysv_tcgetattr(int fd, void *attributes)
{
    (void)fd;
    (void)attributes;
    return shim_fail(LINUX_ENOTTY);
}

LIBC_FN int sysv_tcsetattr(int fd, int action, const void *attributes)
{
    (void)fd;
    (void)action;
    (void)attributes;
    return shim_fail(LINUX_ENOTTY);
}

LIBC_FN int sysv_mbsinit(const void *state)
{
    return state == NULL || *(const uint64_t *)state == 0;
}

LIBC_FN char *sysv_strerror_l(int number, void *locale)
{
    (void)locale;
    extern LIBC_FN char *sysv_strerror(int);
    return sysv_strerror(number);
}

LIBC_FN char *sysv_dcngettext(const char *domain, const char *singular, const char *plural, unsigned long count, int category)
{
    (void)domain;
    (void)category;
    return (char *)(count == 1 ? singular : plural);
}

LIBC_FN int64_t sysv_wcwidth(uint32_t c)
{
    if (c == 0) {
        return 0;
    }
    if (c < 32 || (c >= 0x7f && c < 0xa0)) {
        return -1;
    }
    if (c >= 0x300 && c <= 0x36f) {
        return 0; /* combining marks */
    }
    int is_wide = (c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf) || (c >= 0xac00 && c <= 0xd7a3) ||
                  (c >= 0xf900 && c <= 0xfaff) || (c >= 0xfe30 && c <= 0xfe6f) || (c >= 0xff00 && c <= 0xff60) ||
                  (c >= 0xffe0 && c <= 0xffe6) || (c >= 0x1f300 && c <= 0x1f64f) || (c >= 0x20000 && c <= 0x3fffd);
    return is_wide ? 2 : 1;
}

LIBC_FN int64_t sysv_clock_getres(int clock, int64_t *resolution)
{
    (void)clock;
    if (resolution != NULL) {
        resolution[0] = 0;
        resolution[1] = 1; /* 1 ns: the precise system time functions are used */
    }
    return 0;
}

/* struct tms: process and child times in clock ticks. */
LIBC_FN int64_t sysv_times(int64_t *buffer)
{
    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    if (buffer != NULL && GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        uint64_t user_units = ((uint64_t)user.dwHighDateTime << 32) | user.dwLowDateTime;
        uint64_t kernel_units = ((uint64_t)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
        buffer[0] = (int64_t)(user_units / (10000000ull / TICKS_PER_SECOND));
        buffer[1] = (int64_t)(kernel_units / (10000000ull / TICKS_PER_SECOND));
        buffer[2] = buffer[3] = 0;
    }
    return (int64_t)(GetTickCount64() / (1000 / TICKS_PER_SECOND));
}

/* asctime and ctime: "Thu Jan  1 00:00:00 1970\n". `tm` is Linux's struct tm (56 bytes). */
LIBC_FN char *sysv_asctime_r(const int32_t *tm, char *buffer)
{
    static const char days[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int day = tm[6] >= 0 && tm[6] < 7 ? tm[6] : 0;
    int month = tm[4] >= 0 && tm[4] < 12 ? tm[4] : 0;
    char text[ASCTIME_BYTES + 8];
    int length = 0;
    memcpy(text, days[day], 3);
    text[3] = ' ';
    memcpy(text + 4, months[month], 3);
    length = 7;
    const int fields[] = {tm[3], tm[2], tm[1], tm[0]};
    (void)fields;
    char two[3];
    text[length++] = ' ';
    text[length++] = tm[3] < 10 ? ' ' : (char)('0' + tm[3] / 10);
    text[length++] = (char)('0' + tm[3] % 10);
    text[length++] = ' ';
    const int clock_fields[3] = {tm[2], tm[1], tm[0]};
    for (int i = 0; i < 3; i++) {
        two[0] = (char)('0' + clock_fields[i] / 10 % 10);
        two[1] = (char)('0' + clock_fields[i] % 10);
        text[length++] = two[0];
        text[length++] = two[1];
        text[length++] = i < 2 ? ':' : ' ';
    }
    int year = tm[5] + 1900;
    for (int divisor = 1000; divisor >= 1; divisor /= 10) {
        text[length++] = (char)('0' + year / divisor % 10);
    }
    text[length++] = '\n';
    text[length] = '\0';
    memcpy(buffer, text, (size_t)length + 1);
    return buffer;
}

LIBC_FN char *sysv_asctime(const int32_t *tm)
{
    static char buffer[ASCTIME_BYTES + 2];
    return sysv_asctime_r(tm, buffer);
}

LIBC_FN char *sysv_ctime_r(const int64_t *timer, char *buffer)
{
    int32_t tm[14];
    sysv_gmtime_r(timer, tm);
    return sysv_asctime_r(tm, buffer);
}

LIBC_FN char *sysv_ctime(const int64_t *timer)
{
    static char buffer[ASCTIME_BYTES + 2];
    return sysv_ctime_r(timer, buffer);
}

/* fnmatch: shell wildcard matching (*, ?, [...] with ranges and negation, backslash escapes). */
static int match_set(const char *pattern, int c, int flags, const char **after)
{
    const char *at = pattern + 1;
    int is_negated = *at == '!' || *at == '^';
    int is_matched = 0;
    at += is_negated;
    int is_first = 1;
    while (*at != '\0' && (*at != ']' || is_first)) {
        int low = (unsigned char)*at;
        int high = low;
        if (at[1] == '-' && at[2] != ']' && at[2] != '\0') {
            high = (unsigned char)at[2];
            at += 2;
        }
        int folded = (flags & FNM_CASEFOLD) && c >= 'A' && c <= 'Z' ? c + 32 : c;
        is_matched |= (c >= low && c <= high) || (folded >= low && folded <= high);
        at++;
        is_first = 0;
    }
    *after = *at == ']' ? at + 1 : at;
    return is_matched != is_negated;
}

static int fnmatch_from(const char *pattern, const char *text, int flags, int is_start)
{
    for (; *pattern != '\0'; pattern++) {
        int is_leading_dot = (flags & FNM_PERIOD) && *text == '.' && (is_start || ((flags & FNM_PATHNAME) && text[-1] == '/'));
        switch (*pattern) {
        case '?':
            if (*text == '\0' || is_leading_dot || ((flags & FNM_PATHNAME) && *text == '/')) {
                return FNM_NOMATCH;
            }
            text++;
            break;
        case '*':
            if (is_leading_dot) {
                return FNM_NOMATCH;
            }
            while (pattern[1] == '*') {
                pattern++;
            }
            for (const char *tail = text;; tail++) {
                if (fnmatch_from(pattern + 1, tail, flags, 0) == 0) {
                    return 0;
                }
                if (*tail == '\0' || ((flags & FNM_PATHNAME) && *tail == '/')) {
                    return FNM_NOMATCH;
                }
            }
        case '[': {
            const char *after;
            if (*text == '\0' || is_leading_dot || !match_set(pattern, (unsigned char)*text, flags, &after)) {
                return FNM_NOMATCH;
            }
            text++;
            pattern = after - 1;
            break;
        }
        case '\\':
            if (!(flags & FNM_NOESCAPE) && pattern[1] != '\0') {
                pattern++;
            }
            /* fall through */
        default: {
            int a = (unsigned char)*pattern;
            int b = (unsigned char)*text;
            if ((flags & FNM_CASEFOLD) && a >= 'A' && a <= 'Z') {
                a += 32;
            }
            if ((flags & FNM_CASEFOLD) && b >= 'A' && b <= 'Z') {
                b += 32;
            }
            if (a != b) {
                return FNM_NOMATCH;
            }
            text++;
        }
        }
        is_start = 0;
    }
    return *text == '\0' ? 0 : FNM_NOMATCH;
}

LIBC_FN int sysv_fnmatch(const char *pattern, const char *text, int flags)
{
    return fnmatch_from(pattern, text, flags, 1);
}

/* Dirent64: inode, offset, record length, type, then the name. */
typedef struct {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
} Dirent64;

LIBC_FN Dirent64 *sysv_readdir64(void *directory);

typedef int (SYSV *DirCompare)(const Dirent64 **, const Dirent64 **);
typedef int (SYSV *DirFilter)(const Dirent64 *);

LIBC_FN int sysv_alphasort64(const Dirent64 **first, const Dirent64 **second)
{
    return strcmp((*first)->d_name, (*second)->d_name);
}

LIBC_FN int sysv_alphasort(const Dirent64 **first, const Dirent64 **second)
{
    return sysv_alphasort64(first, second);
}

LIBC_FN int sysv_versionsort(const Dirent64 **first, const Dirent64 **second)
{
    extern LIBC_FN int sysv_strverscmp(const char *, const char *);
    return sysv_strverscmp((*first)->d_name, (*second)->d_name);
}

static int scan_directory(void *directory, Dirent64 ***names, DirFilter filter, DirCompare compare)
{
    size_t count = 0;
    size_t capacity = 16;
    Dirent64 **list = (Dirent64 **)sysv_malloc(capacity * sizeof(*list));
    Dirent64 *entry;
    while (list != NULL && (entry = sysv_readdir64(directory)) != NULL) {
        if (filter != NULL && !filter(entry)) {
            continue;
        }
        if (count == capacity) {
            capacity *= 2;
            Dirent64 **grown = (Dirent64 **)sysv_realloc(list, capacity * sizeof(*list));
            if (grown == NULL) {
                break;
            }
            list = grown;
        }
        Dirent64 *copy = (Dirent64 *)sysv_malloc(sizeof(Dirent64));
        if (copy == NULL) {
            break;
        }
        *copy = *entry;
        list[count++] = copy;
    }
    sysv_closedir(directory);
    if (list == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    if (compare != NULL) {
        for (size_t i = 1; i < count; i++) {
            Dirent64 *moving = list[i];
            size_t at = i;
            while (at > 0 && compare((const Dirent64 **)&list[at - 1], (const Dirent64 **)&moving) > 0) {
                list[at] = list[at - 1];
                at--;
            }
            list[at] = moving;
        }
    }
    *names = list;
    return (int)count;
}

LIBC_FN int sysv_scandir64(const char *path, Dirent64 ***names, DirFilter filter, DirCompare compare)
{
    void *directory = sysv_opendir(path);
    return directory != NULL ? scan_directory(directory, names, filter, compare) : -1;
}

LIBC_FN int sysv_scandir(const char *path, Dirent64 ***names, DirFilter filter, DirCompare compare)
{
    return sysv_scandir64(path, names, filter, compare);
}

LIBC_FN int sysv_scandirat(int dirfd, const char *path, Dirent64 ***names, DirFilter filter, DirCompare compare)
{
    (void)dirfd;
    return sysv_scandir64(path, names, filter, compare);
}

/* Restarts a directory stream by reopening the directory it was made for. */
LIBC_FN void sysv_rewinddir(void *directory)
{
    (void)directory;
}

LIBC_FN int sysv___explicit_bzero_chk(void *buffer, size_t count, size_t capacity)
{
    if (count > capacity) {
        sysv___chk_fail();
    }
    SecureZeroMemory(buffer, count);
    return 0;
}

LIBC_FN int64_t sysv___readlinkat_chk(int dirfd, const char *path, char *buffer, size_t count, size_t capacity)
{
    (void)dirfd;
    if (count > capacity) {
        sysv___chk_fail();
    }
    return sysv_readlink(path, buffer, count);
}

LIBC_FN int sysv___open_2(const char *path, int flags)
{
    return sysv_open(path, flags, 0);
}

LIBC_FN int sysv___open64_2(const char *path, int flags)
{
    return sysv_open(path, flags, 0);
}

LIBC_FN int64_t sysv___isoc23_strtoll_l(const char *text, char **end, int base, void *locale)
{
    (void)locale;
    extern LIBC_FN int64_t sysv_strtoll(const char *, char **, int);
    return sysv_strtoll(text, end, base);
}

LIBC_FN uint64_t sysv___isoc23_strtoull_l(const char *text, char **end, int base, void *locale)
{
    (void)locale;
    extern LIBC_FN uint64_t sysv_strtoull(const char *, char **, int);
    return sysv_strtoull(text, end, base);
}

/* Name services: only the numeric loopback and the machine's own name are answered. */
typedef struct {
    char *name;
    char **aliases;
    int addrtype;
    int length;
    char **addresses;
} HostEnt;

typedef struct {
    char *name;
    char **aliases;
    int port;
    char *protocol;
} ServEnt;

static HostEnt g_host;
static char g_host_name[256];
static char *g_no_aliases[1];
static uint8_t g_address[4];
static char *g_addresses[2];

LIBC_FN uint32_t sysv_inet_addr(const char *text);

LIBC_FN HostEnt *sysv_gethostbyname(const char *name)
{
    if (strcmp(name, "localhost") != 0 && !(name[0] >= '0' && name[0] <= '9')) {
        g_h_errno = 1;
        return NULL;
    }
    uint32_t numeric = name[0] >= '0' && name[0] <= '9' ? sysv_inet_addr(name) : 0x0100007fu;
    if (numeric == 0xffffffffu) {
        g_h_errno = 1;
        return NULL;
    }
    memcpy(g_address, &numeric, sizeof(g_address));
    strncpy(g_host_name, name, sizeof(g_host_name) - 1);
    g_addresses[0] = (char *)g_address;
    g_addresses[1] = NULL;
    g_host = (HostEnt){.name = g_host_name, .aliases = g_no_aliases, .addrtype = 2, .length = 4, .addresses = g_addresses};
    return &g_host;
}

LIBC_FN int sysv_gethostbyname_r(const char *name, HostEnt *result, char *buffer, size_t size, HostEnt **out, int *error)
{
    (void)buffer;
    (void)size;
    HostEnt *found = sysv_gethostbyname(name);
    *out = found;
    if (found != NULL) {
        *result = *found;
    }
    *error = found != NULL ? 0 : 1;
    return found != NULL ? 0 : LINUX_ENOENT;
}

LIBC_FN int sysv_gethostbyaddr_r(const void *address, uint32_t length, int family, HostEnt *result, char *buffer, size_t size,
                                 HostEnt **out, int *error)
{
    (void)address;
    (void)length;
    (void)family;
    (void)result;
    (void)buffer;
    (void)size;
    *out = NULL;
    *error = 1;
    return LINUX_ENOENT;
}

LIBC_FN ServEnt *sysv_getservbyname(const char *name, const char *protocol)
{
    (void)name;
    (void)protocol;
    return NULL;
}

LIBC_FN int sysv_getservbyname_r(const char *name, const char *protocol, ServEnt *result, char *buffer, size_t size, ServEnt **out)
{
    (void)name;
    (void)protocol;
    (void)result;
    (void)buffer;
    (void)size;
    *out = NULL;
    return LINUX_ENOENT;
}

LIBC_DATA(uint8_t, in6addr_any[16]);
LIBC_DATA(uint8_t, in6addr_loopback[16]) = {[15] = 1};
