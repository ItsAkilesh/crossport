/*
 * File: tests/corpus/unix_sockets/unix_sockets.c
 * Purpose: Exercise Unix-domain stream sockets the way libdbus does: path, /run and abstract addresses, a client and a
 *          server in one process, socketpair, peer credentials, poll, select and epoll, and a connect that finds no bus
 * Author: Akilesh
 * Date: 2026-11-06
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define LONG_TIMEOUT_MS 5000
#define SHORT_TIMEOUT_MS 20
#define BULK_BYTES (256 * 1024)
#define CHUNK_BYTES 4096
#define PATH_TEXT_BYTES 108

static int g_failures;
static int g_is_quiet; /* the /run case may not be creatable on Linux, so its lines are counted but not printed */

typedef struct {
    struct sockaddr_un address;
    socklen_t length;
    int result;
} ClientJob;

static void expect(int condition, const char *what)
{
    if (!g_is_quiet || !condition) {
        printf("%s: %s\n", what, condition ? "ok" : "FAILED");
    }
    g_failures += !condition;
}

/* Byte comparison written out so that the compiler does not turn it into a bcmp call. */
static int same_bytes(const char *left, const char *right, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (left[i] != right[i]) {
            return 0;
        }
    }
    return 1;
}

static int poll_one(int fd, short events, int timeout_ms)
{
    struct pollfd entry = {.fd = fd, .events = events};
    int ready = poll(&entry, 1, timeout_ms);
    return ready > 0 ? entry.revents : -ready;
}

static int select_readable(int fd, int timeout_ms)
{
    fd_set readable;
    struct timeval timeout = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    FD_ZERO(&readable);
    FD_SET(fd, &readable);
    return select(fd + 1, &readable, NULL, NULL, &timeout) > 0 && FD_ISSET(fd, &readable);
}

static int epoll_readable(int fd, int timeout_ms)
{
    struct epoll_event event = {.events = EPOLLIN, .data.fd = fd};
    struct epoll_event ready[1];
    int queue = epoll_create1(0);
    int count = queue >= 0 && epoll_ctl(queue, EPOLL_CTL_ADD, fd, &event) == 0 ? epoll_wait(queue, ready, 1, timeout_ms) : -1;
    close(queue);
    return count == 1 && (ready[0].events & EPOLLIN) != 0;
}

/* A sockaddr_un for a pathname (a NUL-led name when `is_abstract`) and the length that bind and connect take. */
static socklen_t make_address(struct sockaddr_un *address, const char *name, int is_abstract)
{
    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    size_t bytes = strlen(name);
    memcpy(address->sun_path + (is_abstract ? 1 : 0), name, bytes);
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + bytes + 1);
}

static void *client_main(void *argument)
{
    ClientJob *job = (ClientJob *)argument;
    char reply[8] = {0};
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    job->result = fd >= 0 && connect(fd, (struct sockaddr *)&job->address, job->length) == 0 && send(fd, "ping", 4, 0) == 4 &&
                  recv(fd, reply, sizeof(reply), 0) == 4 && strcmp(reply, "pong") == 0;
    close(fd);
    return NULL;
}

/* One full exchange with a client thread; returns 1 when both sides did their part. */
static int serve_once(int server, ClientJob *job, int is_checking_names)
{
    pthread_t thread;
    char request[8] = {0};
    struct sockaddr_un peer;
    socklen_t peer_length = sizeof(peer);
    if (pthread_create(&thread, NULL, client_main, job) != 0) {
        return 0;
    }
    expect((poll_one(server, POLLIN, LONG_TIMEOUT_MS) & POLLIN) != 0, "listener becomes readable on a connection");
    int connection = accept(server, (struct sockaddr *)&peer, &peer_length);
    expect(connection >= 0 && peer.sun_family == AF_UNIX, "accept returns an AF_UNIX peer");
    if (is_checking_names) {
        struct sockaddr_un own;
        socklen_t own_length = sizeof(own);
        expect(getsockname(connection, (struct sockaddr *)&own, &own_length) == 0 && own.sun_family == AF_UNIX &&
                   own_length == job->length && same_bytes(own.sun_path, job->address.sun_path, job->length - 2),
               "getsockname on the accepted socket returns the bound name");
    }
    expect(select_readable(connection, LONG_TIMEOUT_MS), "select sees the request");
    expect(recv(connection, request, sizeof(request), 0) == 4 && strcmp(request, "ping") == 0, "server receives ping");
    expect(send(connection, "pong", 4, 0) == 4, "server sends pong");
    pthread_join(thread, NULL);
    expect(job->result == 1, "client completed the exchange");
    close(connection);
    return 1;
}

static void test_address(const char *label, const char *name, int is_abstract, int is_quiet)
{
    ClientJob job = {0};
    char text[PATH_TEXT_BYTES + 32];
    job.length = make_address(&job.address, name, is_abstract);
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    int is_bound = bind(server, (struct sockaddr *)&job.address, job.length) == 0;
    snprintf(text, sizeof(text), "bind %s", label);
    expect(is_bound || (!is_abstract && (errno == ENOENT || errno == EACCES)), text);
    g_is_quiet = is_quiet;
    if (is_bound) {
        struct sockaddr_un again;
        int second = socket(AF_UNIX, SOCK_STREAM, 0);
        make_address(&again, name, is_abstract);
        expect(bind(second, (struct sockaddr *)&again, job.length) == -1 && errno == EADDRINUSE, "a second bind fails with EADDRINUSE");
        close(second);
        expect(listen(server, 4) == 0, "listen");
        serve_once(server, &job, 1);
        if (!is_abstract) {
            unlink(name);
        }
    }
    close(server);
    g_is_quiet = 0;
}

static void test_no_bus(void)
{
    struct sockaddr_un address;
    char path[PATH_TEXT_BYTES];
    snprintf(path, sizeof(path), "/run/user/%u/crossport-no-such-bus-%d", (unsigned)getuid(), (int)getpid());
    socklen_t length = make_address(&address, path, 0);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    expect(connect(fd, (struct sockaddr *)&address, length) == -1 && errno == ENOENT, "connect to a bus that is not there fails with ENOENT");
    close(fd);
    snprintf(path, sizeof(path), "/tmp/crossport-no-such-socket-%d", (int)getpid());
    length = make_address(&address, path, 0);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    expect(connect(fd, (struct sockaddr *)&address, length) == -1 && errno == ENOENT, "connect to a missing path fails with ENOENT");
    close(fd);
    length = make_address(&address, "crossport-no-such-abstract", 1);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    expect(connect(fd, (struct sockaddr *)&address, length) == -1 && (errno == ECONNREFUSED || errno == ENOENT),
           "connect to an unbound abstract name fails");
    close(fd);
}

static void *bulk_writer(void *argument)
{
    int fd = *(int *)argument;
    char chunk[CHUNK_BYTES];
    memset(chunk, 'x', sizeof(chunk));
    for (int sent = 0; sent < BULK_BYTES; sent += CHUNK_BYTES) {
        if (write(fd, chunk, sizeof(chunk)) != (ssize_t)sizeof(chunk)) {
            return (void *)1;
        }
    }
    shutdown(fd, SHUT_WR);
    return NULL;
}

static void test_socketpair(void)
{
    int pair[2];
    char buffer[CHUNK_BYTES];
    struct ucred credentials;
    socklen_t size = sizeof(credentials);
    expect(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0, "socketpair created");
    expect(send(pair[0], "left", 4, 0) == 4 && recv(pair[1], buffer, sizeof(buffer), 0) == 4, "data crosses the pair");
    expect(poll_one(pair[0], POLLIN, 0) == 0 && poll_one(pair[0], POLLOUT, 0) == POLLOUT, "idle end is writable, not readable");
    expect(write(pair[1], "right", 5) == 5 && epoll_readable(pair[0], LONG_TIMEOUT_MS), "epoll sees data on the pair");
    expect(read(pair[0], buffer, sizeof(buffer)) == 5, "read and write work");
    expect(getsockopt(pair[0], SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0 && credentials.uid == getuid() &&
               credentials.gid == getgid(),
           "SO_PEERCRED reports this user");
    int flags = fcntl(pair[0], F_GETFL);
    expect(flags >= 0 && fcntl(pair[0], F_SETFL, flags | O_NONBLOCK) == 0 && recv(pair[0], buffer, 1, 0) == -1 && errno == EAGAIN,
           "empty non-blocking recv fails with EAGAIN");
    pthread_t thread;
    long total = 0;
    long got;
    int writer_fd = pair[1];
    fcntl(pair[0], F_SETFL, flags);
    expect(pthread_create(&thread, NULL, bulk_writer, &writer_fd) == 0, "bulk writer started");
    while ((got = read(pair[0], buffer, sizeof(buffer))) > 0) {
        total += got;
    }
    pthread_join(thread, NULL);
    expect(total == BULK_BYTES, "a quarter megabyte arrives intact");
    expect((poll_one(pair[0], POLLIN, 0) & POLLIN) != 0, "a closed-for-writing peer reads as end of file");
    close(pair[0]);
    close(pair[1]);
}

static void test_hangup(void)
{
    int pair[2];
    char byte;
    expect(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "socketpair for hangup");
    close(pair[1]);
    expect((poll_one(pair[0], POLLIN, LONG_TIMEOUT_MS) & POLLIN) != 0 && read(pair[0], &byte, 1) == 0, "a closed peer reads as end of file");
    expect(poll_one(pair[0], POLLIN, SHORT_TIMEOUT_MS) != 0, "and stays ready");
    close(pair[0]);
}

int main(void)
{
    char path[PATH_TEXT_BYTES];
    char name[PATH_TEXT_BYTES];
    snprintf(path, sizeof(path), "/tmp/crossport-unix-%d", (int)getpid());
    unlink(path);
    test_address("a pathname under /tmp", path, 0, 0);
    snprintf(path, sizeof(path), "/run/user/%u/crossport-bus-%d", (unsigned)getuid(), (int)getpid());
    unlink(path);
    test_address("a pathname under /run/user", path, 0, 1);
    snprintf(name, sizeof(name), "/tmp/crossport-abstract-%d", (int)getpid());
    test_address("an abstract name", name, 1, 0);
    test_no_bus();
    test_socketpair();
    test_hangup();
    printf("failures: %d\n", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
