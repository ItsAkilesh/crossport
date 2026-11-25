/*
 * File: shim/search_r.c
 * Purpose: The reentrant hash table (hcreate_r, hsearch_r, hdestroy_r): a table of string keys the caller owns through a struct hsearch_data
 * Author: Akilesh
 * Date: 2026-11-24
 *
 * Found by the survey (hcreate_r, hsearch_r and hdestroy_r each block 5 programs). Open addressing with linear probing over a table at least
 * twice the requested size, as glibc sizes its own. Keys are not copied: the caller keeps them alive, as the C library specifies.
 */

#include <string.h>

#include "libc_abi.h"

#define FNV_OFFSET 2166136261u
#define FNV_PRIME 16777619u
#define MINIMUM_SLOTS 8u

typedef struct {
    char *key;
    void *data;
} Entry;

/* struct hsearch_data: the table, its size and how many slots are used. */
typedef struct {
    Entry *table;
    unsigned size;
    unsigned filled;
} Table;

enum { ACTION_FIND = 0, ACTION_ENTER = 1 };

LIBC_FN void *sysv_calloc(size_t count, size_t size);
LIBC_FN void sysv_free(void *block);

static unsigned hash(const char *text)
{
    unsigned value = FNV_OFFSET;
    for (; *text != '\0'; text++) {
        value = (value ^ (unsigned char)*text) * FNV_PRIME;
    }
    return value;
}

LIBC_FN int sysv_hcreate_r(size_t count, Table *table)
{
    if (table->table != NULL) {
        return shim_fail(LINUX_EINVAL);
    }
    unsigned slots = MINIMUM_SLOTS;
    while (slots < count * 2 && slots < 0x40000000u) {
        slots *= 2;
    }
    table->table = sysv_calloc(slots, sizeof(Entry));
    if (table->table == NULL) {
        return shim_fail(LINUX_ENOMEM);
    }
    table->size = slots;
    table->filled = 0;
    return 1;
}

LIBC_FN void sysv_hdestroy_r(Table *table)
{
    sysv_free(table->table);
    table->table = NULL;
    table->size = table->filled = 0;
}

/* Returns the slot holding `key`, or the empty slot where it belongs, or NULL when the table is full of other keys. */
static Entry *probe(Table *table, const char *key)
{
    unsigned start = hash(key) % table->size;
    for (unsigned step = 0; step < table->size; step++) {
        Entry *slot = &table->table[(start + step) % table->size];
        if (slot->key == NULL || strcmp(slot->key, key) == 0) {
            return slot;
        }
    }
    return NULL;
}

LIBC_FN int sysv_hsearch_r(Entry item, int action, Entry **found, Table *table)
{
    Entry *slot = table->table != NULL ? probe(table, item.key) : NULL;
    *found = NULL;
    if (slot != NULL && slot->key != NULL) {
        *found = slot;
        return 1;
    }
    if (action != ACTION_ENTER) {
        return shim_fail(LINUX_ESRCH);
    }
    if (slot == NULL || table->filled >= table->size) {
        return shim_fail(LINUX_ENOMEM);
    }
    *slot = item;
    table->filled++;
    *found = slot;
    return 1;
}
