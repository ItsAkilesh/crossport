/*
 * File: tests/corpus/threads/libcount.h
 * Purpose: Interface of a library whose per-thread counter is reached through general-dynamic TLS from several threads
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_LIBCOUNT_H
#define CROSSPORT_LIBCOUNT_H

/* Adds `amount` to the calling thread's counter and returns the new value. */
long count_add(long amount);

#endif
