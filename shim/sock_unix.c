/*
 * File: shim/sock_unix.c
 * Purpose: Unix-domain sockets on Winsock AF_UNIX: address mapping (paths, /run, abstract names), socketpair,
 *          peer credentials and the ancillary data a program may not send (socketpair is in sock_pair.c)
 * Author: Akilesh
 * Date: 2026-11-06
 */

#include <stddef.h>
#include <string.h>

#include "sock.h"

#include <afunix.h>

#ifndef SIO_AF_UNIX_GETPEERPID
#define SIO_AF_UNIX_GETPEERPID 0x58000100
#endif

#define UNIX_PATH_MAX 108
#define ABSTRACT_DIRECTORY "/tmp/crossport-abs/"
#define RUN_DIRECTORY "/run/"
#define TEMP_DIRECTORY "/tmp/"
#define HEX_DIGITS "0123456789abcdef"
#define LINUX_SCM_RIGHTS 1
#define CMSG_ALIGNMENT 8

LIBC_FN unsigned sysv_geteuid(void);
LIBC_FN unsigned sysv_getgid(void);
LIBC_FN int sysv_snprintf(char *buffer, size_t size, const char *format, ...);

/* struct cmsghdr, and struct ucred as SO_PEERCRED returns it. */
typedef struct {
    size_t length;
    int32_t level;
    int32_t type;
} LinuxCmsgHdr;

typedef struct {
    int32_t pid;
    uint32_t uid;
    uint32_t gid;
} LinuxUcred;

static void use_windows_separators(char *path)
{
    for (; *path != '\0'; path++) {
        *path = *path == '/' ? '\\' : *path;
    }
}

static int has_prefix_nocase(const char *text, const char *prefix)
{
    for (; *prefix != '\0'; prefix++, text++) {
        int is_same_separator = (*prefix == '\\' || *prefix == '/') && (*text == '\\' || *text == '/');
        if (!is_same_separator && tolower((unsigned char)*prefix) != tolower((unsigned char)*text)) {
            return 0;
        }
    }
    return 1;
}

/* Creates every directory above the file the path names; the ones that exist already are fine. */
static void make_parent_directories(const char *linux_path)
{
    wchar_t wide[SHIM_PATH_MAX];
    if (shim_path_to_wide(linux_path, wide, SHIM_PATH_MAX) != 0) {
        return;
    }
    for (wchar_t *at = wide + 1; *at != L'\0'; at++) {
        if (*at == L'\\' || *at == L'/') {
            wchar_t kept = *at;
            *at = L'\0';
            CreateDirectoryW(wide, NULL);
            *at = kept;
        }
    }
}

/* The virtual paths whose parents do not exist on Windows until something makes them. */
static int is_runtime_path(const char *path)
{
    return strncmp(path, "/run/", 5) == 0 || strncmp(path, "/var/run/", 9) == 0 ||
           strncmp(path, ABSTRACT_DIRECTORY, sizeof(ABSTRACT_DIRECTORY) - 1) == 0;
}

/* Characters that may stand for themselves in a file name; every other byte is written as %xx. */
static int is_plain(unsigned char byte)
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '-' ||
           byte == '.' || byte == '_';
}

/* An abstract name has no file, so it becomes one under a private directory, with unsafe bytes escaped. */
static int abstract_to_path(const unsigned char *name, int bytes, char *out, size_t out_size)
{
    size_t at = strlen(ABSTRACT_DIRECTORY);
    memcpy(out, ABSTRACT_DIRECTORY, at);
    for (int i = 0; i < bytes; i++) {
        size_t need = is_plain(name[i]) ? 1 : 3;
        if (at + need + 1 > out_size) {
            return shim_fail(LINUX_ENAMETOOLONG);
        }
        if (need == 1) {
            out[at++] = (char)name[i];
        } else {
            out[at++] = '%';
            out[at++] = HEX_DIGITS[name[i] >> 4];
            out[at++] = HEX_DIGITS[name[i] & 0xf];
        }
    }
    out[at] = '\0';
    return 0;
}

int shim_unix_to_windows(const void *linux_address, int length, SOCKADDR_STORAGE *out)
{
    const char *path = (const char *)linux_address + sizeof(uint16_t);
    int path_bytes = length - (int)sizeof(uint16_t);
    char text[SHIM_PATH_MAX];
    if (path_bytes <= 0) {
        return shim_fail(LINUX_EINVAL); /* autobind needs names that only the kernel can invent */
    }
    if (path[0] == '\0') {
        if (abstract_to_path((const unsigned char *)path + 1, path_bytes - 1, text, sizeof(text)) != 0) {
            return -1;
        }
    } else {
        size_t copied = strnlen(path, (size_t)path_bytes < UNIX_PATH_MAX ? (size_t)path_bytes : UNIX_PATH_MAX);
        memcpy(text, path, copied);
        text[copied] = '\0';
    }
    SOCKADDR_UN *target = (SOCKADDR_UN *)out;
    if (is_runtime_path(text)) {
        make_parent_directories(text);
    }
    target->sun_family = AF_UNIX;
    if (shim_path_to_windows(text, target->sun_path, sizeof(target->sun_path)) != 0) {
        return -1;
    }
    return (int)(offsetof(SOCKADDR_UN, sun_path) + strlen(target->sun_path) + 1);
}

/* Decodes the name that abstract_to_path wrote; returns its length, or -1 when `text` is not one. */
static int decode_name(const char *text, unsigned char *out, size_t capacity)
{
    size_t length = 0;
    for (; *text != '\0' && length < capacity; length++) {
        const char *high = text[0] == '%' && text[1] != '\0' ? strchr(HEX_DIGITS, text[1]) : NULL;
        const char *low = high != NULL && text[2] != '\0' ? strchr(HEX_DIGITS, text[2]) : NULL;
        if (text[0] != '%') {
            out[length] = (unsigned char)*text++;
        } else if (low != NULL) {
            out[length] = (unsigned char)(((high - HEX_DIGITS) << 4) | (low - HEX_DIGITS));
            text += 3;
        } else {
            return -1;
        }
    }
    return *text == '\0' ? (int)length : -1;
}

/* The part of `windows_path` below the Windows place of `linux_directory`, or NULL when it lies elsewhere. */
static const char *below(const char *windows_path, const char *linux_directory, char *scratch, size_t scratch_size)
{
    if (shim_path_to_windows(linux_directory, scratch, scratch_size) != 0) {
        return NULL;
    }
    use_windows_separators(scratch);
    return has_prefix_nocase(windows_path, scratch) ? windows_path + strlen(scratch) : NULL;
}

/* `directory` and `rest` joined, with Linux separators; returns the length of the text. */
static int linux_text(const char *directory, const char *rest, char *out, size_t capacity)
{
    int length = sysv_snprintf(out, capacity, "%s%s", directory, rest);
    for (char *at = out; *at != '\0'; at++) {
        *at = *at == '\\' ? '/' : *at;
    }
    return length;
}

/* The Linux name of a bound Windows path: abstract, under /run, under /tmp, or on a drive. */
static int path_to_linux(const char *bound_path, char *out, size_t capacity)
{
    char scratch[SHIM_PATH_MAX];
    char windows_path[UNIX_PATH_MAX];
    lstrcpynA(windows_path, bound_path, sizeof(windows_path));
    use_windows_separators(windows_path);
    const char *rest = below(windows_path, ABSTRACT_DIRECTORY, scratch, sizeof(scratch));
    if (rest != NULL) {
        int bytes = decode_name(rest, (unsigned char *)out + 1, capacity - 1);
        out[0] = '\0';
        return bytes >= 0 ? bytes + 1 : -1;
    }
    rest = below(windows_path, RUN_DIRECTORY, scratch, sizeof(scratch));
    if (rest != NULL) {
        return linux_text(RUN_DIRECTORY, rest, out, capacity) + 1;
    }
    rest = below(windows_path, TEMP_DIRECTORY, scratch, sizeof(scratch));
    if (rest != NULL) {
        return linux_text(TEMP_DIRECTORY, rest, out, capacity) + 1;
    }
    return linux_text(windows_path[1] == ':' ? "/" : "", windows_path, out, capacity) + 1;
}

int shim_unix_to_linux(const void *windows_address, int length, void *out, size_t capacity)
{
    const SOCKADDR_UN *name = (const SOCKADDR_UN *)windows_address;
    char *path = (char *)out + sizeof(uint16_t);
    int bytes = 0;
    if (capacity < sizeof(uint16_t) + UNIX_PATH_MAX) {
        return shim_fail(LINUX_EINVAL);
    }
    ((uint16_t *)out)[0] = LINUX_AF_UNIX;
    if (length > (int)offsetof(SOCKADDR_UN, sun_path) && name->sun_path[0] != '\0') {
        bytes = path_to_linux(name->sun_path, path, UNIX_PATH_MAX);
    }
    return bytes < 0 ? shim_fail(LINUX_EINVAL) : (int)sizeof(uint16_t) + bytes;
}

int shim_unix_check_target(const SOCKADDR_STORAGE *address)
{
    const SOCKADDR_UN *target = (const SOCKADDR_UN *)address;
    if (target->sun_family != AF_UNIX) {
        return 0;
    }
    return GetFileAttributesA(target->sun_path) == INVALID_FILE_ATTRIBUTES ? shim_fail(LINUX_ENOENT) : 0;
}

void shim_unix_remember_abstract(SocketState *state, const SOCKADDR_STORAGE *bound)
{
    char scratch[SHIM_PATH_MAX];
    const SOCKADDR_UN *name = (const SOCKADDR_UN *)bound;
    if (name->sun_family != AF_UNIX || below(name->sun_path, ABSTRACT_DIRECTORY, scratch, sizeof(scratch)) == NULL) {
        return;
    }
    size_t bytes = strlen(name->sun_path) + 1;
    state->abstract_path = (char *)HeapAlloc(GetProcessHeap(), 0, bytes);
    if (state->abstract_path != NULL) {
        memcpy(state->abstract_path, name->sun_path, bytes);
    }
}

void shim_unix_forget_abstract(SocketState *state)
{
    if (state->abstract_path != NULL) {
        DeleteFileA(state->abstract_path);
        HeapFree(GetProcessHeap(), 0, state->abstract_path);
    }
}

int shim_unix_peercred(SocketState *state, void *value, uint32_t *length)
{
    LinuxUcred credentials = {.pid = (int32_t)GetCurrentProcessId(), .uid = sysv_geteuid(), .gid = sysv_getgid()};
    ULONG peer = 0;
    DWORD returned = 0;
    if (state->family != LINUX_AF_UNIX) {
        return shim_fail(LINUX_ENOPROTOOPT);
    }
    if (WSAIoctl(state->socket, SIO_AF_UNIX_GETPEERPID, NULL, 0, &peer, sizeof(peer), &returned, NULL, NULL) == 0) {
        credentials.pid = (int32_t)peer;
    }
    size_t size = *length < sizeof(credentials) ? *length : sizeof(credentials);
    memcpy(value, &credentials, size);
    *length = (uint32_t)size;
    return 0;
}

/* Descriptors cannot cross between Windows processes this way, so SCM_RIGHTS is refused; other control data is dropped. */
int shim_unix_check_control(const void *control, size_t length)
{
    size_t offset = 0;
    while (control != NULL && offset + sizeof(LinuxCmsgHdr) <= length) {
        const LinuxCmsgHdr *header = (const LinuxCmsgHdr *)((const char *)control + offset);
        if (header->level == LINUX_SOL_SOCKET && header->type == LINUX_SCM_RIGHTS) {
            return shim_fail(LINUX_EOPNOTSUPP);
        }
        if (header->length < sizeof(LinuxCmsgHdr)) {
            break;
        }
        offset += (header->length + CMSG_ALIGNMENT - 1) & ~(size_t)(CMSG_ALIGNMENT - 1);
    }
    return 0;
}
