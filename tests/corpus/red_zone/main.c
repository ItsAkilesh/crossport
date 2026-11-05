/*
 * File: tests/corpus/red_zone/main.c
 * Purpose: Leaf functions that keep data in the red zone, run on several threads while the X11 layer dispatches events
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * Built with the red zone (see build.sh). Each leaf function keeps its working data in the 128 bytes below the stack
 * pointer and makes no calls, as the compiler allows. Worker threads run them in a loop while the main thread
 * runs them between X11 calls that send and receive events, so Windows delivers messages on the same
 * thread. If anything wrote below a translated thread's stack pointer, a checksum would differ from Linux's.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include <X11/Xlib.h>

#define WORKERS 3
#define WORKER_ROUNDS 200000
#define EVENT_ROUNDS 3000
#define BLOCK_WORDS 24

/* A leaf function with all its locals in the red zone: volatile keeps the array in memory. */
__attribute__((noinline)) static uint32_t mix(uint32_t seed)
{
    volatile uint32_t block[BLOCK_WORDS];
    for (int i = 0; i < BLOCK_WORDS; i++) {
        block[i] = seed * (uint32_t)(i + 1) + (seed >> (i % 7));
    }
    uint32_t sum = 0;
    for (int i = 0; i < BLOCK_WORDS; i++) {
        sum = sum * 31u + block[i];
    }
    return sum;
}

static uint32_t g_results[WORKERS];

static void *worker(void *argument)
{
    uint32_t index = (uint32_t)(uintptr_t)argument;
    uint32_t value = index + 1;
    for (int round = 0; round < WORKER_ROUNDS; round++) {
        value = mix(value ^ (uint32_t)round);
    }
    g_results[index] = value;
    return NULL;
}

static uint32_t run_events(Display *display, Window window)
{
    Atom message = XInternAtom(display, "RED_ZONE_PING", False);
    uint32_t value = 99;
    uint32_t handled = 0;
    for (int round = 0; round < EVENT_ROUNDS; round++) {
        value = mix(value + (uint32_t)round);
        XEvent event = {0};
        event.xclient.type = ClientMessage;
        event.xclient.window = window;
        event.xclient.message_type = message;
        event.xclient.format = 32;
        event.xclient.data.l[0] = (long)(value & 0xffffu);
        XSendEvent(display, window, False, NoEventMask, &event);
        XFlush(display);
        do {
            XNextEvent(display, &event);
        } while (event.type != ClientMessage);
        value = mix(value ^ (uint32_t)event.xclient.data.l[0]);
        handled++;
    }
    printf("events: handled=%u checksum=%u\n", handled, value);
    return value;
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        puts("no display");
        return 1;
    }
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 50, 40, 0, 0, 0);
    XMapWindow(display, window);
    XSync(display, False);
    pthread_t threads[WORKERS];
    for (uintptr_t i = 0; i < WORKERS; i++) {
        pthread_create(&threads[i], NULL, worker, (void *)i);
    }
    run_events(display, window);
    for (int i = 0; i < WORKERS; i++) {
        pthread_join(threads[i], NULL);
        printf("worker %d: checksum=%u\n", i, g_results[i]);
    }
    XCloseDisplay(display);
    puts("done");
    return 0;
}
