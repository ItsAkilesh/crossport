/*
 * File: shim/sock.h
 * Purpose: Shared pieces of the socket layer: Linux constants, address conversion and the blocking helpers
 * Author: Akilesh
 * Date: 2026-10-29
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

/* Waits until the socket may have changed state, or the timeout passes. */
void shim_socket_wait(SocketState *state, DWORD milliseconds);

/*
 * Linux sockaddr to Winsock sockaddr and back: only the family number differs between the two. Both
 * return the length of the converted address, or -1 with errno set.
 */
int shim_sockaddr_to_windows(const void *linux_address, int length, SOCKADDR_STORAGE *out);
int shim_sockaddr_to_linux(const SOCKADDR *windows_address, int length, void *out, size_t capacity);

int shim_family_to_windows(int linux_family);
int shim_family_to_linux(int windows_family);

#endif
