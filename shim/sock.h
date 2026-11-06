/*
 * File: shim/sock.h
 * Purpose: Shared pieces of the socket layer: Linux constants, address conversion and the blocking helpers
 * Author: Akilesh
 * Date: 2026-11-06
 */

#ifndef CROSSPORT_SOCK_H
#define CROSSPORT_SOCK_H

#include <winsock2.h>
#include <ws2tcpip.h>

#include "fd.h"

/* Linux x86-64 values that differ from Winsock's. */
enum {
    LINUX_AF_UNSPEC = 0, LINUX_AF_UNIX = 1, LINUX_AF_INET = 2, LINUX_AF_INET6 = 10,
    LINUX_SOCK_STREAM = 1, LINUX_SOCK_DGRAM = 2, LINUX_SOCK_TYPE_MASK = 0xf,
    LINUX_SOL_SOCKET = 1, LINUX_IPPROTO_TCP = 6, LINUX_IPPROTO_IPV6 = 41,
    LINUX_MSG_PEEK = 0x2, LINUX_MSG_DONTWAIT = 0x40, LINUX_MSG_NOSIGNAL = 0x4000,
    LINUX_SHUT_RD = 0, LINUX_SHUT_WR = 1, LINUX_SHUT_RDWR = 2
};

#define SOCKADDR_STORAGE_BYTES 128

/* Maps a Linux path onto a Windows path, in UTF-8 (path.c). Returns 0, or -1 with errno set. */
int shim_path_to_windows(const char *linux_path, char *out, size_t out_size);

/* Starts Winsock once; every socket call and getaddrinfo needs it. */
void shim_winsock_init(void);

/* Winsock descriptor behind `fd`, or NULL with errno set (EBADF or ENOTSOCK). */
SocketState *shim_socket_state(int fd);

/* After a failed call: true (having waited once) when the descriptor is blocking and the call only needs a retry. */
int shim_socket_should_retry(int fd, SocketState *state, int is_call_nonblocking);

/* Waits until the socket may have changed state, or the timeout passes. */
void shim_socket_wait(SocketState *state, DWORD milliseconds);

/*
 * Linux sockaddr to Winsock sockaddr and back: only the family number differs between the two. Both
 * return the length of the converted address, or -1 with errno set.
 */
int shim_sockaddr_to_windows(const void *linux_address, int length, SOCKADDR_STORAGE *out);
int shim_sockaddr_to_linux(const SOCKADDR *windows_address, int length, void *out, size_t capacity);

/* Wraps a connected or listening Winsock socket in a descriptor and returns it; closes the socket on failure. */
int shim_socket_adopt(SOCKET socket, int family, int type, int flags);

/*
 * Unix-domain sockets (sock_unix.c). Addresses name files: /tmp and /run map under the Windows temp directory and
 * an abstract name (a leading NUL) becomes a file under a private directory. The conversion functions return the
 * address length, or -1 with errno set.
 */
int shim_unix_to_windows(const void *linux_address, int length, SOCKADDR_STORAGE *out);
int shim_unix_to_linux(const void *windows_address, int length, void *out, size_t capacity);
/* Fails with ENOENT when a Unix-domain address names a file that does not exist; other families pass. */
int shim_unix_check_target(const SOCKADDR_STORAGE *address);
/* An abstract name vanishes when its socket closes: bind records the file that stands for it, close deletes it. */
void shim_unix_remember_abstract(SocketState *state, const SOCKADDR_STORAGE *bound);
void shim_unix_forget_abstract(SocketState *state);
/* SO_PEERCRED: the peer's process, user and group ids. */
int shim_unix_peercred(SocketState *state, void *value, uint32_t *length);
/* Fails with EOPNOTSUPP when ancillary data carries descriptors (SCM_RIGHTS), which cannot be passed. */
int shim_unix_check_control(const void *control, size_t length);

int shim_family_to_windows(int linux_family);
int shim_family_to_linux(int windows_family);

#endif
