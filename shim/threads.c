/*
 * File: shim/threads.c
 * Purpose: pthread thread lifecycle on Win32 threads: create, join, detach, self, exit and attributes
 * Author: Akilesh
 * Date: 2026-11-24
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
#define EBUSY_CODE 16
#define ETIMEDOUT_CODE 110
#define FILETIME_TICKS_PER_MILLISECOND 10000ull
#define FILETIME_UNIX_EPOCH_TICKS 116444736000000000ull
#define NANOSECONDS_PER_MILLISECOND 1000000
#define MILLISECONDS_PER_SECOND 1000
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

/* Records the calling thread's pthread object where pthread_self() and inlined reads of glibc's THREAD_SELF find it. */
static void set_current_thread(Thread *thread)
{
    *tls_tcb_word(TCB_THREAD_OFFSET) = thread;
    *tls_tcb_word(TCB_PTHREAD_SELF_OFFSET) = thread;
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
        shim_die("crossport: cannot allocate the main thread object\n");
    }
    set_current_thread(main_thread);
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
    set_current_thread(thread);
    thread->result = thread->start(thread->argument);
    thread_finish(thread);
    return 0;
}

EXPORT SYSV int pthread_create(uint64_t *out, const Attr *attr, StartRoutine start, void *argument)
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
    *out = (uint64_t)(uintptr_t)thread; /* before the thread can run and possibly free itself */
    /* Without STACK_SIZE_PARAM_IS_A_RESERVATION the size is committed at once, as a Linux stack effectively is. */
    HANDLE handle = CreateThread(NULL, stack_bytes, thread_main, thread, 0, NULL);
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

/* Waits up to `wait_ms` for the thread and collects its result. Returns `busy_code` when it is still running after that. */
static int join_thread(uint64_t id, void **result, DWORD wait_ms, int busy_code)
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
    if (WaitForSingleObject(thread->handle, wait_ms) != WAIT_OBJECT_0) {
        return busy_code;
    }
    if (result != NULL) {
        *result = thread->result;
    }
    thread->state = STATE_JOINED;
    CloseHandle(thread->handle);
    free_thread(thread);
    return 0;
}

EXPORT SYSV int pthread_join(uint64_t id, void **result)
{
    return join_thread(id, result, INFINITE, EBUSY_CODE);
}

/* pthread_tryjoin_np: joins only if the thread has already finished (EBUSY otherwise). */
EXPORT SYSV int pthread_tryjoin_np(uint64_t id, void **result)
{
    return join_thread(id, result, 0, EBUSY_CODE);
}

/* Milliseconds from now until an absolute CLOCK_REALTIME time (seconds, nanoseconds); 0 when it has passed. */
static DWORD milliseconds_until(const int64_t *deadline)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    uint64_t ticks = ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
    int64_t now_ms = (int64_t)((ticks - FILETIME_UNIX_EPOCH_TICKS) / FILETIME_TICKS_PER_MILLISECOND);
    int64_t deadline_ms = deadline[0] * MILLISECONDS_PER_SECOND + deadline[1] / NANOSECONDS_PER_MILLISECOND;
    int64_t left = deadline_ms - now_ms;
    return left <= 0 ? 0 : (left > 0xFFFFFFFEll ? 0xFFFFFFFEu : (DWORD)left);
}

/* pthread_timedjoin_np: waits until the absolute time, then gives up with ETIMEDOUT. */
EXPORT SYSV int pthread_timedjoin_np(uint64_t id, void **result, const int64_t *deadline)
{
    return join_thread(id, result, deadline != NULL ? milliseconds_until(deadline) : INFINITE, ETIMEDOUT_CODE);
}

EXPORT SYSV int pthread_detach(uint64_t id)
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

EXPORT SYSV uint64_t pthread_self(void)
{
    return (uint64_t)(uintptr_t)current_thread();
}

EXPORT SYSV int pthread_equal(uint64_t left, uint64_t right)
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
