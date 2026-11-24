/*
 * File: tests/corpus/argv_name/argv_name.c
 * Purpose: Checks that argv[0] has the form a Linux program expects, so the usual way of finding a program's own short name works
 * Author: Akilesh
 * Date: 2026-11-24
 */

#define _GNU_SOURCE
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>

#define SHORT_NAME "argv_name"

static const char *last_component(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

int main(int argc, char **argv)
{
    char copy[4096];
    snprintf(copy, sizeof(copy), "%s", argv[0]);
    printf("argc %d\n", argc);
    printf("last component of argv[0]: %s\n", last_component(argv[0]));
    printf("basename(argv[0]): %s\n", basename(copy));
    printf("short name matches %d\n", strcmp(program_invocation_short_name, SHORT_NAME) == 0);
    printf("argv[0] has a backslash %d\n", strchr(argv[0], '\\') != NULL);
    printf("argv[0] ends with .exe %d\n", strlen(argv[0]) > 4 && strcmp(argv[0] + strlen(argv[0]) - 4, ".exe") == 0);
    return 0;
}
