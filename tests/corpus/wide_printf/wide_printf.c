/*
 * File: tests/corpus/wide_printf/wide_printf.c
 * Purpose: Compares the wide printf family (swprintf, wprintf, fwprintf), wide string conversions and long double formatting with glibc
 * Author: Akilesh
 * Date: 2026-11-24
 */

#define _GNU_SOURCE
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define BUFFER_CHARS 64
#define WIDE_FILE "wide_printf_out.tmp"

static void show_wide(const char *label, const wchar_t *text)
{
    printf("%s: [%ls] length %d\n", label, text, (int)wcslen(text));
}

static void check_swprintf(void)
{
    wchar_t buffer[BUFFER_CHARS];
    int count = swprintf(buffer, BUFFER_CHARS, L"%ls %d|%-6ls|%6ls|%.3ls|%c|%lc|%5.2f", L"wide", 42, L"ab", L"cd", L"abcdef", 'x', (wint_t)L'y', 3.14159);
    printf("swprintf returned %d\n", count);
    show_wide("formatted", buffer);
    wchar_t small[6];
    count = swprintf(small, 6, L"%ls", L"truncated text");
    printf("truncating swprintf returned %d\n", count);
    wchar_t months[BUFFER_CHARS];
    swprintf(months, BUFFER_CHARS, L"%*ls %d", 12, L"March", 2024);
    show_wide("centered", months);
    wchar_t names[BUFFER_CHARS];
    swprintf(names, BUFFER_CHARS, L"%-2.2ls %-2.2ls %-2.2ls", L"Sunday", L"Monday", L"Tuesday");
    show_wide("day names", names);
    wchar_t narrow_source[BUFFER_CHARS];
    swprintf(narrow_source, BUFFER_CHARS, L"%s and %s", "narrow", "strings");
    show_wide("narrow arguments", narrow_source);
}

/* Wide output goes to a file of its own: glibc refuses wide output on a stream that already carried bytes, so stdout cannot be used. */
static void check_wide_output(void)
{
    FILE *file = fopen(WIDE_FILE, "w");
    int count = fwprintf(file, L"fwprintf: %ls %d|%5.1f|\n", L"line", 7, 2.25);
    int more = fwprintf(file, L"second %lc\n", (wint_t)L'z');
    fclose(file);
    char text[BUFFER_CHARS * 2] = "";
    file = fopen(WIDE_FILE, "r");
    size_t got = fread(text, 1, sizeof(text) - 1, file);
    text[got] = '\0';
    fclose(file);
    remove(WIDE_FILE);
    printf("fwprintf returned %d and %d, wrote %d bytes: [%s]\n", count, more, (int)got, text);
}

static void check_long_double(void)
{
    long double sum = 0.0L;
    for (int i = 1; i <= 10; i++) {
        sum += (long double)i / 3.0L;
    }
    char text[BUFFER_CHARS];
    snprintf(text, sizeof(text), "%Lg %Lf %.10Lg", sum, sum, sqrtl(2.0L));
    printf("long double: %s\n", text);
    printf("expl %.6Lf logl %.6Lf roundl %.1Lf\n", expl(1.0L), logl(10.0L), roundl(2.5L));
    long double whole;
    long double fraction = modfl(7.25L, &whole);
    printf("modfl %.2Lf %.2Lf\n", whole, fraction);
}

static void check_rand_r(void)
{
    unsigned seed = 12345;
    printf("rand_r %d %d %d\n", rand_r(&seed), rand_r(&seed), rand_r(&seed));
}

int main(void)
{
    setlocale(LC_ALL, "C.UTF-8");
    check_swprintf();
    check_wide_output();
    check_long_double();
    check_rand_r();
    return 0;
}
