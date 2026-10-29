/*
 * File: shim/string.c
 * Purpose: The str* functions of the C library in the C locale, plus strerror
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "libc_abi.h"

LIBC_FN size_t sysv_strlen(const char *text)
{
    const char *end = text;
    while (*end != '\0') {
        end++;
    }
    return (size_t)(end - text);
}

LIBC_FN size_t sysv_strnlen(const char *text, size_t limit)
{
    size_t length = 0;
    while (length < limit && text[length] != '\0') {
        length++;
    }
    return length;
}

LIBC_FN int sysv_strcmp(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return (int)(uint8_t)*left - (int)(uint8_t)*right;
}

LIBC_FN int sysv_strncmp(const char *left, const char *right, size_t limit)
{
    for (; limit > 0; limit--, left++, right++) {
        if (*left != *right || *left == '\0') {
            return (int)(uint8_t)*left - (int)(uint8_t)*right;
        }
    }
    return 0;
}

static int fold(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

LIBC_FN int sysv_strcasecmp(const char *left, const char *right)
{
    while (*left != '\0' && fold((uint8_t)*left) == fold((uint8_t)*right)) {
        left++;
        right++;
    }
    return fold((uint8_t)*left) - fold((uint8_t)*right);
}

LIBC_FN int sysv_strncasecmp(const char *left, const char *right, size_t limit)
{
    for (; limit > 0; limit--, left++, right++) {
        int a = fold((uint8_t)*left);
        int b = fold((uint8_t)*right);
        if (a != b || a == 0) {
            return a - b;
        }
    }
    return 0;
}

/* In the C locale collation is byte order. */
LIBC_FN int sysv_strcoll(const char *left, const char *right)
{
    return sysv_strcmp(left, right);
}

LIBC_FN char *sysv_strchr(const char *text, int value)
{
    for (;; text++) {
        if (*text == (char)value) {
            return (char *)text;
        }
        if (*text == '\0') {
            return NULL;
        }
    }
}

LIBC_FN char *sysv_strrchr(const char *text, int value)
{
    const char *found = NULL;
    for (;; text++) {
        if (*text == (char)value) {
            found = text;
        }
        if (*text == '\0') {
            return (char *)found;
        }
    }
}

LIBC_FN char *sysv_strstr(const char *haystack, const char *needle)
{
    size_t length = sysv_strlen(needle);
    if (length == 0) {
        return (char *)haystack;
    }
    for (; *haystack != '\0'; haystack++) {
        if (*haystack == *needle && sysv_strncmp(haystack, needle, length) == 0) {
            return (char *)haystack;
        }
    }
    return NULL;
}

static int in_set(char c, const char *set)
{
    for (; *set != '\0'; set++) {
        if (*set == c) {
            return 1;
        }
    }
    return 0;
}

LIBC_FN size_t sysv_strspn(const char *text, const char *accept)
{
    size_t count = 0;
    while (text[count] != '\0' && in_set(text[count], accept)) {
        count++;
    }
    return count;
}

LIBC_FN size_t sysv_strcspn(const char *text, const char *reject)
{
    size_t count = 0;
    while (text[count] != '\0' && !in_set(text[count], reject)) {
        count++;
    }
    return count;
}

LIBC_FN char *sysv_strpbrk(const char *text, const char *accept)
{
    for (; *text != '\0'; text++) {
        if (in_set(*text, accept)) {
            return (char *)text;
        }
    }
    return NULL;
}

LIBC_FN char *sysv_stpcpy(char *destination, const char *source)
{
    while ((*destination = *source) != '\0') {
        destination++;
        source++;
    }
    return destination;
}

LIBC_FN char *sysv_strcpy(char *destination, const char *source)
{
    sysv_stpcpy(destination, source);
    return destination;
}

LIBC_FN char *sysv_strncpy(char *destination, const char *source, size_t count)
{
    size_t i = 0;
    for (; i < count && source[i] != '\0'; i++) {
        destination[i] = source[i];
    }
    for (; i < count; i++) {
        destination[i] = '\0';
    }
    return destination;
}

LIBC_FN char *sysv_strcat(char *destination, const char *source)
{
    sysv_strcpy(destination + sysv_strlen(destination), source);
    return destination;
}

LIBC_FN char *sysv_strncat(char *destination, const char *source, size_t count)
{
    char *end = destination + sysv_strlen(destination);
    size_t i = 0;
    for (; i < count && source[i] != '\0'; i++) {
        end[i] = source[i];
    }
    end[i] = '\0';
    return destination;
}

LIBC_FN char *sysv_strdup(const char *text)
{
    size_t size = sysv_strlen(text) + 1;
    char *copy = (char *)HeapAlloc(GetProcessHeap(), 0, size);
    if (copy == NULL) {
        shim_set_errno(LINUX_ENOMEM);
        return NULL;
    }
    CopyMemory(copy, text, size);
    return copy;
}

LIBC_FN char *sysv_strerror(int value)
{
    static char unknown[32];
    return (char *)shim_strerror(value, unknown, sizeof(unknown));
}

LIBC_FN int sysv_toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}

LIBC_FN int sysv_tolower(int c)
{
    return fold(c);
}
