/*
 * File: shim/dbus_session.c
 * Purpose: Give a program a D-Bus session bus: find the running crossport-bus, or start it, and set DBUS_SESSION_BUS_ADDRESS
 * Author: Akilesh
 * Date: 2026-11-09
 *
 * GTK and Qt programs look for a session bus in DBUS_SESSION_BUS_ADDRESS. The bus is one process for the user, so
 * every program started from a bundle shares it. The first program starts crossport-bus.exe (found beside the
 * program's own .exe, where `crossport convert` puts it); the daemon writes its address to a file under the same
 * per-user directory the shim shows programs as /run, and exits by itself a minute after the last client leaves.
 * Later programs read the address file and check that something answers on its port. A program in a bundle without
 * the daemon, or one that already has an address, is left alone. Nothing here can stop a program from starting.
 */

#include "sock.h"

#include <stdio.h>
#include <string.h>

#include "env_internal.h"
#include "fd.h"

#define BUS_EXECUTABLE L"crossport-bus.exe"
#define BUS_DIRECTORY L"crossport-run\\crossport-bus\\"
/* The nonce file as the program sees it: the shim maps /run to the directory the daemon writes in. */
#define ADVERTISED_NONCE "/run/crossport-bus/nonce"
#define IDLE_EXIT_SECONDS L"60"
#define ADDRESS_NAME "DBUS_SESSION_BUS_ADDRESS"
#define START_LOCK_NAME L"Local\\CrossportSessionBusStart"
#define LOCK_WAIT_MILLISECONDS 15000
#define START_WAIT_MILLISECONDS 8000
#define POLL_MILLISECONDS 50
#define PROBE_MILLISECONDS 300
#define PORT_TOKEN "port="
#define MAX_ADDRESS_BYTES 1024

/* The directory of the running executable, as a wide Windows path without a trailing separator. */
static int program_directory(wchar_t *out, size_t capacity)
{
    DWORD length = GetModuleFileNameW(NULL, out, (DWORD)capacity);
    wchar_t *last = (length > 0 && length < capacity) ? wcsrchr(out, L'\\') : NULL;
    if (last == NULL) {
        return 0;
    }
    *last = L'\0';
    return 1;
}

static int file_exists(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/* The per-user directory the daemon writes in (and programs see as /run/crossport-bus), with a trailing backslash. */
static int bus_directory(wchar_t *out, size_t capacity)
{
    DWORD length = GetTempPathW((DWORD)capacity, out);
    return length > 0 && length + wcslen(BUS_DIRECTORY) + 1 < capacity && wcscat(out, BUS_DIRECTORY) != NULL;
}

static int read_address(const wchar_t *path, char *out, size_t capacity)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD got = 0;
    BOOL is_read = ReadFile(file, out, (DWORD)capacity - 1, &got, NULL);
    CloseHandle(file);
    if (!is_read || got == 0) {
        return 0;
    }
    out[got] = '\0';
    char *end = out + strcspn(out, "\r\n");
    *end = '\0';
    return strncmp(out, "nonce-tcp:", 10) == 0;
}

/* True when something accepts connections on the port the address names. */
static int is_listening(const char *address)
{
    const char *token = strstr(address, PORT_TOKEN);
    int port = token != NULL ? atoi(token + strlen(PORT_TOKEN)) : 0;
    WSADATA data;
    if (port <= 0 || port > 65535 || WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return 0;
    }
    SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int is_open = 0;
    if (probe != INVALID_SOCKET) {
        u_long non_blocking = 1;
        ioctlsocket(probe, FIONBIO, &non_blocking);
        struct sockaddr_in target = {.sin_family = AF_INET, .sin_port = htons((u_short)port)};
        target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        connect(probe, (struct sockaddr *)&target, sizeof(target));
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(probe, &writable);
        struct timeval wait = {.tv_sec = 0, .tv_usec = PROBE_MILLISECONDS * 1000};
        is_open = select(0, NULL, &writable, NULL, &wait) == 1;
        closesocket(probe);
    }
    return is_open;
}

/* Starts the daemon without a window, detached, and waits for it to publish an address that answers. */
static int start_daemon(const wchar_t *executable, const wchar_t *directory, char *address, size_t capacity)
{
    wchar_t address_file[SHIM_PATH_MAX];
    wchar_t command[SHIM_PATH_MAX * 2];
    swprintf(address_file, SHIM_PATH_MAX, L"%lsaddress", directory);
    DeleteFileW(address_file);
    swprintf(command, sizeof(command) / sizeof(command[0]),
             L"\"%ls\" --nonce-file \"%lsnonce\" --advertise-nonce-file " L"/run/crossport-bus/nonce"
             L" --address-file \"%ls\" --idle-exit " IDLE_EXIT_SECONDS,
             executable, directory, address_file);
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(executable, command, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                        NULL, NULL, &startup, &process)) {
        return 0;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    for (DWORD waited = 0; waited < START_WAIT_MILLISECONDS; waited += POLL_MILLISECONDS) {
        if (read_address(address_file, address, capacity) && is_listening(address)) {
            return 1;
        }
        Sleep(POLL_MILLISECONDS);
    }
    return 0;
}

/* Finds the user's bus or starts it; the address, or NULL, is left in `address`. */
static int find_or_start(const wchar_t *executable, char *address, size_t capacity)
{
    wchar_t directory[SHIM_PATH_MAX];
    wchar_t address_file[SHIM_PATH_MAX];
    if (!bus_directory(directory, SHIM_PATH_MAX)) {
        return 0;
    }
    swprintf(address_file, SHIM_PATH_MAX, L"%lsaddress", directory);
    HANDLE lock = CreateMutexW(NULL, FALSE, START_LOCK_NAME);
    if (lock == NULL || WaitForSingleObject(lock, LOCK_WAIT_MILLISECONDS) == WAIT_TIMEOUT) {
        return 0;
    }
    int is_found = (read_address(address_file, address, capacity) && is_listening(address)) ||
                   start_daemon(executable, directory, address, capacity);
    ReleaseMutex(lock);
    CloseHandle(lock);
    return is_found;
}

void shim_start_session_bus(void)
{
    wchar_t executable[SHIM_PATH_MAX];
    char address[MAX_ADDRESS_BYTES];
    if (GetEnvironmentVariableW(L"DBUS_SESSION_BUS_ADDRESS", NULL, 0) > 0 || !program_directory(executable, SHIM_PATH_MAX) ||
        wcslen(executable) + wcslen(BUS_EXECUTABLE) + 2 >= SHIM_PATH_MAX) {
        return;
    }
    wcscat(executable, L"\\" BUS_EXECUTABLE);
    if (file_exists(executable) && find_or_start(executable, address, sizeof(address))) {
        shim_env_set(ADDRESS_NAME, address, 0);
    }
}
