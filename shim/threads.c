/*
 * File: shim/threads.c
 * Purpose: pthread thread lifecycle on Win32 threads: create, join, detach, self, exit and attributes
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * A pthread_t is a pointer to a heap Thread that outlives the Windows thread
 * until it is joined or detached. The state word arbitrates the races between
 * a thread finishing and another thread joining or detaching it.
 */

#include "internal.h"
#include "threads.h"
#include "tls.h"

#define EAGAIN_CODE 11
#define EINVAL_CODE 22
#define ESRCH_CODE 3
#define EDEADLK_CODE 35
#define DEFAULT_STACK_BYTES (8u * 1024u * 1024u) /* glibc's default */
#define THREAD_MAGIC 0x54485244u                 /* "THRD" */
#define ATTR_DETACHED 1u

typedef void *(SYSV *StartRoutine)(void *);

enum ThreadState { STATE_RUNNING = 0, STATE_DETACHED, STATE_FINISHED, STATE_JOINED };

typedef struct {
    uint32_t magic;
    volatile LONG state;
    HANDLE handle; /* NULL for the main thread */
    StartRoutine start;
    void *argument;
    void *result;
} Thread;

/* Lives inside the caller's 56-byte pthread_attr_t. */
typedef struct {
    uint32_t flags;
    size_t stack_bytes;
} Attr;

_Static_assert(sizeof(Attr) <= 56, "Attr must fit in glibc's pthread_attr_t");

static Thread *current_thread(void)
{
    return (Thread *)*tls_tcb_word(TCB_THREAD_OFFSET);
}

static Thread *new_thread(void)
{
    Thread *thread = (Thread *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Thread));
    if (thread != NULL) {
        thread->magic = THREAD_MAGIC;
    }
    return thread;
}

static void free_thread(Thread *thread)
{
    thread->magic = 0;
    HeapFree(GetProcessHeap(), 0, thread);
}

void threads_init_main(void)
{
    Thread *main_thread = new_thread();
    if (main_thread == NULL) {
        shim_fail("crossport: cannot allocate the main thread object\n");
    }
    *tls_tcb_word(TCB_THREAD_OFFSET) = main_thread;
}

/* Common tail of a thread: destructors, TLS teardown, and the finish/detach hand-off. */
static void thread_finish(Thread *thread)
{
    keys_run_destructors();
    tls_detach_thread();
    LONG previous = InterlockedCompareExchange(&thread->state, STATE_FINISHED, STATE_RUNNING);
    if (previous == STATE_DETACHED) {
        free_thread(thread); /* nobody will join; the handle was closed at detach time */
    }
}

static DWORD WINAPI thread_main(LPVOID parameter)
{
    Thread *thread = (Thread *)parameter;
    if (tls_attach_thread() == NULL) {
        thread->result = NULL;
        InterlockedCompareExchange(&thread->state, STATE_FINISHED, STATE_RUNNING);
        return 1;
    }
    *tls_tcb_word(TCB_THREAD_OFFSET) = thread;
    thread->result = thread->start(thread->argument);
    thread_finish(thread);
    return 0;
}

EXPORT SYSV int pthread_create(unsigned long *out, const Attr *attr, StartRoutine start, void *argument)
{
    Thread *thread = new_thread();
    if (thread == NULL) {
        return EAGAIN_CODE;
    }
    int is_detached = attr != NULL && (attr->flags & ATTR_DETACHED) != 0;
    thread->start = start;
    thread->argument = argument;
    thread->state = is_detached ? STATE_DETACHED : STATE_RUNNING;
    size_t stack_bytes = (attr != NULL && attr->stack_bytes != 0) ? attr->stack_bytes : DEFAULT_STACK_BYTES;
    *out = (unsigned long)(uintptr_t)thread; /* before the thread can run and possibly free itself */
    HANDLE handle = CreateThread(NULL, stack_bytes, thread_main, thread, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (handle == NULL) {
        free_thread(thread);
        return EAGAIN_CODE;
    }
    if (is_detached) {
        CloseHandle(handle); /* the thread may already be gone: do not touch `thread` again */
    } else {
        thread->handle = handle;
    }
    return 0;
}

static int is_joinable(const Thread *thread)
{
    return thread->magic == THREAD_MAGIC && thread->handle != NULL &&
           (thread->state == STATE_RUNNING || thread->state == STATE_FINISHED);
}

EXPORT SYSV int pthread_join(unsigned long id, void **result)
{
    Thread *thread = (Thread *)(uintptr_t)id;
    if (thread == NULL || thread->magic != THREAD_MAGIC) {
        return ESRCH_CODE;
    }
    if (thread == current_thread()) {
        return EDEADLK_CODE;
    }
    if (!is_joinable(thread)) {
        return EINVAL_CODE;
    }
    WaitForSingleObject(thread->handle, INFINITE);
    if (result != NULL) {
        *result = thread->result;
    }
    thread->state = STATE_JOINED;
    CloseHandle(thread->handle);
    free_thread(thread);
    return 0;
}

EXPORT SYSV int pthread_detach(unsigned long id)
{
    Thread *thread = (Thread *)(uintptr_t)id;
    if (thread == NULL || thread->magic != THREAD_MAGIC || thread->handle == NULL) {
        return ESRCH_CODE;
    }
    LONG previous = InterlockedCompareExchange(&thread->state, STATE_DETACHED, STATE_RUNNING);
    if (previous == STATE_RUNNING) {
        CloseHandle(thread->handle);
        return 0;
    }
    if (previous == STATE_FINISHED) {
        CloseHandle(thread->handle);
        free_thread(thread);
        return 0;
    }
    return EINVAL_CODE;
}

EXPORT SYSV unsigned long pthread_self(void)
{
    return (unsigned long)(uintptr_t)current_thread();
}

EXPORT SYSV int pthread_equal(unsigned long left, unsigned long right)
{
    return left == right;
}

EXPORT SYSV NORETURN void pthread_exit(void *result)
{
    Thread *thread = current_thread();
    thread->result = result;
    thread_finish(thread);
    ExitThread(0);
    __builtin_unreachable();
}

EXPORT SYSV int sched_yield(void)
{
    SwitchToThread();
    return 0;
}

EXPORT SYSV int pthread_attr_init(Attr *attr)
{
    attr->flags = 0;
    attr->stack_bytes = 0;
    return 0;
}

EXPORT SYSV int pthread_attr_destroy(Attr *attr)
{
    (void)attr;
    return 0;
}

EXPORT SYSV int pthread_attr_setdetachstate(Attr *attr, int state)
{
    if (state != 0 && state != 1) {
        return EINVAL_CODE;
    }
    attr->flags = state == 1 ? (attr->flags | ATTR_DETACHED) : (attr->flags & ~ATTR_DETACHED);
    return 0;
}

EXPORT SYSV int pthread_attr_setstacksize(Attr *attr, size_t bytes)
{
    attr->stack_bytes = bytes;
    return 0;
}
