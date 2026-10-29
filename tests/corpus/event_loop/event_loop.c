/*
 * File: tests/corpus/event_loop/event_loop.c
 * Purpose: Exercise epoll with pipes, eventfd and timerfd: the pieces a GLib, libuv or Qt event loop is built from
 * Author: Akilesh
 * Date: 2026-10-29
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define LONG_TIMEOUT_MS 5000
#define ONE_SHOT_NS 30000000
#define PERIOD_NS 20000000
#define WAIT_FOR_TICKS_US 130000
#define MIN_TICKS 3
#define PIPE_TAG 11
#define EVENT_TAG 22
#define TIMER_TAG 33

static int g_failures;

static void expect(int condition, const char *what)
{
    printf("%s: %s\n", what, condition ? "ok" : "FAILED");
    g_failures += !condition;
}

static int add(int epfd, int fd, uint32_t events, uint64_t tag)
{
    struct epoll_event event = {.events = events, .data.u64 = tag};
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
}

static void test_pipe_and_eventfd(int epfd)
{
    int fds[2];
    struct epoll_event ready[4];
    expect(pipe(fds) == 0, "pipe created");
    expect(add(epfd, fds[0], EPOLLIN, PIPE_TAG) == 0, "pipe added to epoll");
    int event = eventfd(0, EFD_NONBLOCK);
    expect(add(epfd, event, EPOLLIN, EVENT_TAG) == 0, "eventfd added to epoll");
    expect(add(epfd, event, EPOLLIN, EVENT_TAG) == -1 && errno == EEXIST, "adding twice fails with EEXIST");
    expect(epoll_wait(epfd, ready, 4, 0) == 0, "nothing is ready yet");
    expect(write(fds[1], "x", 1) == 1, "write to the pipe");
    int count = epoll_wait(epfd, ready, 4, LONG_TIMEOUT_MS);
    expect(count == 1 && ready[0].data.u64 == PIPE_TAG && (ready[0].events & EPOLLIN) != 0, "epoll reports the pipe");
    char byte;
    expect(read(fds[0], &byte, 1) == 1, "drain the pipe");
    uint64_t one = 1;
    expect(write(event, &one, sizeof(one)) == 8, "signal the eventfd");
    count = epoll_wait(epfd, ready, 4, LONG_TIMEOUT_MS);
    expect(count == 1 && ready[0].data.u64 == EVENT_TAG, "epoll reports the eventfd");
    expect(read(event, &one, sizeof(one)) == 8, "drain the eventfd");
    expect(epoll_ctl(epfd, EPOLL_CTL_DEL, event, NULL) == 0, "remove the eventfd");
    expect(epoll_ctl(epfd, EPOLL_CTL_DEL, event, NULL) == -1 && errno == ENOENT, "removing again fails with ENOENT");
    close(event);
    epoll_ctl(epfd, EPOLL_CTL_DEL, fds[0], NULL);
    close(fds[0]);
    close(fds[1]);
}

static void test_one_shot_timer(int epfd)
{
    struct epoll_event ready[2];
    struct itimerspec spec = {.it_value = {.tv_nsec = ONE_SHOT_NS}};
    uint64_t expirations = 0;
    int timer = timerfd_create(CLOCK_MONOTONIC, 0);
    expect(timer >= 0, "timerfd created");
    expect(add(epfd, timer, EPOLLIN, TIMER_TAG) == 0, "timerfd added to epoll");
    expect(epoll_wait(epfd, ready, 2, 0) == 0, "an unarmed timer is not ready");
    expect(timerfd_settime(timer, 0, &spec, NULL) == 0, "arm a one-shot timer");
    int count = epoll_wait(epfd, ready, 2, LONG_TIMEOUT_MS);
    expect(count == 1 && ready[0].data.u64 == TIMER_TAG, "epoll reports the expired timer");
    expect(read(timer, &expirations, sizeof(expirations)) == 8 && expirations == 1, "one expiration");
    expect(epoll_wait(epfd, ready, 2, 0) == 0, "a one-shot timer does not fire again");
    epoll_ctl(epfd, EPOLL_CTL_DEL, timer, NULL);
    close(timer);
}

static void test_periodic_timer(void)
{
    struct itimerspec spec = {.it_interval = {.tv_nsec = PERIOD_NS}, .it_value = {.tv_nsec = PERIOD_NS}};
    uint64_t expirations = 0;
    int timer = timerfd_create(CLOCK_MONOTONIC, 0);
    expect(timerfd_settime(timer, 0, &spec, NULL) == 0, "arm a periodic timer");
    usleep(WAIT_FOR_TICKS_US);
    expect(read(timer, &expirations, sizeof(expirations)) == 8 && expirations >= MIN_TICKS, "missed periods are counted");
    struct itimerspec left;
    expect(timerfd_gettime(timer, &left) == 0 && left.it_interval.tv_nsec == PERIOD_NS, "timerfd_gettime reports the interval");
    memset(&spec, 0, sizeof(spec));
    expect(timerfd_settime(timer, 0, &spec, NULL) == 0, "disarm the timer");
    close(timer);
}

static void test_one_shot_epoll(void)
{
    int epfd = epoll_create1(0);
    int event = eventfd(0, EFD_NONBLOCK);
    struct epoll_event ready[2];
    uint64_t one = 1;
    expect(add(epfd, event, EPOLLIN | EPOLLONESHOT, EVENT_TAG) == 0, "EPOLLONESHOT watch added");
    expect(write(event, &one, sizeof(one)) == 8, "signal for the one-shot watch");
    expect(epoll_wait(epfd, ready, 2, LONG_TIMEOUT_MS) == 1, "the one-shot watch fires once");
    expect(epoll_wait(epfd, ready, 2, 0) == 0, "and is then disabled");
    struct epoll_event again = {.events = EPOLLIN | EPOLLONESHOT, .data.u64 = EVENT_TAG};
    expect(epoll_ctl(epfd, EPOLL_CTL_MOD, event, &again) == 0, "EPOLL_CTL_MOD re-arms it");
    expect(epoll_wait(epfd, ready, 2, LONG_TIMEOUT_MS) == 1, "and it fires again");
    close(event);
    close(epfd);
}

int main(void)
{
    int epfd = epoll_create1(0);
    expect(epfd >= 0, "epoll instance created");
    test_pipe_and_eventfd(epfd);
    test_one_shot_timer(epfd);
    test_periodic_timer();
    close(epfd);
    test_one_shot_epoll();
    printf("failures: %d\n", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
