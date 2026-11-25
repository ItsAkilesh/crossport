/*
 * File: shim/shell_support.c
 * Purpose: Calls shells, terminal multiplexers and system monitors need: confstr, imaxdiv, tcsetpgrp, sigsetmask, sigsuspend, wait3, futimes,
 *          strxfrm_l, termios speeds, closefrom, base64 for the resolver, exp10f and wcsrtombs_chk
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Found by the second application sweep: bash, dash, zsh, tmux and htop each failed to translate on a handful of these.
 */

#include <string.h>

#include "fd.h"
#include "libc_abi.h"

#define CS_PATH 0
#define CS_GNU_LIBC_VERSION 2
#define CS_GNU_LIBPTHREAD_VERSION 3
#define CBAUD_MASK 0010017u
#define MAX_DESCRIPTOR 1024
#define NANOSECONDS_PER_MICROSECOND 1000
#define PATH_VALUE "/bin:/usr/bin"
#define LIBC_VERSION_VALUE "glibc 2.39"
#define THREADS_VERSION_VALUE "NPTL 2.39"

typedef struct {
    int64_t quotient;
    int64_t remainder;
} Division;

/* x86-64 struct termios: four flag words, the line discipline, 32 control characters and two speeds. */
typedef struct {
    uint32_t input_flags;
    uint32_t output_flags;
    uint32_t control_flags;
    uint32_t local_flags;
    uint8_t line;
    uint8_t control_chars[32];
    uint32_t input_speed;
    uint32_t output_speed;
} Termios;

LIBC_FN int64_t sysv_wait4(int pid, int *status, int options, void *usage);
LIBC_FN int sysv_futimens(int fd, const int64_t times[4]);
LIBC_FN size_t sysv_strxfrm(char *destination, const char *source, size_t count);
LIBC_FN size_t sysv_wcsrtombs(char *destination, const int32_t **source, size_t count, void *state);
LIBC_FN float sysv_powf(float base, float exponent);
LIBC_FN NORETURN void sysv___chk_fail(void);

/* confstr(3) for the three names programs ask about; the result length includes the terminating NUL. */
LIBC_FN size_t sysv_confstr(int name, char *buffer, size_t size)
{
    const char *value;
    switch (name) {
    case CS_PATH:
        value = PATH_VALUE;
        break;
    case CS_GNU_LIBC_VERSION:
        value = LIBC_VERSION_VALUE;
        break;
    case CS_GNU_LIBPTHREAD_VERSION:
        value = THREADS_VERSION_VALUE;
        break;
    default:
        shim_set_errno(LINUX_EINVAL);
        return 0;
    }
    size_t needed = strlen(value) + 1;
    if (size > 0 && buffer != NULL) {
        size_t copied = needed < size ? needed : size - 1;
        memcpy(buffer, value, copied);
        buffer[copied] = '\0';
    }
    return needed;
}

LIBC_FN size_t sysv___confstr_chk(int name, char *buffer, size_t size, size_t buffer_size)
{
    if (size > buffer_size) {
        sysv___chk_fail();
    }
    return sysv_confstr(name, buffer, size);
}

LIBC_FN size_t sysv___wcsrtombs_chk(char *destination, const int32_t **source, size_t count, void *state, size_t destination_size)
{
    if (destination != NULL && count > destination_size) {
        sysv___chk_fail();
    }
    return sysv_wcsrtombs(destination, source, count, state);
}

LIBC_FN Division sysv_imaxdiv(int64_t numerator, int64_t denominator)
{
    return (Division){numerator / denominator, numerator % denominator};
}

/* There is no job control, so any process group is already the foreground one. */
LIBC_FN int sysv_tcsetpgrp(int fd, int group)
{
    (void)fd;
    (void)group;
    return 0;
}

LIBC_FN int sysv_sigsetmask(int mask)
{
    (void)mask;
    return 0;
}

/* sigsuspend(2) returns after a signal handler runs; the shim runs none asynchronously, so it reports the interruption at once. */
LIBC_FN int sysv_sigsuspend(const void *mask)
{
    (void)mask;
    return shim_fail(LINUX_EINTR);
}

LIBC_FN int sysv_wait3(int *status, int options, void *usage)
{
    return (int)sysv_wait4(-1, status, options, usage);
}

LIBC_FN int sysv_futimes(int fd, const int64_t times[4])
{
    if (times == NULL) {
        return sysv_futimens(fd, NULL);
    }
    int64_t converted[4] = {times[0], times[1] * NANOSECONDS_PER_MICROSECOND, times[2], times[3] * NANOSECONDS_PER_MICROSECOND};
    return sysv_futimens(fd, converted);
}

LIBC_FN size_t sysv_strxfrm_l(char *destination, const char *source, size_t count, void *locale)
{
    (void)locale;
    return sysv_strxfrm(destination, source, count);
}

LIBC_FN int sysv_cfsetispeed(Termios *attributes, unsigned speed)
{
    attributes->control_flags = (attributes->control_flags & ~CBAUD_MASK) | (speed & CBAUD_MASK);
    return 0;
}

LIBC_FN int sysv_cfsetospeed(Termios *attributes, unsigned speed)
{
    return sysv_cfsetispeed(attributes, speed);
}

/* Closes every open descriptor from `first` up. */
LIBC_FN void sysv_closefrom(int first)
{
    for (int fd = first < 0 ? 0 : first; fd < MAX_DESCRIPTOR; fd++) {
        if (shim_fd_handle(fd) != INVALID_HANDLE_VALUE) {
            shim_fd_close(fd);
        }
    }
}

LIBC_FN float sysv_exp10f(float exponent)
{
    return sysv_powf(10.0f, exponent);
}

static const char BASE64_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* __b64_ntop: base64 encoding with padding into `target`; returns the length (without the NUL) or -1 when it does not fit. */
LIBC_FN int sysv___b64_ntop(const uint8_t *source, size_t length, char *target, size_t target_size)
{
    size_t out = 0;
    if (target_size < (length + 2) / 3 * 4 + 1) {
        return -1;
    }
    for (size_t i = 0; i < length; i += 3) {
        uint32_t group = (uint32_t)source[i] << 16 | (i + 1 < length ? (uint32_t)source[i + 1] << 8 : 0u) | (i + 2 < length ? source[i + 2] : 0u);
        target[out++] = BASE64_ALPHABET[group >> 18 & 63];
        target[out++] = BASE64_ALPHABET[group >> 12 & 63];
        target[out++] = i + 1 < length ? BASE64_ALPHABET[group >> 6 & 63] : '=';
        target[out++] = i + 2 < length ? BASE64_ALPHABET[group & 63] : '=';
    }
    target[out] = '\0';
    return (int)out;
}

/* __b64_pton: decodes base64 (spaces are skipped) into `target`; returns the byte count or -1 for malformed input. */
LIBC_FN int sysv___b64_pton(const char *source, uint8_t *target, size_t target_size)
{
    uint32_t bits = 0;
    int collected = 0;
    size_t out = 0;
    int padding = 0;
    for (; *source != '\0'; source++) {
        if (*source == ' ' || *source == '\n' || *source == '\t' || *source == '\r') {
            continue;
        }
        const char *at = *source == '=' ? NULL : strchr(BASE64_ALPHABET, *source);
        if (*source == '=') {
            padding++;
            continue;
        }
        if (at == NULL || padding > 0) {
            return -1;
        }
        bits = bits << 6 | (uint32_t)(at - BASE64_ALPHABET);
        if (++collected == 4) {
            if (out + 3 > target_size) {
                return -1;
            }
            target[out++] = (uint8_t)(bits >> 16);
            target[out++] = (uint8_t)(bits >> 8);
            target[out++] = (uint8_t)bits;
            bits = 0;
            collected = 0;
        }
    }
    if (collected == 2 && out + 1 <= target_size) {
        target[out++] = (uint8_t)(bits >> 4);
    } else if (collected == 3 && out + 2 <= target_size) {
        target[out++] = (uint8_t)(bits >> 10);
        target[out++] = (uint8_t)(bits >> 2);
    } else if (collected != 0) {
        return -1;
    }
    return (int)out;
}
