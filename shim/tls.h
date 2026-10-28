/*
 * File: shim/tls.h
 * Purpose: Thread-local storage interface: per-thread blocks, the thread pointer, stack canaries
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_TLS_H
#define CROSSPORT_TLS_H

#include "startinfo.h"

/*
 * One-time setup, called by the main thread before any translated code runs:
 * reserves the Windows TLS slot, patches every module's stubs and canary cell,
 * seals the stub sections as executable, and attaches the calling thread.
 * `modules` must stay valid for the life of the process.
 */
void tls_init(const LoadedModule *modules, size_t count);

/* Allocates and initialises this thread's TLS block and thread pointer. Returns NULL on failure. */
uint8_t *tls_attach_thread(void);

/* Releases the calling thread's TLS block. Safe to call when none is attached. */
void tls_detach_thread(void);

/* The calling thread's thread pointer, or NULL if it has no TLS block. */
uint8_t *tls_thread_pointer(void);

/* Private words in the thread control block, for the pthread layer. */
#define TCB_THREAD_OFFSET 0x88u /* the thread's pthread_t object */
#define TCB_KEYS_OFFSET 0x90u   /* array of pthread_key values */
#define TCB_ERRNO_OFFSET 0x98u  /* this thread's errno */

/* Address of a private TCB word of the calling thread. The thread must have a TLS block. */
static inline void **tls_tcb_word(uint32_t offset)
{
    return (void **)(tls_thread_pointer() + offset);
}

#endif
