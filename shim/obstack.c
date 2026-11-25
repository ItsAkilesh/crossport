/*
 * File: shim/obstack.c
 * Purpose: GNU obstacks: stacks of growable memory chunks that grep, find, gawk and many other GNU programs allocate from
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * The survey found _obstack_begin and _obstack_newchunk missing from 11 and 16 programs. obstack.h works through macros that read and write the
 * fields of struct obstack directly, so the layout here is glibc's; only the functions that grow, free and initialise are real calls.
 * The caller's allocation functions are System V functions called through pointers declared with that convention.
 */

#include <string.h>

#include "libc_abi.h"
#include "startinfo.h"
#include "stdio_internal.h"

#define DEFAULT_ALIGNMENT 16
#define DEFAULT_CHUNK_BYTES 4096
#define CHUNK_OVERHEAD_ESTIMATE 32
#define GROWTH_SLACK 100
#define EXIT_STATUS_ON_FAILURE 1

typedef struct ObstackChunk {
    char *limit;
    struct ObstackChunk *previous;
    char contents[4];
} ObstackChunk;

typedef void *(SYSV *PlainAllocator)(size_t);
typedef void *(SYSV *ExtraAllocator)(void *, size_t);
typedef void (SYSV *PlainDeallocator)(void *);
typedef void (SYSV *ExtraDeallocator)(void *, void *);

typedef struct {
    long chunk_size;
    ObstackChunk *chunk;
    char *object_base;
    char *next_free;
    char *chunk_limit;
    union {
        intptr_t integer;
        void *pointer;
    } temporary;
    int alignment_mask;
    union {
        PlainAllocator plain;
        ExtraAllocator extra;
    } allocate;
    union {
        PlainDeallocator plain;
        ExtraDeallocator extra;
    } release;
    void *extra_argument;
    unsigned use_extra_argument : 1;
    unsigned maybe_empty_object : 1;
    unsigned alloc_failed : 1;
} Obstack;

LIBC_FN NORETURN void sysv_exit(int status);
LIBC_FN int sysv_fputs(const char *text, ShimFile *stream);
LIBC_FN NORETURN void sysv_abort(void);
LIBC_FN int sysv_vasprintf(char **out, const char *format, __builtin_sysv_va_list ap);
LIBC_FN void sysv_free(void *block);

static void SYSV report_exhausted(void)
{
    sysv_fputs("memory exhausted\n", crossport_data_stderr);
    sysv_exit(EXIT_STATUS_ON_FAILURE);
}

LIBC_DATA(void *, obstack_alloc_failed_handler) = (void *)report_exhausted;

LIBC_DATA(int, obstack_exit_failure) = EXIT_STATUS_ON_FAILURE;

static void call_failure_handler(void)
{
    ((void (SYSV *)(void)) * (void **)shim_data_home(&crossport_data_obstack_alloc_failed_handler))();
}

static void *allocate(Obstack *stack, size_t size)
{
    return stack->use_extra_argument ? stack->allocate.extra(stack->extra_argument, size) : stack->allocate.plain(size);
}

static void release(Obstack *stack, void *block)
{
    if (stack->use_extra_argument) {
        stack->release.extra(stack->extra_argument, block);
    } else {
        stack->release.plain(block);
    }
}

static char *align_in_chunk(ObstackChunk *chunk, int mask)
{
    char *base = (char *)chunk;
    return base + ((size_t)(chunk->contents - base + (size_t)mask) & ~(size_t)mask);
}

static int begin(Obstack *stack, long size, long alignment)
{
    alignment = alignment == 0 ? DEFAULT_ALIGNMENT : alignment;
    size = size == 0 ? DEFAULT_CHUNK_BYTES - CHUNK_OVERHEAD_ESTIMATE : size;
    stack->chunk_size = size;
    stack->alignment_mask = (int)alignment - 1;
    stack->chunk = allocate(stack, (size_t)size);
    if (stack->chunk == NULL) {
        call_failure_handler();
        return 0;
    }
    stack->next_free = stack->object_base = align_in_chunk(stack->chunk, stack->alignment_mask);
    stack->chunk_limit = stack->chunk->limit = (char *)stack->chunk + stack->chunk_size;
    stack->chunk->previous = NULL;
    stack->maybe_empty_object = 0;
    stack->alloc_failed = 0;
    return 1;
}

LIBC_FN int sysv__obstack_begin(Obstack *stack, int size, int alignment, void *chunk_function, void *free_function)
{
    stack->use_extra_argument = 0;
    stack->allocate.plain = (PlainAllocator)chunk_function;
    stack->release.plain = (PlainDeallocator)free_function;
    return begin(stack, size, alignment);
}

LIBC_FN int sysv__obstack_begin_1(Obstack *stack, int size, int alignment, void *chunk_function, void *free_function, void *argument)
{
    stack->use_extra_argument = 1;
    stack->extra_argument = argument;
    stack->allocate.extra = (ExtraAllocator)chunk_function;
    stack->release.extra = (ExtraDeallocator)free_function;
    return begin(stack, size, alignment);
}

/* Starts a new chunk big enough for the object being built plus `length` more bytes, and moves the object into it. */
LIBC_FN void sysv__obstack_newchunk(Obstack *stack, int length)
{
    ObstackChunk *old_chunk = stack->chunk;
    long object_size = (long)(stack->next_free - stack->object_base);
    long new_size = object_size + length + (object_size >> 3) + stack->alignment_mask + GROWTH_SLACK;
    new_size = new_size < stack->chunk_size ? stack->chunk_size : new_size;
    ObstackChunk *new_chunk = allocate(stack, (size_t)new_size);
    if (new_chunk == NULL) {
        call_failure_handler();
        return;
    }
    stack->chunk = new_chunk;
    new_chunk->previous = old_chunk;
    new_chunk->limit = stack->chunk_limit = (char *)new_chunk + new_size;
    char *new_base = align_in_chunk(new_chunk, stack->alignment_mask);
    memcpy(new_base, stack->object_base, (size_t)object_size);
    if (!stack->maybe_empty_object && stack->object_base == align_in_chunk(old_chunk, stack->alignment_mask)) {
        new_chunk->previous = old_chunk->previous;
        release(stack, old_chunk);
    }
    stack->object_base = new_base;
    stack->next_free = new_base + object_size;
    stack->maybe_empty_object = 0;
}

/* Frees `object` and everything allocated after it; with NULL, frees the whole obstack. */
LIBC_FN void sysv__obstack_free(Obstack *stack, void *object)
{
    ObstackChunk *chunk = stack->chunk;
    while (chunk != NULL && ((void *)chunk >= object || (void *)chunk->limit < object)) {
        ObstackChunk *previous = chunk->previous;
        release(stack, chunk);
        chunk = previous;
        stack->maybe_empty_object = 1;
    }
    if (chunk != NULL) {
        stack->object_base = stack->next_free = (char *)object;
        stack->chunk_limit = chunk->limit;
        stack->chunk = chunk;
    } else if (object != NULL) {
        sysv_abort();
    }
}

LIBC_FN void sysv_obstack_free(Obstack *stack, void *object)
{
    sysv__obstack_free(stack, object);
}

LIBC_FN int sysv__obstack_allocated_p(Obstack *stack, void *object)
{
    for (ObstackChunk *chunk = stack->chunk; chunk != NULL; chunk = chunk->previous) {
        if ((void *)chunk < object && (void *)chunk->limit >= object) {
            return 1;
        }
    }
    return 0;
}

LIBC_FN int sysv__obstack_memory_used(Obstack *stack)
{
    int total = 0;
    for (ObstackChunk *chunk = stack->chunk; chunk != NULL; chunk = chunk->previous) {
        total += (int)(chunk->limit - (char *)chunk);
    }
    return total;
}

/* obstack_vprintf(3): appends formatted text to the object being grown, without a terminating NUL. */
LIBC_FN int sysv_obstack_vprintf(Obstack *stack, const char *format, __builtin_sysv_va_list arguments)
{
    char *text;
    int length = sysv_vasprintf(&text, format, arguments);
    if (length < 0) {
        return -1;
    }
    if (stack->next_free + length > stack->chunk_limit) {
        sysv__obstack_newchunk(stack, length);
    }
    memcpy(stack->next_free, text, (size_t)length);
    stack->next_free += length;
    sysv_free(text);
    return length;
}
