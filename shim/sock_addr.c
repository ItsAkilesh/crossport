/*
 * File: shim/sock_addr.c
 * Purpose: Socket addresses and names: sockaddr conversion, getaddrinfo, getnameinfo, inet_* and byte-order functions
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <stddef.h>
#include <string.h>

#include "sock.h"

#include <afunix.h>

#define UNIX_PATH_MAX 108

/* Linux struct addrinfo (48 bytes); Winsock orders and sizes its fields differently. */
typedef struct LinuxAddrInfo {
    int32_t ai_flags;
    int32_t ai_family;
    int32_t ai_socktype;
    int32_t ai_protocol;
    uint32_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct LinuxAddrInfo *ai_next;
} LinuxAddrInfo;

/* Linux and Winsock getaddrinfo flags that differ. */
enum { LINUX_AI_NUMERICSERV = 0x400, LINUX_AI_ADDRCONFIG = 0x20, SAME_FLAGS_MASK = 0x7 };
enum {
    LINUX_EAI_BADFLAGS = -1, LINUX_EAI_NONAME = -2, LINUX_EAI_AGAIN = -3, LINUX_EAI_FAIL = -4, LINUX_EAI_FAMILY = -6, LINUX_EAI_SOCKTYPE = -7,
    LINUX_EAI_SERVICE = -8, LINUX_EAI_MEMORY = -10, LINUX_EAI_SYSTEM = -11
};

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);

int shim_family_to_windows(int linux_family)
{
    return linux_family == LINUX_AF_INET6 ? AF_INET6 : linux_family;
}

int shim_family_to_linux(int windows_family)
{
    return windows_family == AF_INET6 ? LINUX_AF_INET6 : windows_family;
}

/* A Unix-domain address names a file, so its path goes through the same mapping as any other path. */
static int unix_to_windows(const void *linux_address, int length, SOCKADDR_STORAGE *out)
{
    const char *path = (const char *)linux_address + sizeof(uint16_t);
    int path_bytes = length - (int)sizeof(uint16_t);
    if (path_bytes <= 0 || path[0] == '\0') {
        return shim_fail(LINUX_EINVAL); /* abstract-namespace sockets have no file to map */
    }
    SOCKADDR_UN *target = (SOCKADDR_UN *)out;
    char text[UNIX_PATH_MAX];
    size_t copied = strnlen(path, (size_t)path_bytes < sizeof(text) - 1 ? (size_t)path_bytes : sizeof(text) - 1);
    memcpy(text, path, copied);
    text[copied] = '\0';
    target->sun_family = AF_UNIX;
    if (shim_path_to_windows(text, target->sun_path, sizeof(target->sun_path)) != 0) {
        return -1;
    }
    return (int)(offsetof(SOCKADDR_UN, sun_path) + strlen(target->sun_path) + 1);
}

int shim_sockaddr_to_windows(const void *linux_address, int length, SOCKADDR_STORAGE *out)
{
    if (linux_address == NULL || length < (int)sizeof(uint16_t) || length > (int)sizeof(*out)) {
        return shim_fail(LINUX_EINVAL);
    }
    memset(out, 0, sizeof(*out));
    if (((const uint16_t *)linux_address)[0] == LINUX_AF_UNIX) {
        return unix_to_windows(linux_address, length, out);
    }
    memcpy(out, linux_address, (size_t)length);
    out->ss_family = (ADDRESS_FAMILY)shim_family_to_windows(((const uint16_t *)linux_address)[0]);
    return length;
}

int shim_sockaddr_to_linux(const SOCKADDR *windows_address, int length, void *out, size_t capacity)
{
    if (length < 0 || (size_t)length > capacity) {
        return shim_fail(LINUX_EINVAL);
    }
    memcpy(out, windows_address, (size_t)length);
    ((uint16_t *)out)[0] = (uint16_t)shim_family_to_linux(windows_address->sa_family);
    return length;
}

LIBC_FN uint16_t sysv_htons(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

LIBC_FN uint16_t sysv_ntohs(uint16_t value)
{
    return sysv_htons(value);
}

LIBC_FN uint32_t sysv_htonl(uint32_t value)
{
    return __builtin_bswap32(value);
}

LIBC_FN uint32_t sysv_ntohl(uint32_t value)
{
    return __builtin_bswap32(value);
}

LIBC_FN int sysv_inet_pton(int family, const char *text, void *out)
{
    int windows_family = shim_family_to_windows(family);
    if (windows_family != AF_INET && windows_family != AF_INET6) {
        return shim_fail(LINUX_EAFNOSUPPORT);
    }
    return InetPtonA(windows_family, text, out);
}

LIBC_FN const char *sysv_inet_ntop(int family, const void *address, char *buffer, uint32_t size)
{
    int windows_family = shim_family_to_windows(family);
    if (windows_family != AF_INET && windows_family != AF_INET6) {
        shim_set_errno(LINUX_EAFNOSUPPORT);
        return NULL;
    }
    if (InetNtopA(windows_family, address, buffer, size) == NULL) {
        shim_fail_from_win();
        return NULL;
    }
    return buffer;
}

LIBC_FN uint32_t sysv_inet_addr(const char *text)
{
    uint32_t address = 0;
    return InetPtonA(AF_INET, text, &address) == 1 ? address : 0xffffffffu;
}

LIBC_FN int sysv_inet_aton(const char *text, uint32_t *out)
{
    return InetPtonA(AF_INET, text, out) == 1;
}

LIBC_FN char *sysv_inet_ntoa(uint32_t address)
{
    static char text[INET_ADDRSTRLEN];
    return InetNtopA(AF_INET, &address, text, sizeof(text)) != NULL ? text : NULL;
}

LIBC_FN int sysv_gethostname(char *name, size_t size)
{
    shim_winsock_init();
    if (gethostname(name, (int)size) != 0) {
        return shim_fail_from_win();
    }
    return 0;
}

static int translate_error(int windows_error)
{
    switch (windows_error) {
    case WSAHOST_NOT_FOUND:
    case WSANO_DATA:
        return LINUX_EAI_NONAME;
    case WSATRY_AGAIN:
        return LINUX_EAI_AGAIN;
    case WSANO_RECOVERY:
        return LINUX_EAI_FAIL;
    case WSAEAFNOSUPPORT:
        return LINUX_EAI_FAMILY;
    case WSAESOCKTNOSUPPORT:
        return LINUX_EAI_SOCKTYPE;
    case WSATYPE_NOT_FOUND:
        return LINUX_EAI_SERVICE;
    case WSAEINVAL:
        return LINUX_EAI_BADFLAGS;
    case WSA_NOT_ENOUGH_MEMORY:
        return LINUX_EAI_MEMORY;
    default:
        return LINUX_EAI_SYSTEM;
    }
}

static int flags_to_windows(int flags)
{
    int converted = flags & SAME_FLAGS_MASK;
    converted |= (flags & LINUX_AI_NUMERICSERV) ? AI_NUMERICSERV : 0;
    converted |= (flags & LINUX_AI_ADDRCONFIG) ? AI_ADDRCONFIG : 0;
    return converted;
}

/* One Linux addrinfo node, address and name in a single allocation, copied from a Winsock node. */
static SYSV LinuxAddrInfo *convert_node(const ADDRINFOA *source)
{
    size_t name_bytes = source->ai_canonname != NULL ? strlen(source->ai_canonname) + 1 : 0;
    size_t total = sizeof(LinuxAddrInfo) + sizeof(SOCKADDR_STORAGE) + name_bytes;
    LinuxAddrInfo *node = (LinuxAddrInfo *)sysv_malloc(total);
    if (node == NULL) {
        return NULL;
    }
    memset(node, 0, total);
    node->ai_flags = source->ai_flags;
    node->ai_family = shim_family_to_linux(source->ai_family);
    node->ai_socktype = source->ai_socktype;
    node->ai_protocol = source->ai_protocol;
    node->ai_addrlen = (uint32_t)source->ai_addrlen;
    node->ai_addr = (struct sockaddr *)(node + 1);
    shim_sockaddr_to_linux(source->ai_addr, (int)source->ai_addrlen, node->ai_addr, sizeof(SOCKADDR_STORAGE));
    if (name_bytes != 0) {
        node->ai_canonname = (char *)node->ai_addr + sizeof(SOCKADDR_STORAGE);
        memcpy(node->ai_canonname, source->ai_canonname, name_bytes);
    }
    return node;
}

LIBC_FN void sysv_freeaddrinfo(LinuxAddrInfo *list)
{
    while (list != NULL) {
        LinuxAddrInfo *next = list->ai_next;
        sysv_free(list);
        list = next;
    }
}

LIBC_FN int sysv_getaddrinfo(const char *node, const char *service, const LinuxAddrInfo *hints, LinuxAddrInfo **result)
{
    shim_winsock_init();
    ADDRINFOA windows_hints = {0};
    ADDRINFOA *found = NULL;
    if (hints != NULL) {
        windows_hints.ai_flags = flags_to_windows(hints->ai_flags);
        windows_hints.ai_family = shim_family_to_windows(hints->ai_family);
        windows_hints.ai_socktype = hints->ai_socktype;
        windows_hints.ai_protocol = hints->ai_protocol;
    }
    int status = getaddrinfo(node, service, hints != NULL ? &windows_hints : NULL, &found);
    if (status != 0) {
        return translate_error(status);
    }
    LinuxAddrInfo *head = NULL;
    LinuxAddrInfo **tail = &head;
    for (const ADDRINFOA *at = found; at != NULL; at = at->ai_next) {
        *tail = convert_node(at);
        if (*tail == NULL) {
            sysv_freeaddrinfo(head);
            freeaddrinfo(found);
            return LINUX_EAI_MEMORY;
        }
        tail = &(*tail)->ai_next;
    }
    freeaddrinfo(found);
    *result = head;
    return 0;
}

LIBC_FN int sysv_getnameinfo(const void *address, uint32_t length, char *host, uint32_t host_size, char *service,
                             uint32_t service_size, int flags)
{
    SOCKADDR_STORAGE converted;
    shim_winsock_init();
    if (shim_sockaddr_to_windows(address, (int)length, &converted) < 0) {
        return LINUX_EAI_FAMILY;
    }
    int status = getnameinfo((const SOCKADDR *)&converted, (socklen_t)length, host, host_size, service, service_size,
                             flags);
    return status == 0 ? 0 : translate_error(status);
}

LIBC_FN const char *sysv_gai_strerror(int code)
{
    switch (code) {
    case LINUX_EAI_BADFLAGS:
        return "Bad value for ai_flags";
    case LINUX_EAI_NONAME:
        return "Name or service not known";
    case LINUX_EAI_AGAIN:
        return "Temporary failure in name resolution";
    case LINUX_EAI_FAIL:
        return "Non-recoverable failure in name resolution";
    case LINUX_EAI_FAMILY:
        return "ai_family not supported";
    case LINUX_EAI_SOCKTYPE:
        return "ai_socktype not supported";
    case LINUX_EAI_SERVICE:
        return "Servname not supported for ai_socktype";
    case LINUX_EAI_MEMORY:
        return "Memory allocation failure";
    case LINUX_EAI_SYSTEM:
        return "System error";
    default:
        return "Unknown error";
    }
}
