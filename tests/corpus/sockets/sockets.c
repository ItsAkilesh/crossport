/*
 * File: tests/corpus/sockets/sockets.c
 * Purpose: Exercise sockets: socketpair, TCP and UDP on loopback, Unix-domain paths, options, names, poll and sendmsg
 * Author: Akilesh
 * Date: 2026-10-29
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SHORT_TIMEOUT_MS 20
#define LONG_TIMEOUT_MS 5000

static int g_failures;

static void expect(int condition, const char *what)
{
    printf("%s: %s\n", what, condition ? "ok" : "FAILED");
    g_failures += !condition;
}

static int poll_one(int fd, short events, int timeout_ms)
{
    struct pollfd entry = {.fd = fd, .events = events};
    int ready = poll(&entry, 1, timeout_ms);
    return ready > 0 ? entry.revents : -ready;
}

static void test_socketpair(void)
{
    int pair[2];
    char buffer[16] = {0};
    expect(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "socketpair created");
    expect(send(pair[0], "left", 4, 0) == 4 && recv(pair[1], buffer, sizeof(buffer), 0) == 4 && strcmp(buffer, "left") == 0,
           "data crosses the pair");
    expect(write(pair[1], "right", 5) == 5 && read(pair[0], buffer, sizeof(buffer)) == 5, "read and write work on sockets");
    expect(poll_one(pair[0], POLLIN, 0) == 0, "idle socket is not readable");
    shutdown(pair[0], SHUT_WR);
    expect(recv(pair[1], buffer, sizeof(buffer), 0) == 0, "shutdown shows as end of file");
    close(pair[0]);
    close(pair[1]);
}

static void test_message_vectors(void)
{
    int pair[2];
    char first[4] = {0};
    char second[8] = {0};
    char text[] = "abcdefghij";
    struct iovec out[2] = {{text, 3}, {text + 3, 7}};
    struct iovec in[2] = {{first, 4}, {second, 8}};
    struct msghdr outgoing = {.msg_iov = out, .msg_iovlen = 2};
    struct msghdr incoming = {.msg_iov = in, .msg_iovlen = 2};
    expect(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "socketpair for messages");
    expect(sendmsg(pair[0], &outgoing, 0) == 10, "sendmsg gathers two buffers");
    expect(recvmsg(pair[1], &incoming, 0) == 10 && memcmp(first, "abcd", 4) == 0 && strcmp(second, "efghij") == 0,
           "recvmsg scatters into two buffers");
    close(pair[0]);
    close(pair[1]);
}

typedef struct {
    struct sockaddr_in address;
    int result;
} ClientJob;

static void *tcp_client(void *argument)
{
    ClientJob *job = (ClientJob *)argument;
    char reply[8] = {0};
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    job->result = fd >= 0 && connect(fd, (struct sockaddr *)&job->address, sizeof(job->address)) == 0 &&
                  send(fd, "ping", 4, 0) == 4 && recv(fd, reply, sizeof(reply), 0) == 4 && strcmp(reply, "pong") == 0;
    close(fd);
    return NULL;
}

static void test_tcp(void)
{
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t length = sizeof(address);
    int on = 1;
    int server = socket(AF_INET, SOCK_STREAM, 0);
    expect(setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == 0, "SO_REUSEADDR accepted");
    expect(bind(server, (struct sockaddr *)&address, sizeof(address)) == 0, "bind to loopback");
    expect(listen(server, 4) == 0, "listen");
    expect(getsockname(server, (struct sockaddr *)&address, &length) == 0 && address.sin_family == AF_INET &&
               ntohs(address.sin_port) != 0,
           "getsockname reports the port");
    int type = 0;
    socklen_t type_length = sizeof(type);
    expect(getsockopt(server, SOL_SOCKET, SO_TYPE, &type, &type_length) == 0 && type == SOCK_STREAM, "SO_TYPE is SOCK_STREAM");
    ClientJob job = {.address = address};
    pthread_t thread;
    expect(pthread_create(&thread, NULL, tcp_client, &job) == 0, "client thread started");
    expect((poll_one(server, POLLIN, LONG_TIMEOUT_MS) & POLLIN) != 0, "listener becomes readable on a connection");
    struct sockaddr_in peer;
    socklen_t peer_length = sizeof(peer);
    int connection = accept(server, (struct sockaddr *)&peer, &peer_length);
    char request[8] = {0};
    expect(connection >= 0 && peer.sin_family == AF_INET, "accept returns the peer address");
    expect(recv(connection, request, sizeof(request), 0) == 4 && strcmp(request, "ping") == 0, "server receives ping");
    expect(send(connection, "pong", 4, 0) == 4, "server sends pong");
    pthread_join(thread, NULL);
    expect(job.result == 1, "client completed the exchange");
    int error = -1;
    socklen_t error_length = sizeof(error);
    expect(getsockopt(connection, SOL_SOCKET, SO_ERROR, &error, &error_length) == 0 && error == 0, "SO_ERROR is clear");
    close(connection);
    close(server);
}

static void test_udp_and_nonblocking(void)
{
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t length = sizeof(address);
    char buffer[8] = {0};
    int receiver = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    int sender = socket(AF_INET, SOCK_DGRAM, 0);
    expect(bind(receiver, (struct sockaddr *)&address, sizeof(address)) == 0, "bind UDP");
    expect(getsockname(receiver, (struct sockaddr *)&address, &length) == 0, "UDP port");
    expect(recv(receiver, buffer, sizeof(buffer), 0) == -1 && errno == EAGAIN, "empty non-blocking recv fails with EAGAIN");
    expect(sendto(sender, "udp", 3, 0, (struct sockaddr *)&address, sizeof(address)) == 3, "sendto");
    expect((poll_one(receiver, POLLIN, LONG_TIMEOUT_MS) & POLLIN) != 0, "datagram makes the socket readable");
    struct sockaddr_in from;
    socklen_t from_length = sizeof(from);
    expect(recvfrom(receiver, buffer, sizeof(buffer), 0, (struct sockaddr *)&from, &from_length) == 3 &&
               strcmp(buffer, "udp") == 0 && from.sin_family == AF_INET,
           "recvfrom returns data and the sender");
    expect(poll_one(receiver, POLLIN, SHORT_TIMEOUT_MS) == 0, "no datagram left");
    close(receiver);
    close(sender);
}

static void *unix_client(void *argument)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strncpy(address.sun_path, (const char *)argument, sizeof(address.sun_path) - 1);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    int ok = fd >= 0 && connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0 && send(fd, "unix", 4, 0) == 4;
    close(fd);
    return ok ? NULL : (void *)1;
}

static void test_unix_path(void)
{
    char path[64];
    snprintf(path, sizeof(path), "/tmp/crossport-sockets-%d", (int)getpid());
    unlink(path);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    expect(bind(server, (struct sockaddr *)&address, sizeof(address)) == 0, "bind a Unix-domain path");
    expect(listen(server, 1) == 0, "listen on the path");
    pthread_t thread;
    expect(pthread_create(&thread, NULL, unix_client, path) == 0, "Unix client thread started");
    int connection = accept(server, NULL, NULL);
    char buffer[8] = {0};
    expect(connection >= 0 && recv(connection, buffer, sizeof(buffer), 0) == 4 && strcmp(buffer, "unix") == 0,
           "data arrives over the Unix path");
    void *client_result = NULL;
    pthread_join(thread, &client_result);
    expect(client_result == NULL, "Unix client succeeded");
    close(connection);
    close(server);
    unlink(path);
}

static void test_names(void)
{
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM, .ai_flags = AI_NUMERICHOST};
    struct addrinfo *found = NULL;
    expect(getaddrinfo("127.0.0.1", "8080", &hints, &found) == 0 && found != NULL, "getaddrinfo of a numeric address");
    const struct sockaddr_in *address = (const struct sockaddr_in *)found->ai_addr;
    expect(found->ai_family == AF_INET && found->ai_socktype == SOCK_STREAM && ntohs(address->sin_port) == 8080 &&
               ntohl(address->sin_addr.s_addr) == INADDR_LOOPBACK,
           "the result describes 127.0.0.1:8080");
    freeaddrinfo(found);
    expect(getaddrinfo("not a host name", NULL, &hints, &found) == EAI_NONAME, "a bad name fails with EAI_NONAME");
    printf("gai_strerror: %s\n", gai_strerror(EAI_NONAME));
    unsigned char bytes[16];
    char text[64];
    expect(inet_pton(AF_INET, "192.0.2.33", bytes) == 1 && bytes[3] == 33, "inet_pton for IPv4");
    expect(inet_ntop(AF_INET, bytes, text, sizeof(text)) != NULL && strcmp(text, "192.0.2.33") == 0, "inet_ntop for IPv4");
    expect(inet_pton(AF_INET6, "2001:db8::1", bytes) == 1 && bytes[0] == 0x20 && bytes[15] == 1, "inet_pton for IPv6");
    expect(inet_ntop(AF_INET6, bytes, text, sizeof(text)) != NULL && strcmp(text, "2001:db8::1") == 0, "inet_ntop for IPv6");
    expect(inet_pton(AF_INET, "999.1.1.1", bytes) == 0, "inet_pton rejects a bad address");
}

static void test_errors(void)
{
    int pipe_fds[2];
    char byte;
    expect(pipe(pipe_fds) == 0, "pipe for the error checks");
    expect(recv(pipe_fds[0], &byte, 1, 0) == -1 && errno == ENOTSOCK, "recv on a pipe fails with ENOTSOCK");
    expect(send(999, "x", 1, 0) == -1 && errno == EBADF, "send on an unknown descriptor fails with EBADF");
    close(pipe_fds[0]);
    close(pipe_fds[1]);
}

int main(void)
{
    test_socketpair();
    test_message_vectors();
    test_tcp();
    test_udp_and_nonblocking();
    test_unix_path();
    test_names();
    test_errors();
    printf("failures: %d\n", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
