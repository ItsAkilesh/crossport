/*
 * File: shim/string_more.c
 * Purpose: String functions beyond the core set: strtok, strsep, strndup, strlcpy, strcasestr, memrchr, basename, strsignal and friends
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

#define SIGNAL_NAME_COUNT 32
#define UNKNOWN_SIGNAL_BUFFER 32

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN size_t sysv_strlen(const char *text);
LIBC_FN char *sysv_strchr(const char *text, int c);
LIBC_FN size_t sysv_strspn(const char *text, const char *accept);
LIBC_FN size_t sysv_strcspn(const char *text, const char *reject);
LIBC_FN int sysv_strncasecmp(const char *first, const char *second, size_t count);
LIBC_FN NORETURN void sysv___chk_fail(void);
const char *shim_strerror(int value, char *scratch, size_t scratch_size);

static const char *const SIGNAL_NAMES[SIGNAL_NAME_COUNT] = {
    NULL, "Hangup", "Interrupt", "Quit", "Illegal instruction", "Trace/breakpoint trap", "Aborted", "Bus error",
    "Floating point exception", "Killed", "User defined signal 1", "Segmentation fault", "User defined signal 2",
    "Broken pipe", "Alarm clock", "Terminated", "Stack fault", "Child exited", "Continued", "Stopped (signal)",
    "Stopped", "Stopped (tty input)", "Stopped (tty output)", "Urgent I/O condition", "CPU time limit exceeded",
    "File size limit exceeded", "Virtual timer expired", "Profiling timer expired", "Window changed", "I/O possible",
    "Power failure", "Bad system call"};

LIBC_FN char *sysv_strndup(const char *text, size_t limit)
{
    size_t length = 0;
    while (length < limit && text[length] != '\0') {
        length++;
    }
    char *copy = (char *)sysv_malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

LIBC_FN char *sysv_strchrnul(const char *text, int c)
{
    while (*text != '\0' && *text != (char)c) {
        text++;
    }
    return (char *)text;
}

LIBC_FN char *sysv_stpncpy(char *to, const char *from, size_t count)
{
    size_t length = 0;
    while (length < count && from[length] != '\0') {
        to[length] = from[length];
        length++;
    }
    char *end = to + length;
    while (length < count) {
        to[length++] = '\0';
    }
    return end;
}

LIBC_FN char *sysv_strsep(char **cursor, const char *delimiters)
{
    char *start = *cursor;
    if (start == NULL) {
        return NULL;
    }
    char *end = start + sysv_strcspn(start, delimiters);
    if (*end != '\0') {
        *end = '\0';
        *cursor = end + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

LIBC_FN char *sysv_strtok_r(char *text, const char *delimiters, char **saved)
{
    char *start = text != NULL ? text : *saved;
    if (start == NULL) {
        return NULL;
    }
    start += sysv_strspn(start, delimiters);
    if (*start == '\0') {
        *saved = NULL;
        return NULL;
    }
    char *end = start + sysv_strcspn(start, delimiters);
    if (*end != '\0') {
        *end = '\0';
        *saved = end + 1;
    } else {
        *saved = NULL;
    }
    return start;
}

LIBC_FN char *sysv_strtok(char *text, const char *delimiters)
{
    static char *saved;
    return sysv_strtok_r(text, delimiters, &saved);
}

LIBC_FN size_t sysv_strlcpy(char *to, const char *from, size_t size)
{
    size_t length = sysv_strlen(from);
    if (size > 0) {
        size_t copied = length < size - 1 ? length : size - 1;
        memcpy(to, from, copied);
        to[copied] = '\0';
    }
    return length;
}

LIBC_FN size_t sysv_strlcat(char *to, const char *from, size_t size)
{
    size_t used = 0;
    while (used < size && to[used] != '\0') {
        used++;
    }
    return used + (used < size ? sysv_strlcpy(to + used, from, size - used) : sysv_strlen(from));
}

LIBC_FN size_t sysv___strlcpy_chk(char *to, const char *from, size_t size, size_t capacity)
{
    if (size > capacity) {
        sysv___chk_fail();
    }
    return sysv_strlcpy(to, from, size);
}

LIBC_FN char *sysv_strcasestr(const char *haystack, const char *needle)
{
    size_t length = sysv_strlen(needle);
    for (; *haystack != '\0'; haystack++) {
        if (sysv_strncasecmp(haystack, needle, length) == 0) {
            return (char *)haystack;
        }
    }
    return length == 0 ? (char *)haystack : NULL;
}

/* Compares like strcmp except that runs of digits compare as numbers (version order). */
LIBC_FN int sysv_strverscmp(const char *first, const char *second)
{
    while (*first != '\0' && *second != '\0') {
        int is_digits = *first >= '0' && *first <= '9' && *second >= '0' && *second <= '9';
        if (is_digits) {
            while (*first == '0' && first[1] >= '0' && first[1] <= '9') {
                first++;
            }
            while (*second == '0' && second[1] >= '0' && second[1] <= '9') {
                second++;
            }
            size_t first_digits = 0;
            size_t second_digits = 0;
            while (first[first_digits] >= '0' && first[first_digits] <= '9') {
                first_digits++;
            }
            while (second[second_digits] >= '0' && second[second_digits] <= '9') {
                second_digits++;
            }
            if (first_digits != second_digits) {
                return first_digits < second_digits ? -1 : 1;
            }
            int order = memcmp(first, second, first_digits);
            if (order != 0) {
                return order < 0 ? -1 : 1;
            }
            first += first_digits;
            second += second_digits;
        } else if (*first != *second) {
            return (unsigned char)*first < (unsigned char)*second ? -1 : 1;
        } else {
            first++;
            second++;
        }
    }
    return (unsigned char)*first - (unsigned char)*second;
}

LIBC_FN void *sysv_memrchr(const void *memory, int c, size_t count)
{
    const unsigned char *bytes = (const unsigned char *)memory;
    while (count > 0) {
        if (bytes[--count] == (unsigned char)c) {
            return (void *)(bytes + count);
        }
    }
    return NULL;
}

LIBC_FN void *sysv_mempcpy(void *to, const void *from, size_t count)
{
    memcpy(to, from, count);
    return (char *)to + count;
}

LIBC_FN void *sysv___mempcpy_chk(void *to, const void *from, size_t count, size_t capacity)
{
    if (count > capacity) {
        sysv___chk_fail();
    }
    return sysv_mempcpy(to, from, count);
}

LIBC_FN int sysv_ffs(int value)
{
    return value == 0 ? 0 : __builtin_ctz((unsigned)value) + 1;
}

LIBC_FN int sysv_ffsl(int64_t value)
{
    return value == 0 ? 0 : __builtin_ctzll((uint64_t)value) + 1;
}

LIBC_FN int sysv_ffsll(int64_t value)
{
    return sysv_ffsl(value);
}

/* POSIX basename: may modify its argument, strips trailing slashes. */
LIBC_FN char *sysv___xpg_basename(char *path)
{
    if (path == NULL || *path == '\0') {
        return (char *)".";
    }
    size_t end = sysv_strlen(path);
    while (end > 1 && path[end - 1] == '/') {
        path[--end] = '\0';
    }
    char *slash = NULL;
    for (char *at = path; *at != '\0'; at++) {
        slash = *at == '/' ? at : slash;
    }
    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

/* GNU basename: never modifies its argument; "dir/" gives "". */
LIBC_FN char *sysv_basename(const char *path)
{
    const char *slash = NULL;
    for (const char *at = path; *at != '\0'; at++) {
        slash = *at == '/' ? at : slash;
    }
    return (char *)(slash != NULL ? slash + 1 : path);
}

LIBC_FN char *sysv_strsignal(int number)
{
    static char unknown[UNKNOWN_SIGNAL_BUFFER];
    if (number > 0 && number < SIGNAL_NAME_COUNT) {
        return (char *)SIGNAL_NAMES[number];
    }
    char digits[12];
    size_t count = 0;
    unsigned value = number < 0 ? (unsigned)-number : (unsigned)number;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0 && count < sizeof(digits));
    static const char prefix[] = "Unknown signal ";
    size_t length = sizeof(prefix) - 1;
    memcpy(unknown, prefix, length);
    if (number < 0) {
        unknown[length++] = '-';
    }
    for (size_t i = 0; i < count; i++) {
        unknown[length++] = digits[count - 1 - i];
    }
    unknown[length] = '\0';
    return unknown;
}

/* GNU strerror_r: returns the message, using the buffer only for unknown numbers. */
LIBC_FN char *sysv_strerror_r(int number, char *buffer, size_t size)
{
    char scratch[64];
    const char *message = shim_strerror(number, scratch, sizeof(scratch));
    if (message != scratch) {
        return (char *)message;
    }
    sysv_strlcpy(buffer, message, size);
    return buffer;
}

/* XSI strerror_r: copies the message and returns 0, or ERANGE when it does not fit. */
LIBC_FN int sysv___xpg_strerror_r(int number, char *buffer, size_t size)
{
    char scratch[64];
    const char *message = shim_strerror(number, scratch, sizeof(scratch));
    if (size == 0 || sysv_strlen(message) >= size) {
        if (size > 0) {
            sysv_strlcpy(buffer, message, size);
        }
        return LINUX_ERANGE;
    }
    sysv_strlcpy(buffer, message, size);
    return 0;
}
