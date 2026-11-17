/*
 * File: tests/corpus/gnu_ext/gnu_ext.c
 * Purpose: Exercise the GNU C library extensions that stock GNU tools call (rawmemchr, mbrtoc32, freopen, __freading, __fwriting,
 *          __fpurge, error, sysinfo) so Linux and the translated program can be compared
 * Author: Akilesh
 * Date: 2026-11-17
 */

#define _GNU_SOURCE
#include <errno.h>
#include <error.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <uchar.h>

#define FIRST_FILE "gnu_ext_first.tmp"
#define SECOND_FILE "gnu_ext_second.tmp"
#define ERROR_FILE "gnu_ext_error.tmp"
#define READ_BYTES 256
#define UTF8_TEXT "\xc3\xa4\xe2\x82\xac\xf0\x9f\x98\x80"

static void read_whole(const char *path, char *out, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    size_t got = file != NULL ? fread(out, 1, capacity - 1, file) : 0;
    out[got] = '\0';
    if (file != NULL) {
        fclose(file);
    }
}

static void check_rawmemchr(void)
{
    const char text[] = "abcdefg";
    printf("rawmemchr offset %td\n", (const char *)rawmemchr(text, 'e') - text);
}

static void check_mbrtoc32(void)
{
    const char *at = UTF8_TEXT;
    const char *end = at + strlen(at);
    mbstate_t state = {0};
    char32_t code = 0;
    const char *chosen = setlocale(LC_ALL, "C.UTF-8");
    printf("locale chosen %s, query %s\n", chosen != NULL ? chosen : "(null)", setlocale(LC_ALL, NULL));
    while (at < end) {
        size_t used = mbrtoc32(&code, at, (size_t)(end - at), &state);
        printf("mbrtoc32 used %zu code %x\n", used, (unsigned)code);
        at += used;
    }
}

static void check_direction(void)
{
    FILE *writer = fopen(FIRST_FILE, "w");
    printf("writer: reading %d writing %d\n", __freading(writer) != 0, __fwriting(writer) != 0);
    fputs("direction", writer);
    fclose(writer);
    FILE *reader = fopen(FIRST_FILE, "r");
    printf("reader: reading %d writing %d\n", __freading(reader) != 0, __fwriting(reader) != 0);
    fclose(reader);
}

static void check_purge_and_reopen(void)
{
    char text[READ_BYTES];
    FILE *file = fopen(FIRST_FILE, "w");
    fputs("discard me", file);
    __fpurge(file);
    fputs("keep", file);
    fclose(file);
    read_whole(FIRST_FILE, text, sizeof(text));
    printf("after purge: [%s]\n", text);
    file = fopen(FIRST_FILE, "w");
    file = freopen(SECOND_FILE, "w", file);
    fputs("second", file);
    fclose(file);
    read_whole(FIRST_FILE, text, sizeof(text));
    printf("first after reopen: [%s]\n", text);
    read_whole(SECOND_FILE, text, sizeof(text));
    printf("second after reopen: [%s]\n", text);
}

static void check_error(const char *program)
{
    char text[READ_BYTES];
    char prefix[READ_BYTES];
    snprintf(prefix, sizeof(prefix), "%s: ", program);
    if (freopen(ERROR_FILE, "w", stderr) == NULL) {
        printf("freopen of stderr failed\n");
        return;
    }
    error(0, ENOENT, "cannot open %s", "x.txt");
    error(0, 0, "plain %d", 5);
    fflush(stderr);
    read_whole(ERROR_FILE, text, sizeof(text));
    size_t prefix_length = strlen(prefix);
    for (char *line = text; *line != '\0';) {
        char *newline = strchr(line, '\n');
        size_t length = newline != NULL ? (size_t)(newline - line) : strlen(line);
        int has_prefix = strncmp(line, prefix, prefix_length) == 0;
        size_t skip = has_prefix ? prefix_length : 0;
        printf("error line has the program name %d: [%.*s]\n", has_prefix, (int)(length - skip), line + skip);
        line += length + (newline != NULL ? 1 : 0);
    }
}

static void check_sysinfo(void)
{
    struct sysinfo info;
    int ok = sysinfo(&info) == 0;
    printf("sysinfo ok %d ram %d free-le-total %d unit %d uptime %d procs %d\n", ok, info.totalram > 0, info.freeram <= info.totalram,
           info.mem_unit >= 1, info.uptime >= 0, info.procs > 0);
}

int main(int argc, char **argv)
{
    (void)argc;
    check_rawmemchr();
    check_mbrtoc32();
    check_direction();
    check_purge_and_reopen();
    check_sysinfo();
    fflush(stdout);
    check_error(argv[0]);
    remove(FIRST_FILE);
    remove(SECOND_FILE);
    remove(ERROR_FILE);
    return 0;
}
