/*
 * File: shim/netdb.c
 * Purpose: Name service lookups: gethostbyname, gethostbyaddr_r and getservbyname, answered for loopback and numeric names only
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

LIBC_FN uint32_t sysv_inet_addr(const char *text);

/* Name services: only the numeric loopback and the machine's own name are answered. */
typedef struct {
    char *name;
    char **aliases;
    int addrtype;
    int length;
    char **addresses;
} HostEnt;

typedef struct {
    char *name;
    char **aliases;
    int port;
    char *protocol;
} ServEnt;

static HostEnt g_host;
static char g_host_name[256];
static char *g_no_aliases[1];
static uint8_t g_address[4];
static char *g_addresses[2];

static int g_h_errno;

LIBC_FN int *sysv___h_errno_location(void)
{
    return &g_h_errno;
}

LIBC_FN HostEnt *sysv_gethostbyname(const char *name)
{
    if (strcmp(name, "localhost") != 0 && !(name[0] >= '0' && name[0] <= '9')) {
        g_h_errno = 1;
        return NULL;
    }
    uint32_t numeric = name[0] >= '0' && name[0] <= '9' ? sysv_inet_addr(name) : 0x0100007fu;
    if (numeric == 0xffffffffu) {
        g_h_errno = 1;
        return NULL;
    }
    memcpy(g_address, &numeric, sizeof(g_address));
    strncpy(g_host_name, name, sizeof(g_host_name) - 1);
    g_addresses[0] = (char *)g_address;
    g_addresses[1] = NULL;
    g_host = (HostEnt){.name = g_host_name, .aliases = g_no_aliases, .addrtype = 2, .length = 4, .addresses = g_addresses};
    return &g_host;
}

LIBC_FN int sysv_gethostbyname_r(const char *name, HostEnt *result, char *buffer, size_t size, HostEnt **out, int *error)
{
    (void)buffer;
    (void)size;
    HostEnt *found = sysv_gethostbyname(name);
    *out = found;
    if (found != NULL) {
        *result = *found;
    }
    *error = found != NULL ? 0 : 1;
    return found != NULL ? 0 : LINUX_ENOENT;
}

LIBC_FN int sysv_gethostbyaddr_r(const void *address, uint32_t length, int family, HostEnt *result, char *buffer, size_t size,
                                 HostEnt **out, int *error)
{
    (void)address;
    (void)length;
    (void)family;
    (void)result;
    (void)buffer;
    (void)size;
    *out = NULL;
    *error = 1;
    return LINUX_ENOENT;
}

LIBC_FN ServEnt *sysv_getservbyname(const char *name, const char *protocol)
{
    (void)name;
    (void)protocol;
    return NULL;
}

LIBC_FN int sysv_getservbyname_r(const char *name, const char *protocol, ServEnt *result, char *buffer, size_t size, ServEnt **out)
{
    (void)name;
    (void)protocol;
    (void)result;
    (void)buffer;
    (void)size;
    *out = NULL;
    return LINUX_ENOENT;
}
