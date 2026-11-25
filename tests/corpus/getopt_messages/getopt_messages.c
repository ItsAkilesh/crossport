/*
 * File: tests/corpus/getopt_messages/getopt_messages.c
 * Purpose: Compares the option parser's results and its error messages with glibc: short options, long options, abbreviations, ambiguity,
 *          missing and unexpected arguments, and "--word" given to plain getopt
 * Author: Akilesh
 * Date: 2026-11-24
 */

#define _GNU_SOURCE
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ERROR_FILE "getopt_messages_error.tmp"
#define MAX_ARGUMENTS 8

typedef struct {
    const char *short_options;
    int uses_long;
    const char *arguments[MAX_ARGUMENTS];
} Case;

static const struct option LONG_OPTIONS[] = {
    {"verbose", no_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {"need", required_argument, NULL, 'n'},
    {"maybe", optional_argument, NULL, 'm'},
    {"plain", no_argument, NULL, 'p'},
    {NULL, 0, NULL, 0},
};

static const Case CASES[] = {
    {"ab:", 0, {"prog", "-a", "-b", "value", "rest"}},
    {"ab:", 0, {"prog", "-x"}},
    {"ab:", 0, {"prog", "-b"}},
    {"ab:", 0, {"prog", "--version"}},
    {"ab:", 0, {"prog", "--"}},
    {"ab:", 0, {"prog", "file", "-a"}},
    {"+ab:", 0, {"prog", "file", "-a"}},
    {":ab:", 0, {"prog", "-b"}},
    {"vVn:m::p", 1, {"prog", "--verbose", "--need", "x", "-p"}},
    {"vVn:m::p", 1, {"prog", "--unknown"}},
    {"vVn:m::p", 1, {"prog", "--ver"}},
    {"vVn:m::p", 1, {"prog", "--verb"}},
    {"vVn:m::p", 1, {"prog", "--plain=1"}},
    {"vVn:m::p", 1, {"prog", "--need"}},
    {"vVn:m::p", 1, {"prog", "--need=abc", "--maybe=q", "--maybe"}},
    {"vVn:m::p", 1, {"prog", "-z"}},
    {"vVn:m::p", 1, {"prog", "-n"}},
};

static void run(const Case *test)
{
    char *argv[MAX_ARGUMENTS + 1] = {NULL};
    int argc = 0;
    while (argc < MAX_ARGUMENTS && test->arguments[argc] != NULL) {
        argv[argc] = strdup(test->arguments[argc]);
        argc++;
    }
    optind = 0;
    opterr = 1;
    printf("options [%s] arguments:", test->short_options);
    for (int i = 1; i < argc; i++) {
        printf(" %s", argv[i]);
    }
    printf("\n");
    fflush(stdout);
    int result;
    int index = -1;
    while ((result = test->uses_long ? getopt_long(argc, argv, test->short_options, LONG_OPTIONS, &index)
                                     : getopt(argc, argv, test->short_options)) != -1) {
        printf("  result %d optopt %d optarg [%s]\n", result, optopt, optarg != NULL ? optarg : "(null)");
    }
    printf("  optind %d first rest [%s]\n", optind, optind < argc ? argv[optind] : "(none)");
}

static void show_errors(void)
{
    char text[4096] = "";
    FILE *file = fopen(ERROR_FILE, "rb");
    size_t got = file != NULL ? fread(text, 1, sizeof(text) - 1, file) : 0;
    text[got] = '\0';
    if (file != NULL) {
        fclose(file);
    }
    printf("--- messages ---\n%s", text);
}

int main(void)
{
    if (freopen(ERROR_FILE, "w", stderr) == NULL) {
        return 1;
    }
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        fputs("[case]\n", stderr);
        run(&CASES[i]);
    }
    fflush(stderr);
    show_errors();
    remove(ERROR_FILE);
    return 0;
}
