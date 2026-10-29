/*
 * File: shim/getopt.c
 * Purpose: getopt, getopt_long and getopt_long_only with GNU argument permutation, and the optarg, optind, opterr, optopt globals
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Non-option arguments are moved to the end as the scan goes (GNU behaviour) unless the option string
 * starts with '+' or POSIXLY_CORRECT is set. Setting optind to 0 restarts the scan.
 */

#include <string.h>

#include "libc_abi.h"
#include "startinfo.h"

LIBC_DATA(char *, optarg);
LIBC_DATA(int, optind) = 1;
LIBC_DATA(int, opterr) = 1;
LIBC_DATA(int, optopt) = '?';

#define NO_ARGUMENT 0
#define REQUIRED_ARGUMENT 1
#define OPTIONAL_ARGUMENT 2
#define END_OF_OPTIONS (-1)
#define IN_ORDER 1

typedef struct {
    const char *name;
    int has_argument;
    int *flag;
    int value;
} LongOption;

LIBC_FN char *sysv_getenv(const char *name);
LIBC_FN int sysv_fprintf(void *stream, const char *format, ...);
extern void *crossport_data_stderr;

static char **p_optarg = &crossport_data_optarg;
static int *p_optind = &crossport_data_optind;
static int *p_opterr = &crossport_data_opterr;
static int *p_optopt = &crossport_data_optopt;

/* The program's copies of the four globals, found on first use. */
static SYSV void locate_globals(void)
{
    static int is_done;
    if (!is_done) {
        p_optarg = (char **)shim_data_home(&crossport_data_optarg);
        p_optind = (int *)shim_data_home(&crossport_data_optind);
        p_opterr = (int *)shim_data_home(&crossport_data_opterr);
        p_optopt = (int *)shim_data_home(&crossport_data_optopt);
        is_done = 1;
    }
}

static const char *g_place = "";
static int g_nonoption_start = -1;
static int g_nonoption_end = -1;

enum { FLAG_PERMUTE = 1, FLAG_ALLARGS = 2, FLAG_LONGONLY = 4 };

static SYSV void report(const char *program, const char *message, const char *detail)
{
    if ((*p_opterr)) {
        sysv_fprintf(crossport_data_stderr, "%s: %s %s\n", program, message, detail);
    }
}

static SYSV void reverse(char **argv, int low, int high)
{
    for (high--; low < high; low++, high--) {
        char *swap = argv[low];
        argv[low] = argv[high];
        argv[high] = swap;
    }
}

/* Moves the block [start, middle) after [middle, end), keeping the order within each block. */
static SYSV void permute(int start, int middle, int end, char **argv)
{
    reverse(argv, start, middle);
    reverse(argv, middle, end);
    reverse(argv, start, end);
}

static SYSV int finish(int argc, char **argv)
{
    if (g_nonoption_end != -1) {
        permute(g_nonoption_start, g_nonoption_end, (*p_optind), argv);
        (*p_optind) -= g_nonoption_end - g_nonoption_start;
    } else if (g_nonoption_start != -1) {
        (*p_optind) = g_nonoption_start;
    }
    (void)argc;
    g_nonoption_start = g_nonoption_end = -1;
    g_place = "";
    return END_OF_OPTIONS;
}

/* A long option named at g_place (after "--"); returns the option value, or '?' / ':' on errors. */
static SYSV int long_option(int argc, char **argv, const char *options, const LongOption *longs, int *index, int flags)
{
    char *current = argv[(*p_optind)];
    const char *name = g_place;
    size_t length = strcspn(name, "=");
    const LongOption *match = NULL;
    int is_ambiguous = 0;
    for (const LongOption *at = longs; at != NULL && at->name != NULL; at++) {
        if (strncmp(at->name, name, length) == 0) {
            if (strlen(at->name) == length) {
                match = at;
                is_ambiguous = 0;
                break;
            }
            if (match != NULL && (match->has_argument != at->has_argument || match->flag != at->flag || match->value != at->value)) {
                is_ambiguous = 1;
            }
            match = match != NULL ? match : at;
        }
    }
    (void)options;
    (void)flags;
    (*p_optind)++;
    if (match == NULL || is_ambiguous) {
        report(argv[0], is_ambiguous ? "option is ambiguous:" : "unrecognized option", current);
        (*p_optopt) = 0;
        g_place = "";
        return '?';
    }
    if (name[length] == '=') {
        if (match->has_argument == NO_ARGUMENT) {
            report(argv[0], "option doesn't take an argument:", match->name);
            (*p_optopt) = match->value;
            g_place = "";
            return '?';
        }
        (*p_optarg) = (char *)name + length + 1;
    } else if (match->has_argument == REQUIRED_ARGUMENT) {
        if ((*p_optind) < argc) {
            (*p_optarg) = argv[(*p_optind)++];
        } else {
            report(argv[0], "option requires an argument:", match->name);
            (*p_optopt) = match->value;
            g_place = "";
            return ':';
        }
    }
    g_place = "";
    if (index != NULL) {
        *index = (int)(match - longs);
    }
    if (match->flag != NULL) {
        *match->flag = match->value;
        return 0;
    }
    return match->value;
}

/* A short option letter at g_place. */
static SYSV int short_option(int argc, char **argv, const char *options)
{
    int letter = (unsigned char)*g_place++;
    const char *spec = letter == ':' ? NULL : strchr(options, letter);
    int is_last = g_place[0] == '\0';
    if (spec == NULL) {
        (*p_optopt) = letter;
        (*p_optind) += is_last;
        g_place = is_last ? "" : g_place;
        report(argv[0], "invalid option --", (char[]){(char)letter, '\0'});
        return '?';
    }
    if (spec[1] != ':') {
        (*p_optind) += is_last;
        return letter;
    }
    if (!is_last) {
        (*p_optarg) = (char *)g_place;
        (*p_optind)++;
        g_place = "";
        return letter;
    }
    (*p_optind)++;
    g_place = "";
    if (spec[2] == ':') {
        return letter;
    }
    if ((*p_optind) < argc) {
        (*p_optarg) = argv[(*p_optind)++];
        return letter;
    }
    (*p_optopt) = letter;
    report(argv[0], "option requires an argument --", (char[]){(char)letter, '\0'});
    return options[0] == ':' ? ':' : '?';
}

static SYSV int scan(int argc, char **argv, const char *options, const LongOption *longs, int *index, int flags)
{
    if ((*p_optind) == 0) {
        (*p_optind) = 1;
        g_place = "";
        g_nonoption_start = g_nonoption_end = -1;
    }
    (*p_optarg) = NULL;
    for (;;) {
        if (g_place[0] == '\0') {
            if ((*p_optind) >= argc) {
                return finish(argc, argv);
            }
            const char *word = argv[(*p_optind)];
            if (word[0] != '-' || word[1] == '\0') {
                if (!(flags & FLAG_PERMUTE)) {
                    return END_OF_OPTIONS;
                }
                if (g_nonoption_start == -1) {
                    g_nonoption_start = (*p_optind);
                } else if (g_nonoption_end != -1) {
                    permute(g_nonoption_start, g_nonoption_end, (*p_optind), argv);
                    g_nonoption_start = (*p_optind) - (g_nonoption_end - g_nonoption_start);
                    g_nonoption_end = -1;
                }
                (*p_optind)++;
                continue;
            }
            if (g_nonoption_start != -1 && g_nonoption_end == -1) {
                g_nonoption_end = (*p_optind);
            }
            if (word[1] == '-' && word[2] == '\0') {
                (*p_optind)++;
                return finish(argc, argv);
            }
            g_place = word + 1;
            if (word[1] == '-') {
                g_place = word + 2;
                return long_option(argc, argv, options, longs, index, flags);
            }
            if (longs != NULL && (flags & FLAG_LONGONLY)) {
                return long_option(argc, argv, options, longs, index, flags);
            }
        }
        return short_option(argc, argv, options);
    }
}

static SYSV int dispatch(int argc, char **argv, const char *options, const LongOption *longs, int *index, int is_long_only)
{
    locate_globals();
    int flags = is_long_only ? FLAG_LONGONLY : 0;
    if (options != NULL && options[0] == '+') {
        options++;
    } else if (sysv_getenv("POSIXLY_CORRECT") == NULL) {
        flags |= FLAG_PERMUTE;
    }
    return scan(argc, argv, options, longs, index, flags);
}

LIBC_FN int sysv_getopt(int argc, char *const argv[], const char *options)
{
    return dispatch(argc, (char **)argv, options, NULL, NULL, 0);
}

LIBC_FN int sysv_getopt_long(int argc, char *const argv[], const char *options, const LongOption *longs, int *index)
{
    return dispatch(argc, (char **)argv, options, longs, index, 0);
}

LIBC_FN int sysv_getopt_long_only(int argc, char *const argv[], const char *options, const LongOption *longs, int *index)
{
    return dispatch(argc, (char **)argv, options, longs, index, 1);
}
