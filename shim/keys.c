/*
 * File: shim/keys.c
 * Purpose: pthread thread-specific data (keys with destructors) and the per-thread errno
 * Author: Akilesh
 * Date: 2026-10-28
 */

#include "internal.h"
#include "threads.h"
#include "tls.h"

#define EAGAIN_CODE 11
#define ENOMEM_CODE 12
#define EINVAL_CODE 22
#define MAX_KEYS 1024u
#define DESTRUCTOR_ROUNDS 4 /* PTHREAD_DESTRUCTOR_ITERATIONS */

typedef void (SYSV *KeyDestructor)(void *);

typedef struct {
    int in_use;
    KeyDestructor destructor;
} KeySlot;

static KeySlot g_keys[MAX_KEYS];
static SRWLOCK g_keys_lock = SRWLOCK_INIT;

static void **thread_values(void)
{
    return (void **)*tls_tcb_word(TCB_KEYS_OFFSET);
}

EXPORT SYSV int pthread_key_create(unsigned *key, KeyDestructor destructor)
{
    int result = EAGAIN_CODE;
    AcquireSRWLockExclusive(&g_keys_lock);
    for (unsigned index = 0; index < MAX_KEYS; index++) {
        if (!g_keys[index].in_use) {
            g_keys[index].in_use = 1;
            g_keys[index].destructor = destructor;
            *key = index;
            result = 0;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_keys_lock);
    return result;
}

EXPORT SYSV int pthread_key_delete(unsigned key)
{
    int result = EINVAL_CODE;
    AcquireSRWLockExclusive(&g_keys_lock);
    if (key < MAX_KEYS && g_keys[key].in_use) {
        g_keys[key].in_use = 0;
        g_keys[key].destructor = NULL;
        result = 0;
    }
    ReleaseSRWLockExclusive(&g_keys_lock);
    return result;
}

EXPORT SYSV void *pthread_getspecific(unsigned key)
{
    void **values = thread_values();
    return (values != NULL && key < MAX_KEYS) ? values[key] : NULL;
}

EXPORT SYSV int pthread_setspecific(unsigned key, const void *value)
{
    if (key >= MAX_KEYS || !g_keys[key].in_use) {
        return EINVAL_CODE;
    }
    void **values = thread_values();
    if (values == NULL) {
        values = (void **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_KEYS * sizeof(void *));
        if (values == NULL) {
            return ENOMEM_CODE;
        }
        *tls_tcb_word(TCB_KEYS_OFFSET) = values;
    }
    values[key] = (void *)value;
    return 0;
}

void keys_run_destructors(void)
{
    void **values = thread_values();
    if (values == NULL) {
        return;
    }
    for (int round = 0; round < DESTRUCTOR_ROUNDS; round++) {
        int ran_any = 0;
        for (unsigned key = 0; key < MAX_KEYS; key++) {
            void *value = values[key];
            if (value == NULL) {
                continue;
            }
            AcquireSRWLockShared(&g_keys_lock);
            KeyDestructor destructor = g_keys[key].in_use ? g_keys[key].destructor : NULL;
            ReleaseSRWLockShared(&g_keys_lock);
            if (destructor != NULL) {
                values[key] = NULL;
                destructor(value);
                ran_any = 1;
            }
        }
        if (!ran_any) {
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, values);
    *tls_tcb_word(TCB_KEYS_OFFSET) = NULL;
}

EXPORT SYSV int *__errno_location(void)
{
    return (int *)tls_tcb_word(TCB_ERRNO_OFFSET);
}
