/*
 * File: shim/tls_dtors.c
 * Purpose: __cxa_thread_atexit_impl: destructors of C++ thread_local objects, run when their thread exits
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Each thread keeps a list of (destructor, object) pairs in a pthread key. The key's destructor runs the
 * list when a created thread exits. The main thread never exits through that path, so shim_exit calls
 * shim_run_thread_destructors for it, before the atexit handlers, as glibc does.
 */

#include "libc_abi.h"

typedef void (SYSV *Destructor)(void *);

typedef struct Entry {
    Destructor destructor;
    void *object;
    struct Entry *next;
} Entry;

SYSV int pthread_key_create(unsigned *key, void (SYSV *destructor)(void *));
SYSV void *pthread_getspecific(unsigned key);
SYSV int pthread_setspecific(unsigned key, const void *value);
void shim_run_thread_destructors(void);

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static unsigned g_key;
static int g_has_key;

static void SYSV run_list(void *head)
{
    for (Entry *entry = (Entry *)head; entry != NULL;) {
        Entry *next = entry->next;
        entry->destructor(entry->object);
        HeapFree(GetProcessHeap(), 0, entry);
        entry = next;
    }
}

static BOOL CALLBACK create_key(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    g_has_key = pthread_key_create(&g_key, run_list) == 0;
    return TRUE;
}

/* Runs the calling thread's registered destructors, newest first, including any they register. */
void shim_run_thread_destructors(void)
{
    InitOnceExecuteOnce(&g_once, create_key, NULL, NULL);
    while (g_has_key) {
        void *head = pthread_getspecific(g_key);
        if (head == NULL) {
            break;
        }
        pthread_setspecific(g_key, NULL);
        run_list(head);
    }
}

LIBC_FN int sysv___cxa_thread_atexit_impl(Destructor destructor, void *object, void *dso)
{
    (void)dso;
    InitOnceExecuteOnce(&g_once, create_key, NULL, NULL);
    Entry *entry = (Entry *)HeapAlloc(GetProcessHeap(), 0, sizeof(Entry));
    if (!g_has_key || entry == NULL) {
        HeapFree(GetProcessHeap(), 0, entry);
        return -1;
    }
    *entry = (Entry){.destructor = destructor, .object = object, .next = (Entry *)pthread_getspecific(g_key)};
    return pthread_setspecific(g_key, entry);
}
