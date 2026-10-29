/*
 * File: shim/mem.c
 * Purpose: malloc family on the Win32 process heap and the memory functions, with the fortify _chk variants
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include "libc_abi.h"
#include "mem_aligned.h"

#define CHK_FAILURE_EXIT_CODE 134 /* 128 + SIGABRT, like glibc's abort */

typedef uint64_t __attribute__((may_alias)) Word;

LIBC_FN void *sysv_malloc(size_t size)
{
    void *block = HeapAlloc(GetProcessHeap(), 0, size != 0 ? size : 1);
    if (block == NULL) {
        shim_set_errno(LINUX_ENOMEM);
    }
    return block;
}

LIBC_FN void sysv_free(void *block)
{
    if (block != NULL && !aligned_block_release(block)) {
        HeapFree(GetProcessHeap(), 0, block);
    }
}

LIBC_FN void *sysv_calloc(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size) {
        shim_set_errno(LINUX_ENOMEM);
        return NULL;
    }
    size_t total = count * size;
    void *block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total != 0 ? total : 1);
    if (block == NULL) {
        shim_set_errno(LINUX_ENOMEM);
    }
    return block;
}

LIBC_FN void *sysv_realloc(void *block, size_t size)
{
    if (block == NULL) {
        return sysv_malloc(size);
    }
    if (size == 0) {
        sysv_free(block);
        return NULL;
    }
    size_t aligned_size = aligned_block_size(block);
    if (aligned_size != 0) {
        void *copy = sysv_malloc(size);
        if (copy != NULL) {
            CopyMemory(copy, block, aligned_size < size ? aligned_size : size);
            aligned_block_release(block);
        }
        return copy;
    }
    void *moved = HeapReAlloc(GetProcessHeap(), 0, block, size);
    if (moved == NULL) {
        shim_set_errno(LINUX_ENOMEM);
    }
    return moved;
}

LIBC_FN void *sysv_memcpy(void *destination, const void *source, size_t count)
{
    void *result = destination;
    __asm__ volatile("rep movsb" : "+D"(destination), "+S"(source), "+c"(count) : : "memory");
    return result;
}

LIBC_FN void *sysv_memmove(void *destination, const void *source, size_t count)
{
    uint8_t *to = (uint8_t *)destination;
    const uint8_t *from = (const uint8_t *)source;
    if (to == from || count == 0) {
        return destination;
    }
    if (to < from || to >= from + count) {
        return sysv_memcpy(destination, source, count);
    }
    to += count - 1;
    from += count - 1;
    __asm__ volatile("std\n\trep movsb\n\tcld" : "+D"(to), "+S"(from), "+c"(count) : : "memory");
    return destination;
}

LIBC_FN void *sysv_memset(void *destination, int value, size_t count)
{
    void *result = destination;
    __asm__ volatile("rep stosb" : "+D"(destination), "+c"(count) : "a"((uint8_t)value) : "memory");
    return result;
}

LIBC_FN int sysv_memcmp(const void *left, const void *right, size_t count)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    while (count >= sizeof(Word) && *(const Word *)a == *(const Word *)b) {
        a += sizeof(Word);
        b += sizeof(Word);
        count -= sizeof(Word);
    }
    for (; count > 0; count--, a++, b++) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
    }
    return 0;
}

LIBC_FN void *sysv_memchr(const void *block, int value, size_t count)
{
    const uint8_t *bytes = (const uint8_t *)block;
    for (size_t i = 0; i < count; i++) {
        if (bytes[i] == (uint8_t)value) {
            return (void *)(bytes + i);
        }
    }
    return NULL;
}

/* Naive search anchored on the needle's first byte; fine for the short needles real programs use. */
LIBC_FN void *sysv_memmem(const void *haystack, size_t haystack_length, const void *needle, size_t needle_length)
{
    if (needle_length == 0) {
        return (void *)haystack;
    }
    if (needle_length > haystack_length) {
        return NULL;
    }
    const uint8_t *start = (const uint8_t *)haystack;
    const uint8_t *last = start + (haystack_length - needle_length);
    const uint8_t first = *(const uint8_t *)needle;
    for (const uint8_t *at = start; at <= last; at++) {
        if (*at == first && sysv_memcmp(at, needle, needle_length) == 0) {
            return (void *)at;
        }
    }
    return NULL;
}

NORETURN static void chk_failure(const char *message)
{
    shim_write_all(GetStdHandle(STD_ERROR_HANDLE), message, lstrlenA(message));
    ExitProcess(CHK_FAILURE_EXIT_CODE);
    __builtin_unreachable();
}

LIBC_FN void *sysv___memcpy_chk(void *destination, const void *source, size_t count, size_t destination_size)
{
    if (count > destination_size) {
        chk_failure("*** buffer overflow detected ***: terminated\n");
    }
    return sysv_memcpy(destination, source, count);
}

LIBC_FN void *sysv___memmove_chk(void *destination, const void *source, size_t count, size_t destination_size)
{
    if (count > destination_size) {
        chk_failure("*** buffer overflow detected ***: terminated\n");
    }
    return sysv_memmove(destination, source, count);
}

LIBC_FN void *sysv___memset_chk(void *destination, int value, size_t count, size_t destination_size)
{
    if (count > destination_size) {
        chk_failure("*** buffer overflow detected ***: terminated\n");
    }
    return sysv_memset(destination, value, count);
}

LIBC_FN NORETURN void sysv___chk_fail(void)
{
    chk_failure("*** buffer overflow detected ***: terminated\n");
}
