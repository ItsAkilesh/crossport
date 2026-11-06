/*
 * File: shim/sock_pair.c
 * Purpose: socketpair on Winsock AF_UNIX: a listener on a private path, one connect and one accept
 * Author: Akilesh
 * Date: 2026-11-06
 */

#include <stddef.h>
#include <string.h>

#include "sock.h"

#include <afunix.h>

#define UNIX_PATH_MAX 108
#define PAIR_PATH_FORMAT "/tmp/crossport-pair-%lu-%ld"
#define PAIR_BACKLOG 1

LIBC_FN int sysv_snprintf(char *buffer, size_t size, const char *format, ...);

static volatile LONG g_pair_counter;

/* Listens on a fresh private path, connects to it and accepts: the two ends are the pair. */
static int connect_through(SOCKET listener, SOCKET client, const char *windows_path, SOCKET *server)
{
    SOCKADDR_UN address = {.sun_family = AF_UNIX};
    strncpy(address.sun_path, windows_path, sizeof(address.sun_path) - 1);
    if (bind(listener, (SOCKADDR *)&address, sizeof(address)) != 0 || listen(listener, PAIR_BACKLOG) != 0 ||
        connect(client, (SOCKADDR *)&address, sizeof(address)) != 0) {
        return -1;
    }
    *server = accept(listener, NULL, NULL);
    return *server == INVALID_SOCKET ? -1 : 0;
}

static int make_pair(SOCKET *server, SOCKET *client)
{
    char linux_path[SHIM_PATH_MAX];
    char windows_path[UNIX_PATH_MAX] = {0};
    sysv_snprintf(linux_path, sizeof(linux_path), PAIR_PATH_FORMAT, (unsigned long)GetCurrentProcessId(), (long)InterlockedIncrement(&g_pair_counter));
    SOCKET listener = socket(AF_UNIX, SOCK_STREAM, 0);
    *client = socket(AF_UNIX, SOCK_STREAM, 0);
    *server = INVALID_SOCKET;
    int is_ready = listener != INVALID_SOCKET && *client != INVALID_SOCKET &&
                   shim_path_to_windows(linux_path, windows_path, sizeof(windows_path)) == 0 &&
                   connect_through(listener, *client, windows_path, server) == 0;
    int error = is_ready ? 0 : shim_fail_from_win();
    if (listener != INVALID_SOCKET) {
        closesocket(listener);
    }
    DeleteFileA(windows_path);
    if (!is_ready && *client != INVALID_SOCKET) {
        closesocket(*client);
    }
    return is_ready ? 0 : error;
}

/* A connected pair of Unix-domain stream sockets. */
LIBC_FN int sysv_socketpair(int domain, int type, int protocol, int fds[2])
{
    SOCKET server;
    SOCKET client;
    (void)protocol;
    if (domain != LINUX_AF_UNIX) {
        return shim_fail(LINUX_EOPNOTSUPP);
    }
    if ((type & LINUX_SOCK_TYPE_MASK) != LINUX_SOCK_STREAM) {
        return shim_fail(LINUX_EOPNOTSUPP);
    }
    shim_winsock_init();
    if (make_pair(&server, &client) != 0) {
        return -1;
    }
    fds[0] = shim_socket_adopt(server, domain, LINUX_SOCK_STREAM, type);
    fds[1] = shim_socket_adopt(client, domain, LINUX_SOCK_STREAM, type);
    return (fds[0] < 0 || fds[1] < 0) ? -1 : 0;
}
