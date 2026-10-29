/*
 * File: shim/thread_cell.h
 * Purpose: A per-thread pointer that needs no compiler TLS support (emulated TLS would call into the shim's own pthread exports)
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef CROSSPORT_THREAD_CELL_H
#define CROSSPORT_THREAD_CELL_H

#include "libc_abi.h"

/* Declare with THREAD_CELL_INIT; the Win32 TLS slot behind it is allocated on first use. */
typedef struct {
    INIT_ONCE once;
    DWORD index;
} ThreadCell;

#define THREAD_CELL_INIT {INIT_ONCE_STATIC_INIT, TLS_OUT_OF_INDEXES}

/* The calling thread's value, NULL until it has set one. */
void *thread_cell_get(ThreadCell *cell);
void thread_cell_set(ThreadCell *cell, void *value);

#endif
