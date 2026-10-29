/*
 * File: shim/aligned.c
 * Purpose: aligned_alloc, posix_memalign and memalign, and the bookkeeping that lets free and realloc accept their blocks
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The process heap returns 16-byte aligned blocks. Larger alignments are served from whole pages, which
 * are aligned to the 64 KiB allocation granularity, and listed here so that free() and realloc() can tell
 * such a block from a heap block. Heap blocks are never page aligned by chance without being looked up
 * here, so free() only searches the list for page-aligned pointers.
 */

#include "libc_abi.h"
#include "mem_aligned.h"

#define HEAP_ALIGNMENT 16
#define PAGE_SIZE 4096
#define MAX_ALIGNMENT 65536

typedef struct Block {
    void *address;
    size_t size;
    struct Block *next;
} Block;

static Block *g_blocks;
static SRWLOCK g_lock = SRWLOCK_INIT;

static int is_power_of_two(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

/* Remembers a page-backed block; false when the bookkeeping itself cannot be allocated. */
static int record(void *address, size_t size)
{
    Block *block = (Block *)HeapAlloc(GetProcessHeap(), 0, sizeof(Block));
    if (block == NULL) {
        return 0;
    }
    block->address = address;
    block->size = size;
    AcquireSRWLockExclusive(&g_lock);
    block->next = g_blocks;
    g_blocks = block;
    ReleaseSRWLockExclusive(&g_lock);
    return 1;
}

void *aligned_block_create(size_t alignment, size_t size)
{
    if (!is_power_of_two(alignment) || alignment > MAX_ALIGNMENT) {
        shim_set_errno(LINUX_EINVAL);
        return NULL;
    }
    size_t usable = size != 0 ? size : 1;
    if (alignment <= HEAP_ALIGNMENT) {
        return HeapAlloc(GetProcessHeap(), 0, usable);
    }
    size_t rounded = (usable + PAGE_SIZE - 1) & ~((size_t)PAGE_SIZE - 1);
    void *address = VirtualAlloc(NULL, rounded, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (address != NULL && !record(address, usable)) {
        VirtualFree(address, 0, MEM_RELEASE);
        address = NULL;
    }
    if (address == NULL) {
        shim_set_errno(LINUX_ENOMEM);
    }
    return address;
}

size_t aligned_block_size(const void *block)
{
    size_t size = 0;
    if (((uintptr_t)block & (PAGE_SIZE - 1)) != 0) {
        return 0;
    }
    AcquireSRWLockShared(&g_lock);
    for (const Block *at = g_blocks; at != NULL && size == 0; at = at->next) {
        size = at->address == block ? at->size : 0;
    }
    ReleaseSRWLockShared(&g_lock);
    return size;
}

int aligned_block_release(void *block)
{
    Block *found = NULL;
    if (((uintptr_t)block & (PAGE_SIZE - 1)) != 0) {
        return 0;
    }
    AcquireSRWLockExclusive(&g_lock);
    for (Block **link = &g_blocks; *link != NULL; link = &(*link)->next) {
        if ((*link)->address == block) {
            found = *link;
            *link = found->next;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (found == NULL) {
        return 0;
    }
    VirtualFree(block, 0, MEM_RELEASE);
    HeapFree(GetProcessHeap(), 0, found);
    return 1;
}

LIBC_FN void *sysv_aligned_alloc(size_t alignment, size_t size)
{
    return aligned_block_create(alignment, size);
}

LIBC_FN void *sysv_memalign(size_t alignment, size_t size)
{
    return aligned_block_create(alignment, size);
}

LIBC_FN int sysv_posix_memalign(void **out, size_t alignment, size_t size)
{
    void *block = aligned_block_create(alignment, size);
    if (block == NULL) {
        return *shim_errno_ptr();
    }
    *out = block;
    return 0;
}
