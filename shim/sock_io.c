/*
 * File: shim/sock_io.c
 * Purpose: Stream and datagram I/O on sockets: recv, recvfrom, send and sendto
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "sock.h"

#define MAX_IO_CHUNK 0x40000000

static int64_t receive(int fd, void *buffer, size_t count, int flags, SOCKADDR *from, int *from_length)
{
    SocketState *state = shim_socket_state(fd);
    if (state == NULL) {
        return -1;
    }
    int chunk = count > MAX_IO_CHUNK ? MAX_IO_CHUNK : (int)count;
    for (;;) {
        int got = recvfrom(state->socket, (char *)buffer, chunk, (flags & LINUX_MSG_PEEK) ? MSG_PEEK : 0, from, from_length);
        if (got >= 0) {
            return got;
        }
        if (WSAGetLastError() == WSAECONNRESET && state->type == LINUX_SOCK_STREAM) {
            return shim_fail(LINUX_ECONNRESET);
        }
        if (!shim_socket_should_retry(fd, state, (flags & LINUX_MSG_DONTWAIT) != 0)) {
            return shim_fail_from_win();
        }
    }
}

LIBC_FN int64_t sysv_recv(int fd, void *buffer, size_t count, int flags)
{
    return receive(fd, buffer, count, flags, NULL, NULL);
}

LIBC_FN int64_t sysv_recvfrom(int fd, void *buffer, size_t count, int flags, void *address, uint32_t *length)
{
    SOCKADDR_STORAGE from;
    int from_length = sizeof(from);
    int64_t got = receive(fd, buffer, count, flags, (SOCKADDR *)&from, &from_length);
    if (got >= 0 && address != NULL && length != NULL) {
        int written = shim_sockaddr_to_linux((SOCKADDR *)&from, from_length, address, *length);
        *length = written >= 0 ? (uint32_t)written : *length;
    }
    return got;
}

static int64_t transmit(int fd, const void *buffer, size_t count, int flags, const SOCKADDR *to, int to_length)
{
    SocketState *state = shim_socket_state(fd);
    size_t done = 0;
    if (state == NULL) {
        return -1;
    }
    while (done < count || count == 0) {
        int chunk = (count - done) > MAX_IO_CHUNK ? MAX_IO_CHUNK : (int)(count - done);
        int sent = to != NULL ? sendto(state->socket, (const char *)buffer + done, chunk, 0, to, to_length)
                              : send(state->socket, (const char *)buffer + done, chunk, 0);
        if (sent >= 0) {
            done += (size_t)sent;
            if (count == 0 || state->type != LINUX_SOCK_STREAM) {
                break;
            }
        } else if (!shim_socket_should_retry(fd, state, (flags & LINUX_MSG_DONTWAIT) != 0)) {
            return done > 0 ? (int64_t)done : shim_fail_from_win();
        }
    }
    return (int64_t)done;
}

LIBC_FN int64_t sysv_send(int fd, const void *buffer, size_t count, int flags)
{
    return transmit(fd, buffer, count, flags, NULL, 0);
}

LIBC_FN int64_t sysv_sendto(int fd, const void *buffer, size_t count, int flags, const void *address, uint32_t length)
{
    SOCKADDR_STORAGE converted;
    if (address == NULL) {
        return transmit(fd, buffer, count, flags, NULL, 0);
    }
    int size = shim_sockaddr_to_windows(address, (int)length, &converted);
    return size < 0 ? -1 : transmit(fd, buffer, count, flags, (const SOCKADDR *)&converted, size);
}

int64_t shim_socket_read(int fd, void *buffer, size_t count)
{
    return receive(fd, buffer, count, 0, NULL, NULL);
}

int64_t shim_socket_write(int fd, const void *buffer, size_t count)
{
    return transmit(fd, buffer, count, 0, NULL, 0);
}
