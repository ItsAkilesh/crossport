/*
 * File: shim/stdio_internal.h
 * Purpose: The FILE object (glibc-compatible head) and the buffered-I/O primitives stdio, printf and exit share
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * Programs built against glibc's headers read the first fields directly through getc_unlocked,
 * putc_unlocked, feof_unlocked and ferror_unlocked, so the field order, offsets and flag bits
 * below are an ABI contract and must not change.
 */

#ifndef CROSSPORT_STDIO_INTERNAL_H
#define CROSSPORT_STDIO_INTERNAL_H

#include "fd.h"

#define IO_EOF_SEEN 0x0010
#define IO_ERR_SEEN 0x0020
#define SHIM_EOF (-1)
#define SHIM_BUFFER_BYTES 4096

enum BufferMode { BUFFER_FULL = 0, BUFFER_LINE = 1, BUFFER_NONE = 2 }; /* _IOFBF, _IOLBF, _IONBF */
enum StreamState { STATE_IDLE = 0, STATE_READING = 1, STATE_WRITING = 2 };

typedef struct ShimFile {
    /* glibc struct _IO_FILE, offsets 0 to 144. */
    int flags;
    char *read_ptr;
    char *read_end;
    char *read_base;
    char *write_base;
    char *write_ptr;
    char *write_end;
    char *buf_base;
    char *buf_end;
    char *save_base;
    char *backup_base;
    char *save_end;
    void *markers;
    struct ShimFile *chain;
    int fileno;
    int flags2;
    int64_t old_offset;
    unsigned short cur_column;
    signed char vtable_offset;
    char shortbuf[1];
    void *lock;
    int64_t offset;
    /* Private tail. */
    SRWLOCK mutex;
    DWORD owner;
    LONG depth;
    int state;
    int buffer_mode;
    int is_readable;
    int is_writable;
    int has_own_buffer;
    int is_static;
    struct ShimFile *next_open;
} ShimFile;

/* Recursive per-stream lock, like glibc's. */
void file_lock(ShimFile *file);
void file_unlock(ShimFile *file);

/* Writes `count` bytes through the buffer, honouring line buffering. Returns 0, or -1 with the error flag set. */
int file_put(ShimFile *file, const void *data, size_t count);
/* Writes out buffered data. Returns 0 or -1. */
int file_flush(ShimFile *file);
/* Refills the read buffer. Returns the first byte, or SHIM_EOF with the EOF or error flag set. */
int file_fill(ShimFile *file);
/* Prepares a stream for reading or writing, flushing or discarding the other direction's buffer. */
int file_prepare(ShimFile *file, int state);

ShimFile *file_new(int fd, int is_readable, int is_writable);
/* Flushes every open writable stream; used by exit and fflush(NULL). */
void stdio_flush_all(void);
void stdio_init(void);

extern ShimFile *crossport_data_stdin;
extern ShimFile *crossport_data_stdout;
extern ShimFile *crossport_data_stderr;

SYSV size_t sysv_fread(void *destination, size_t size, size_t count, ShimFile *file);

#endif
