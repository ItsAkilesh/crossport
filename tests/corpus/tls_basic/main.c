/*
 * File: tests/corpus/tls_basic/main.c
 * Purpose: Milestone 3 corpus program (single thread): local-exec, initial-exec, general-dynamic and local-dynamic TLS plus stack canaries
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include <stdio.h>

#include "libtls.h"
#include "numfmt.h"

#define NUMBER_BUFFER_SIZE 32
#define EXE_INITIAL 5
#define EXE_INCREMENT 10
#define ZEROED_INDEX 3
#define ZEROED_VALUE 9
#define BUFFER_SIZE 32

__thread int exe_initialized = EXE_INITIAL;
__thread char exe_buffer[BUFFER_SIZE];
__thread long exe_zeroed[8];

static void print_number(const char *label, long value)
{
    char text[NUMBER_BUFFER_SIZE];
    puts(label);
    format_long(text, sizeof(text), value);
    puts(text);
}

/* Compiled with -fstack-protector-all, so this frame reads and checks the canary. */
static long checksum(const char *text)
{
    char copy[BUFFER_SIZE];
    long sum = 0;
    size_t length = 0;
    while (text[length] != '\0' && length + 1 < sizeof(copy)) {
        copy[length] = text[length];
        length++;
    }
    for (size_t i = 0; i < length; i++) {
        sum += copy[i];
    }
    return sum;
}

static void fill_buffer(void)
{
    const char word[] = "tls";
    size_t i = 0;
    for (; word[i] != '\0'; i++) {
        exe_buffer[i] = word[i];
    }
    exe_buffer[i] = '\0';
}

int main(void)
{
    print_number("exe initialized:", exe_initialized);
    exe_initialized += EXE_INCREMENT;
    print_number("exe after increment:", exe_initialized);

    print_number("exe zeroed before:", exe_zeroed[ZEROED_INDEX]);
    exe_zeroed[ZEROED_INDEX] = ZEROED_VALUE;
    print_number("exe zeroed after:", exe_zeroed[ZEROED_INDEX]);

    fill_buffer();
    puts(exe_buffer);

    print_number("lib initial:", lib_get());
    lib_set(lib_get() + 1);
    print_number("lib after set:", lib_get());

    print_number("lib exported via exe:", lib_exported);
    lib_exported = 8;
    print_number("lib sees exported:", lib_read_exported());

    print_number("hidden sum:", lib_hidden_sum());
    print_number("hidden sum again:", lib_hidden_sum());
    print_number("checksum:", checksum("crossport"));
    return 0;
}
