/*
 * File: tests/corpus/posix_io/posix_io.c
 * Purpose: Exercise pipes, eventfd, dup, fcntl, ioctl, poll and select, the descriptor calls event loops are built on
 * Author: Akilesh
 * Date: 2026-10-29
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#define WAKE_DELAY_US 50000
#define LONG_TIMEOUT_MS 5000
#define SHORT_TIMEOUT_MS 20

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

static void test_pipe(void)
{
    int fds[2];
    char buffer[16] = {0};
    expect(pipe(fds) == 0, "pipe created");
    expect(poll_one(fds[0], POLLIN, 0) == 0, "empty pipe is not readable");
    expect(poll_one(fds[1], POLLOUT, 0) == POLLOUT, "pipe is writable");
    expect(write(fds[1], "hello", 5) == 5, "write to pipe");
    expect((poll_one(fds[0], POLLIN, 0) & POLLIN) != 0, "pipe is readable after a write");
    int available = 0;
    expect(ioctl(fds[0], FIONREAD, &available) == 0 && available == 5, "FIONREAD counts the bytes");
    expect(read(fds[0], buffer, sizeof(buffer)) == 5 && strcmp(buffer, "hello") == 0, "read from pipe");
    close(fds[1]);
    expect((poll_one(fds[0], POLLIN, 0) & POLLHUP) != 0, "closed pipe reports hang-up");
    expect(read(fds[0], buffer, sizeof(buffer)) == 0, "read at end of file returns 0");
    close(fds[0]);
}

static void test_nonblocking_pipe(void)
{
    int fds[2];
    char byte;
    expect(pipe2(fds, O_NONBLOCK) == 0, "pipe2 with O_NONBLOCK");
    expect((fcntl(fds[0], F_GETFL) & O_NONBLOCK) != 0, "F_GETFL shows O_NONBLOCK");
    expect(read(fds[0], &byte, 1) == -1 && errno == EAGAIN, "empty non-blocking read fails with EAGAIN");
    int flags = fcntl(fds[0], F_GETFL);
    expect(fcntl(fds[0], F_SETFL, flags & ~O_NONBLOCK) == 0 && (fcntl(fds[0], F_GETFL) & O_NONBLOCK) == 0,
           "F_SETFL clears O_NONBLOCK");
    close(fds[0]);
    close(fds[1]);
}

static void test_eventfd(void)
{
    uint64_t value = 0;
    int event = eventfd(0, EFD_NONBLOCK);
    expect(event >= 0, "eventfd created");
    expect(poll_one(event, POLLIN, 0) == 0, "zero counter is not readable");
    value = 3;
    expect(write(event, &value, sizeof(value)) == 8, "eventfd write");
    value = 4;
    expect(write(event, &value, sizeof(value)) == 8, "second eventfd write");
    expect((poll_one(event, POLLIN, 0) & POLLIN) != 0, "counter makes it readable");
    expect(read(event, &value, sizeof(value)) == 8 && value == 7, "read returns the sum");
    expect(read(event, &value, sizeof(value)) == -1 && errno == EAGAIN, "drained read fails with EAGAIN");
    close(event);
    int semaphore = eventfd(2, EFD_SEMAPHORE | EFD_NONBLOCK);
    expect(read(semaphore, &value, sizeof(value)) == 8 && value == 1, "semaphore read takes one");
    expect(read(semaphore, &value, sizeof(value)) == 8 && value == 1, "semaphore read takes one again");
    expect(read(semaphore, &value, sizeof(value)) == -1 && errno == EAGAIN, "semaphore is empty");
    close(semaphore);
}

static void test_dup(void)
{
    int fds[2];
    char buffer[8] = {0};
    expect(pipe(fds) == 0, "pipe for dup");
    int copy = dup(fds[1]);
    expect(copy >= 0 && copy != fds[1], "dup returns a new descriptor");
    expect(write(copy, "dup", 3) == 3, "write through the duplicate");
    close(copy);
    expect(read(fds[0], buffer, sizeof(buffer)) == 3 && strcmp(buffer, "dup") == 0, "data arrives from the duplicate");
    int target = 20;
    expect(dup2(fds[1], target) == target, "dup2 to a chosen number");
    expect(write(target, "two", 3) == 3 && read(fds[0], buffer, 3) == 3, "write through the dup2 descriptor");
    close(target);
    close(fds[0]);
    close(fds[1]);
}

static void test_select(void)
{
    int fds[2];
    fd_set reads;
    struct timeval timeout = {0, 0};
    expect(pipe(fds) == 0, "pipe for select");
    FD_ZERO(&reads);
    FD_SET(fds[0], &reads);
    expect(select(fds[0] + 1, &reads, NULL, NULL, &timeout) == 0, "select times out on an empty pipe");
    expect(write(fds[1], "x", 1) == 1, "write for select");
    FD_ZERO(&reads);
    FD_SET(fds[0], &reads);
    expect(select(fds[0] + 1, &reads, NULL, NULL, &timeout) == 1 && FD_ISSET(fds[0], &reads), "select sees the byte");
    close(fds[0]);
    close(fds[1]);
}

static void *wake_later(void *argument)
{
    uint64_t one = 1;
    usleep(WAKE_DELAY_US);
    if (write(*(int *)argument, &one, sizeof(one)) != sizeof(one)) {
        g_failures++;
    }
    return NULL;
}

static void test_wake_a_blocked_poll(void)
{
    int event = eventfd(0, 0);
    pthread_t thread;
    expect(pthread_create(&thread, NULL, wake_later, &event) == 0, "waker thread started");
    expect((poll_one(event, POLLIN, LONG_TIMEOUT_MS) & POLLIN) != 0, "poll is woken by another thread");
    uint64_t value = 0;
    expect(read(event, &value, sizeof(value)) == 8 && value == 1, "woken read sees the count");
    pthread_join(thread, NULL);
    expect(poll_one(event, POLLIN, SHORT_TIMEOUT_MS) == 0, "poll times out when nothing happens");
    close(event);
}

static void test_bad_descriptors(void)
{
    expect(poll_one(999, POLLIN, 0) == POLLNVAL, "poll flags an unknown descriptor");
    expect(fcntl(999, F_GETFL) == -1 && errno == EBADF, "fcntl on an unknown descriptor fails with EBADF");
    char byte;
    expect(read(999, &byte, 1) == -1 && errno == EBADF, "read on an unknown descriptor fails with EBADF");
}

int main(void)
{
    test_pipe();
    test_nonblocking_pipe();
    test_eventfd();
    test_dup();
    test_select();
    test_wake_a_blocked_poll();
    test_bad_descriptors();
    printf("failures: %d\n", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
