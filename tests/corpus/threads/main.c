/*
 * File: tests/corpus/threads/main.c
 * Purpose: Milestone 3 corpus program: threads with thread-local state, a mutex-protected total and ordered, deterministic output
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include <pthread.h>
#include <stdio.h>

#include "libcount.h"
#include "numfmt.h"

#define THREAD_COUNT 4
#define ITERATIONS 1000
#define NUMBER_BUFFER_SIZE 32
#define LOCAL_INITIAL 42

typedef struct {
    long id;
    long initial_seen;
    long local_total;
    long lib_total;
} WorkerResult;

static __thread long local_counter = LOCAL_INITIAL;
static long shared_total;
static pthread_mutex_t shared_lock = PTHREAD_MUTEX_INITIALIZER;

static void print_number(const char *label, long value)
{
    char text[NUMBER_BUFFER_SIZE];
    puts(label);
    format_long(text, sizeof(text), value);
    puts(text);
}

static void *worker(void *argument)
{
    WorkerResult *result = (WorkerResult *)argument;
    result->initial_seen = local_counter;
    for (int i = 0; i < ITERATIONS; i++) {
        local_counter += result->id;
        pthread_mutex_lock(&shared_lock);
        shared_total++;
        pthread_mutex_unlock(&shared_lock);
        result->lib_total = count_add(1);
    }
    result->local_total = local_counter;
    return result;
}

int main(void)
{
    pthread_t threads[THREAD_COUNT];
    WorkerResult results[THREAD_COUNT];

    for (long i = 0; i < THREAD_COUNT; i++) {
        results[i].id = i + 1;
        if (pthread_create(&threads[i], NULL, worker, &results[i]) != 0) {
            puts("pthread_create failed");
            return 1;
        }
    }
    for (long i = 0; i < THREAD_COUNT; i++) {
        void *returned = NULL;
        pthread_join(threads[i], &returned);
        puts("thread:");
        print_number("  id:", results[i].id);
        print_number("  initial local:", results[i].initial_seen);
        print_number("  final local:", results[i].local_total);
        print_number("  final library counter:", results[i].lib_total);
        print_number("  returned its own result:", returned == &results[i]);
    }
    print_number("shared total:", shared_total);
    print_number("main local untouched:", local_counter);
    print_number("main library counter untouched:", count_add(0));
    return 0;
}
