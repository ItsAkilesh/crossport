/*
 * File: shim/mem_aligned.h
 * Purpose: The hooks malloc, free and realloc use to recognise blocks that came from the aligned allocator
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef CROSSPORT_MEM_ALIGNED_H
#define CROSSPORT_MEM_ALIGNED_H

#include <stddef.h>

/* A block with the requested power-of-two alignment (at most 64 KiB), or NULL with errno set. */
void *aligned_block_create(size_t alignment, size_t size);

/* The size of a block this allocator made, or 0 when `block` is an ordinary heap block. */
size_t aligned_block_size(const void *block);

/* Releases a block this allocator made. Returns 0 when `block` is not one of them. */
int aligned_block_release(void *block);

#endif
