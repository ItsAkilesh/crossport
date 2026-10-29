/*
 * File: shim/sock.c
 * Purpose: Sockets on Winsock: create, bind, listen, accept, connect, shutdown, names and socketpair
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "sock.h"

#define EVENTS_OF_INTEREST (FD_READ | FD_WRITE | FD_ACCEPT | FD_CONNECT | FD_CLOSE)
#define WAIT_SLICE_MS 50

static INIT_ONCE g_winsock_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK start_winsock(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    WSADATA data;
    (void)once;
    (void)parameter;
    (void)context;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void shim_winsock_init(void)
{
    InitOnceExecuteOnce(&g_winsock_once, start_winsock, NULL, NULL);
}

SocketState *shim_socket_state(int fd)
{
    if (shim_fd_flags(fd) < 0) {
        return NULL;
    }
    if (shim_fd_kind(fd) != FD_KIND_SOCKET) {
        shim_set_errno(LINUX_ENOTSOCK);
        return NULL;
    }
    return (SocketState *)shim_fd_state(fd);
}

void shim_socket_wait(SocketState *state, DWORD milliseconds)
{
    WSANETWORKEVENTS events;
    WaitForSingleObject(state->event, milliseconds);
    WSAEnumNetworkEvents(state->socket, state->event, &events);
}

/* True when `fd` is blocking and the failed call only needs to wait: waits once and asks for a retry. */
int shim_socket_should_retry(int fd, SocketState *state, int is_call_nonblocking)
{
    if (WSAGetLastError() != WSAEWOULDBLOCK || is_call_nonblocking || (shim_fd_flags(fd) & LINUX_O_NONBLOCK) != 0) {
        return 0;
    }
    shim_socket_wait(state, WAIT_SLICE_MS);
    return 1;
}

static int adopt(SOCKET socket, int family, int type, int flags)
{
    SocketState *state = (SocketState *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SocketState));
    HANDLE event = WSACreateEvent();
    if (state == NULL || event == WSA_INVALID_EVENT) {
        HeapFree(GetProcessHeap(), 0, state);
        closesocket(socket);
        return shim_fail(LINUX_ENOMEM);
    }
    *state = (SocketState){.socket = socket, .event = event, .family = family, .type = type};
    WSAEventSelect(socket, event, EVENTS_OF_INTEREST);
    int fd = shim_fd_adopt_kind(FD_KIND_SOCKET, (HANDLE)socket, state, LINUX_O_RDWR | (flags & LINUX_O_NONBLOCK));
    if (fd < 0) {
        shim_socket_close(state);
    }
    return fd;
}

int shim_socket_close(SocketState *state)
{
    int result = closesocket(state->socket) == 0 ? 0 : shim_fail_from_win();
    WSACloseEvent(state->event);
    HeapFree(GetProcessHeap(), 0, state);
    return result;
}

LIBC_FN int sysv_socket(int domain, int type, int protocol)
{
    shim_winsock_init();
    int kind = type & LINUX_SOCK_TYPE_MASK;
    SOCKET socket_handle = WSASocketW(shim_family_to_windows(domain), kind, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (socket_handle == INVALID_SOCKET) {
        return shim_fail_from_win();
    }
    return adopt(socket_handle, domain, kind, type);
}

LIBC_FN int sysv_bind(int fd, const void *address, uint32_t length)
{
    SocketState *state = shim_socket_state(fd);
    SOCKADDR_STORAGE converted;
    int size = state != NULL ? shim_sockaddr_to_windows(address, (int)length, &converted) : -1;
    return size >= 0 && bind(state->socket, (const SOCKADDR *)&converted, size) == 0 ? 0 : (size < 0 ? -1 : shim_fail_from_win());
}

LIBC_FN int sysv_listen(int fd, int backlog)
{
    SocketState *state = shim_socket_state(fd);
    return state != NULL && listen(state->socket, backlog) == 0 ? 0 : (state == NULL ? -1 : shim_fail_from_win());
}

static int accept_into(int fd, void *address, uint32_t *length, int flags)
{
    SocketState *state = shim_socket_state(fd);
    if (state == NULL) {
        return -1;
    }
    for (;;) {
        SOCKADDR_STORAGE peer;
        int peer_length = sizeof(peer);
        SOCKET accepted = accept(state->socket, (SOCKADDR *)&peer, &peer_length);
        if (accepted != INVALID_SOCKET) {
            if (address != NULL && length != NULL) {
                int written = shim_sockaddr_to_linux((SOCKADDR *)&peer, peer_length, address, *length);
                *length = written >= 0 ? (uint32_t)written : *length;
            }
            return adopt(accepted, state->family, state->type, flags);
        }
        if (!shim_socket_should_retry(fd, state, 0)) {
            return shim_fail_from_win();
        }
    }
}

LIBC_FN int sysv_accept(int fd, void *address, uint32_t *length)
{
    return accept_into(fd, address, length, 0);
}

LIBC_FN int sysv_accept4(int fd, void *address, uint32_t *length, int flags)
{
    return accept_into(fd, address, length, flags);
}

/* A connect that is still in progress finishes when the socket becomes writable; its result is SO_ERROR. */
static int finish_connect(int fd, SocketState *state)
{
    for (;;) {
        fd_set writable;
        TIMEVAL zero = {0, 0};
        FD_ZERO(&writable);
        FD_SET(state->socket, &writable);
        if (select(0, NULL, &writable, NULL, &zero) > 0) {
            int error = 0;
            int size = sizeof(error);
            getsockopt(state->socket, SOL_SOCKET, SO_ERROR, (char *)&error, &size);
            return error == 0 ? 0 : shim_fail(shim_errno_from_win((DWORD)error));
        }
        shim_socket_wait(state, WAIT_SLICE_MS);
        (void)fd;
    }
}

LIBC_FN int sysv_connect(int fd, const void *address, uint32_t length)
{
    SocketState *state = shim_socket_state(fd);
    SOCKADDR_STORAGE converted;
    if (state == NULL) {
        return -1;
    }
    int size = shim_sockaddr_to_windows(address, (int)length, &converted);
    if (size < 0) {
        return -1;
    }
    if (connect(state->socket, (const SOCKADDR *)&converted, size) == 0) {
        return 0;
    }
    if (WSAGetLastError() != WSAEWOULDBLOCK) {
        return shim_fail_from_win();
    }
    return (shim_fd_flags(fd) & LINUX_O_NONBLOCK) != 0 ? shim_fail(LINUX_EINPROGRESS) : finish_connect(fd, state);
}

LIBC_FN int sysv_shutdown(int fd, int how)
{
    SocketState *state = shim_socket_state(fd);
    int windows_how = how == LINUX_SHUT_RD ? SD_RECEIVE : (how == LINUX_SHUT_WR ? SD_SEND : SD_BOTH);
    return state != NULL && shutdown(state->socket, windows_how) == 0 ? 0 : (state == NULL ? -1 : shim_fail_from_win());
}

static int name_of(int fd, void *address, uint32_t *length, int is_peer)
{
    SocketState *state = shim_socket_state(fd);
    SOCKADDR_STORAGE name;
    int size = sizeof(name);
    if (state == NULL) {
        return -1;
    }
    int status = is_peer ? getpeername(state->socket, (SOCKADDR *)&name, &size)
                         : getsockname(state->socket, (SOCKADDR *)&name, &size);
    if (status != 0) {
        return shim_fail_from_win();
    }
    int written = shim_sockaddr_to_linux((SOCKADDR *)&name, size, address, *length);
    *length = written >= 0 ? (uint32_t)written : *length;
    return written >= 0 ? 0 : -1;
}

LIBC_FN int sysv_getsockname(int fd, void *address, uint32_t *length)
{
    return name_of(fd, address, length, 0);
}

LIBC_FN int sysv_getpeername(int fd, void *address, uint32_t *length)
{
    return name_of(fd, address, length, 1);
}

/* A connected pair of stream sockets, built from a loopback listener; the pair reports AF_UNIX. */
LIBC_FN int sysv_socketpair(int domain, int type, int protocol, int fds[2])
{
    (void)protocol;
    shim_winsock_init();
    SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
    SOCKET client = socket(AF_INET, SOCK_STREAM, 0);
    SOCKADDR_IN local = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    int size = sizeof(local);
    int is_ready = listener != INVALID_SOCKET && client != INVALID_SOCKET && bind(listener, (SOCKADDR *)&local, size) == 0 &&
                   listen(listener, 1) == 0 && getsockname(listener, (SOCKADDR *)&local, &size) == 0 &&
                   connect(client, (SOCKADDR *)&local, size) == 0;
    SOCKET server = is_ready ? accept(listener, NULL, NULL) : INVALID_SOCKET;
    if (listener != INVALID_SOCKET) {
        closesocket(listener);
    }
    if (server == INVALID_SOCKET) {
        int error = shim_fail_from_win();
        if (client != INVALID_SOCKET) {
            closesocket(client);
        }
        return error;
    }
    fds[0] = adopt(server, domain, LINUX_SOCK_STREAM, type);
    fds[1] = adopt(client, domain, LINUX_SOCK_STREAM, type);
    return (fds[0] < 0 || fds[1] < 0) ? -1 : 0;
}
