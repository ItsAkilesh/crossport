/*
 * File: shim/progname.c
 * Purpose: The program name globals, time-zone data, and the small runtime constants programs read
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"
#include "startinfo.h"
#include "time_internal.h"

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
    *(char **)shim_data_home(&crossport_data___progname) = base;
    *(char **)shim_data_home(&crossport_data___progname_full) = full;
    *(char **)shim_data_home(&crossport_data_program_invocation_name) = full;
    *(char **)shim_data_home(&crossport_data_program_invocation_short_name) = base;
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

extern int64_t crossport_data_timezone;
extern int64_t crossport_data___timezone;
extern int crossport_data_daylight;
extern int crossport_data___daylight;
extern char *crossport_data_tzname[2];
extern char *crossport_data___tzname[2];

/* tzset(3): the zone variables (timezone, daylight, tzname) for the zone in force. Setting them is not required for localtime to work. */
LIBC_FN void sysv_tzset(void)
{
    int64_t seconds_west;
    int has_daylight;
    const char *standard;
    const char *daylight;
    shim_zone_variables(&seconds_west, &has_daylight, &standard, &daylight);
    *(int64_t *)shim_data_home(&crossport_data_timezone) = seconds_west;
    *(int64_t *)shim_data_home(&crossport_data___timezone) = seconds_west;
    *(int *)shim_data_home(&crossport_data_daylight) = has_daylight;
    *(int *)shim_data_home(&crossport_data___daylight) = has_daylight;
    char **names = (char **)shim_data_home(&crossport_data_tzname);
    char **double_names = (char **)shim_data_home(&crossport_data___tzname);
    names[0] = double_names[0] = (char *)standard;
    names[1] = double_names[1] = (char *)daylight;
}
