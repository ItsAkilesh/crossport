/*
 * File: shim/thread_cell.c
 * Purpose: Per-thread pointers on Win32 TLS slots, for the few thread-local variables the shim itself keeps
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include "thread_cell.h"

static BOOL CALLBACK allocate(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)context;
    ThreadCell *cell = (ThreadCell *)parameter;
    cell->index = TlsAlloc();
    return cell->index != TLS_OUT_OF_INDEXES;
}

static int ensure_slot(ThreadCell *cell)
{
    return InitOnceExecuteOnce(&cell->once, allocate, cell, NULL) != 0 && cell->index != TLS_OUT_OF_INDEXES;
}

void *thread_cell_get(ThreadCell *cell)
{
    return ensure_slot(cell) ? TlsGetValue(cell->index) : NULL;
}

void thread_cell_set(ThreadCell *cell, void *value)
{
    if (ensure_slot(cell)) {
        TlsSetValue(cell->index, value);
    }
}
