/*
 * File: shim/progname.c
 * Purpose: The program name globals, time-zone data, and the small runtime constants programs read
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

#define SIGRTMIN_VALUE 34
#define SIGRTMAX_VALUE 64

LIBC_FN c_long sysv_sysconf(int name);

/* The program's own name, set when the program starts (see crossport_libc.c). */
LIBC_DATA(char *, __progname);

LIBC_DATA(char *, __progname_full);

LIBC_DATA(char *, program_invocation_name);

LIBC_DATA(char *, program_invocation_short_name);

LIBC_DATA(int64_t, __timezone);

LIBC_DATA(char *, __tzname[2]) = {"UTC", "UTC"};

LIBC_DATA(int64_t, timezone);

LIBC_DATA(int, daylight);

LIBC_DATA(char *, tzname[2]) = {"UTC", "UTC"};


void shim_set_program_name(char *full)
{
    char *base = full;
    for (char *at = full; *at != '\0'; at++) {
        base = (*at == '/' || *at == '\\') ? at + 1 : base;
    }
    crossport_data___progname = base;
    crossport_data___progname_full = full;
    crossport_data_program_invocation_name = full;
    crossport_data_program_invocation_short_name = base;
}

LIBC_FN int sysv___libc_current_sigrtmin(void)
{
    return SIGRTMIN_VALUE;
}

LIBC_FN int sysv___libc_current_sigrtmax(void)
{
    return SIGRTMAX_VALUE;
}

LIBC_FN c_long sysv___sysconf(int name)
{
    return sysv_sysconf(name);
}

LIBC_FN void sysv_tzset(void)
{
}
