/*
 * File: shim/threads.h
 * Purpose: Internal interface of the pthread layer: what startup and thread teardown need from it
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_THREADS_H
#define CROSSPORT_THREADS_H

/* Registers the calling (main) thread so pthread_self works. Call after tls_init. */
void threads_init_main(void);

/* Runs key destructors for the calling thread and frees its key storage. */
void keys_run_destructors(void);

#endif
