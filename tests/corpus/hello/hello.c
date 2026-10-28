/*
 * File: tests/corpus/hello/hello.c
 * Purpose: Milestone 0 corpus program; fixed stdout, stderr and exit code for differential testing
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include <stdio.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define HELLO_EXIT_CODE 3

/* Windows streams default to text mode, which rewrites "\n" as "\r\n".
 * Binary mode keeps the bytes identical to the Linux build. */
static void set_binary_streams(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
}

int main(void)
{
    set_binary_streams();
    fputs("hello from crossport\n", stdout);
    fputs("hello on stderr\n", stderr);
    return HELLO_EXIT_CODE;
}
