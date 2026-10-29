/*
 * File: shim/compat.c
 * Purpose: Small glibc entry points programs expect: __libc_single_threaded, secure_getenv and the gettext family
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include "libc_abi.h"

LIBC_FN char *sysv_getenv(const char *name);

/*
 * glibc's flag that lets a C++ runtime skip atomic operations while no second thread exists. The shim
 * supports threads, so it is always false, which is always safe.
 */
LIBC_DATA(char, __libc_single_threaded);

LIBC_FN char *sysv_secure_getenv(const char *name)
{
    return sysv_getenv(name);
}

/* No message catalogs are installed, so every lookup returns its source text, as glibc does without one. */
LIBC_FN char *sysv_gettext(const char *message)
{
    return (char *)message;
}

LIBC_FN char *sysv_dgettext(const char *domain, const char *message)
{
    (void)domain;
    return (char *)message;
}

LIBC_FN char *sysv_dcgettext(const char *domain, const char *message, int category)
{
    (void)domain;
    (void)category;
    return (char *)message;
}

LIBC_FN char *sysv_ngettext(const char *singular, const char *plural, unsigned long count)
{
    return (char *)(count == 1 ? singular : plural);
}

LIBC_FN char *sysv_textdomain(const char *domain)
{
    return (char *)(domain != NULL ? domain : "messages");
}

LIBC_FN char *sysv_bindtextdomain(const char *domain, const char *directory)
{
    (void)domain;
    return (char *)directory;
}
