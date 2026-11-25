/*
 * File: tests/corpus/cookie_stream/cookie_stream.c
 * Purpose: Compares custom stdio streams (fopencookie) with glibc: writing, reading back, seeking, end of file and the close callback; also
 *          memccpy and posix_fallocate
 * Author: Akilesh
 * Date: 2026-11-24
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MEMORY_BYTES 256
#define SCRATCH_FILE "cookie_stream_scratch.tmp"

typedef struct {
    char bytes[MEMORY_BYTES];
    size_t size;
    size_t position;
    int closed;
} Memory;

static ssize_t memory_read(void *cookie, char *buffer, size_t count)
{
    Memory *memory = cookie;
    size_t left = memory->size - memory->position;
    size_t take = count < left ? count : left;
    memcpy(buffer, memory->bytes + memory->position, take);
    memory->position += take;
    return (ssize_t)take;
}

static ssize_t memory_write(void *cookie, const char *buffer, size_t count)
{
    Memory *memory = cookie;
    size_t room = MEMORY_BYTES - memory->position;
    size_t take = count < room ? count : room;
    memcpy(memory->bytes + memory->position, buffer, take);
    memory->position += take;
    memory->size = memory->position > memory->size ? memory->position : memory->size;
    return (ssize_t)take;
}

static int memory_seek(void *cookie, off64_t *offset, int whence)
{
    Memory *memory = cookie;
    off64_t base = whence == SEEK_SET ? 0 : (whence == SEEK_CUR ? (off64_t)memory->position : (off64_t)memory->size);
    if (base + *offset < 0 || base + *offset > MEMORY_BYTES) {
        errno = EINVAL;
        return -1;
    }
    memory->position = (size_t)(base + *offset);
    *offset = (off64_t)memory->position;
    return 0;
}

static int memory_close(void *cookie)
{
    ((Memory *)cookie)->closed = 1;
    return 0;
}

static const cookie_io_functions_t FUNCTIONS = {memory_read, memory_write, memory_seek, memory_close};

static void check_cookie_stream(void)
{
    Memory memory;
    memset(&memory, 0, sizeof(memory));
    FILE *out = fopencookie(&memory, "w+", FUNCTIONS);
    printf("opened %d\n", out != NULL);
    fprintf(out, "first %d\nsecond %s\n", 1, "line");
    fputs("third\n", out);
    fflush(out);
    printf("stored %zu bytes: [%.*s]\n", memory.size, (int)memory.size - 1, memory.bytes);
    rewind(out);
    char line[64];
    while (fgets(line, sizeof(line), out) != NULL) {
        printf("read [%.*s]\n", (int)strcspn(line, "\n"), line);
    }
    printf("end of file %d error %d\n", feof(out) != 0, ferror(out) != 0);
    fseek(out, 6, SEEK_SET);
    printf("after seek, next char %c, tell %ld\n", fgetc(out), ftell(out));
    fclose(out);
    printf("close callback ran %d\n", memory.closed);
}

static void check_memccpy_and_fallocate(void)
{
    char target[16];
    memset(target, '.', sizeof(target));
    char *end = memccpy(target, "abc:def", ':', sizeof(target));
    printf("memccpy stopped after %d bytes\n", end != NULL ? (int)(end - target) : -1);
    printf("memccpy without the byte: %d\n", memccpy(target, "xyz", ':', 3) == NULL);
    int fd = open(SCRATCH_FILE, O_RDWR | O_CREAT | O_TRUNC, 0600);
    int status = posix_fallocate(fd, 0, 1000);
    struct stat info;
    fstat(fd, &info);
    printf("posix_fallocate status %d size %ld\n", status, (long)info.st_size);
    close(fd);
    remove(SCRATCH_FILE);
}

int main(void)
{
    check_cookie_stream();
    check_memccpy_and_fallocate();
    return 0;
}
