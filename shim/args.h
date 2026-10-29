/*
 * File: shim/args.h
 * Purpose: Declares the builder of the process's initial stack block
 * Author: Akilesh
 * Date: 2026-10-28
 */

#ifndef CROSSPORT_ARGS_H
#define CROSSPORT_ARGS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Returns a heap block laid out like the stack the kernel hands to _start (argc at slot 0) and stores its size
 * in slots. The caller copies it onto the real thread stack: the program must not run on this block. Needs
 * env_init() first.
 */
uint64_t *args_build_stack(size_t *slot_count);

#endif
