/*
 * File: tests/corpus/tls_basic/libtls.h
 * Purpose: Interface of the TLS test library: thread-local variables reached through several TLS models
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_LIBTLS_H
#define CROSSPORT_LIBTLS_H

/* Exported thread-local variable the executable reads directly (initial-exec model). */
extern __thread int lib_exported;

int lib_get(void);
void lib_set(int value);
int lib_read_exported(void);
/* Touches two non-exported thread-local variables (local-dynamic model) and returns their sum. */
int lib_hidden_sum(void);

#endif
