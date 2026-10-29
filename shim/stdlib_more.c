/*
 * File: shim/stdlib_more.c
 * Purpose: div, drand48 family, qsort_r, lfind/lsearch, tsearch, hsearch, reallocarray, getauxval and heap introspection
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "libc_abi.h"

#define RAND48_MULTIPLIER 0x5deece66dull
#define RAND48_ADDEND 0xbull
#define RAND48_MASK 0xffffffffffffull
#define RAND48_SEED_LOW 0x330eull
#define RAND48_BITS 48
#define HEAP_ALIGNED_FLAG 0
#define MIN_SORT_RUN 1

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);
LIBC_FN void *sysv_realloc(void *block, size_t size);
LIBC_FN char *sysv_getcwd(char *buffer, size_t size);
LIBC_FN char *sysv_strdup(const char *text);
LIBC_FN int sysv_unsetenv(const char *name);
LIBC_FN int64_t sysv_strtoll(const char *text, char **end, int base);
LIBC_FN uint64_t sysv_strtoull(const char *text, char **end, int base);

typedef struct {
    int quotient;
    int remainder;
} DivT;

typedef struct {
    int64_t quotient;
    int64_t remainder;
} LDivT;

LIBC_FN DivT sysv_div(int numerator, int denominator)
{
    return (DivT){numerator / denominator, numerator % denominator};
}

LIBC_FN LDivT sysv_ldiv(int64_t numerator, int64_t denominator)
{
    return (LDivT){numerator / denominator, numerator % denominator};
}

LIBC_FN LDivT sysv_lldiv(int64_t numerator, int64_t denominator)
{
    return sysv_ldiv(numerator, denominator);
}

LIBC_FN int64_t sysv_strtoimax(const char *text, char **end, int base)
{
    return sysv_strtoll(text, end, base);
}

LIBC_FN uint64_t sysv_strtoumax(const char *text, char **end, int base)
{
    return sysv_strtoull(text, end, base);
}

LIBC_FN int64_t sysv___isoc23_strtoimax(const char *text, char **end, int base)
{
    return sysv_strtoll(text, end, base);
}

LIBC_FN uint64_t sysv___isoc23_strtoumax(const char *text, char **end, int base)
{
    return sysv_strtoull(text, end, base);
}

LIBC_FN void *sysv_reallocarray(void *block, size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size) {
        shim_set_errno(LINUX_ENOMEM);
        return NULL;
    }
    return sysv_realloc(block, count * size);
}

LIBC_FN size_t sysv_malloc_usable_size(void *block)
{
    return block == NULL ? 0 : HeapSize(GetProcessHeap(), 0, block);
}

LIBC_FN int sysv_malloc_trim(size_t pad)
{
    (void)pad;
    return 0;
}

/* struct mallinfo2 is 80 bytes returned through a hidden pointer; the shim keeps no statistics. */
LIBC_FN void sysv_mallinfo2(uint64_t *result)
{
    memset(result, 0, 10 * sizeof(uint64_t));
}

LIBC_FN int sysv_getpagesize(void)
{
    return 4096;
}

LIBC_FN int sysv_gettid(void)
{
    return (int)GetCurrentThreadId();
}

/* No auxiliary vector is exposed; every lookup reports "not found". */
LIBC_FN uint64_t sysv_getauxval(uint64_t type)
{
    (void)type;
    shim_set_errno(LINUX_ENOENT);
    return 0;
}

LIBC_FN char *sysv_get_current_dir_name(void)
{
    char buffer[4096];
    return sysv_getcwd(buffer, sizeof(buffer)) != NULL ? sysv_strdup(buffer) : NULL;
}

LIBC_FN int sysv_clearenv(void)
{
    extern char **crossport_data_environ;
    while (crossport_data_environ != NULL && crossport_data_environ[0] != NULL) {
        char name[256];
        const char *entry = crossport_data_environ[0];
        size_t length = 0;
        while (entry[length] != '=' && entry[length] != '\0' && length < sizeof(name) - 1) {
            name[length] = entry[length];
            length++;
        }
        name[length] = '\0';
        if (sysv_unsetenv(name) != 0) {
            break;
        }
    }
    return 0;
}

/* The 48-bit linear congruential generator of drand48 and its relatives. */
static uint64_t g_state = (0x1234abcdull << 16) | RAND48_SEED_LOW;

static uint64_t advance(uint64_t *state)
{
    *state = (*state * RAND48_MULTIPLIER + RAND48_ADDEND) & RAND48_MASK;
    return *state;
}

static uint64_t from_words(const uint16_t words[3])
{
    return ((uint64_t)words[2] << 32) | ((uint64_t)words[1] << 16) | words[0];
}

static void to_words(uint64_t state, uint16_t words[3])
{
    words[0] = (uint16_t)state;
    words[1] = (uint16_t)(state >> 16);
    words[2] = (uint16_t)(state >> 32);
}

static double to_unit_interval(uint64_t state)
{
    return (double)state / (double)(1ull << RAND48_BITS);
}

LIBC_FN void sysv_srand48(int64_t seed)
{
    g_state = (((uint64_t)seed & 0xffffffffull) << 16) | RAND48_SEED_LOW;
}

LIBC_FN double sysv_drand48(void)
{
    return to_unit_interval(advance(&g_state));
}

LIBC_FN int64_t sysv_lrand48(void)
{
    return (int64_t)(advance(&g_state) >> 17);
}

LIBC_FN int64_t sysv_mrand48(void)
{
    return (int32_t)(advance(&g_state) >> 16);
}

LIBC_FN double sysv_erand48(uint16_t words[3])
{
    uint64_t state = from_words(words);
    advance(&state);
    to_words(state, words);
    return to_unit_interval(state);
}

LIBC_FN int64_t sysv_nrand48(uint16_t words[3])
{
    uint64_t state = from_words(words);
    advance(&state);
    to_words(state, words);
    return (int64_t)(state >> 17);
}

LIBC_FN int64_t sysv_jrand48(uint16_t words[3])
{
    uint64_t state = from_words(words);
    advance(&state);
    to_words(state, words);
    return (int32_t)(state >> 16);
}

/* Stable merge sort with a context argument (qsort_r); the same ordering guarantee as the shim's qsort. */
typedef int (SYSV *CompareWithArgument)(const void *, const void *, void *);

static void merge_runs(uint8_t *items, uint8_t *scratch, size_t left, size_t middle, size_t right, size_t size,
                       CompareWithArgument compare, void *argument)
{
    size_t i = left;
    size_t j = middle;
    size_t out = left;
    while (i < middle && j < right) {
        const uint8_t *chosen = compare(items + i * size, items + j * size, argument) <= 0 ? items + (i++) * size
                                                                                          : items + (j++) * size;
        memcpy(scratch + (out++) * size, chosen, size);
    }
    while (i < middle) {
        memcpy(scratch + (out++) * size, items + (i++) * size, size);
    }
    while (j < right) {
        memcpy(scratch + (out++) * size, items + (j++) * size, size);
    }
    memcpy(items + left * size, scratch + left * size, (right - left) * size);
}

LIBC_FN void sysv_qsort_r(void *base, size_t count, size_t size, CompareWithArgument compare, void *argument)
{
    if (count < 2 || size == 0) {
        return;
    }
    uint8_t *scratch = (uint8_t *)sysv_malloc(count * size);
    if (scratch == NULL) {
        return;
    }
    for (size_t width = MIN_SORT_RUN; width < count; width *= 2) {
        for (size_t left = 0; left < count; left += 2 * width) {
            size_t middle = left + width < count ? left + width : count;
            size_t right = left + 2 * width < count ? left + 2 * width : count;
            merge_runs((uint8_t *)base, scratch, left, middle, right, size, compare, argument);
        }
    }
    sysv_free(scratch);
}

typedef int (SYSV *Compare)(const void *, const void *);

LIBC_FN void *sysv_lfind(const void *key, const void *base, size_t *count, size_t size, Compare compare)
{
    const uint8_t *items = (const uint8_t *)base;
    for (size_t i = 0; i < *count; i++) {
        if (compare(key, items + i * size) == 0) {
            return (void *)(items + i * size);
        }
    }
    return NULL;
}

LIBC_FN void *sysv_lsearch(const void *key, void *base, size_t *count, size_t size, Compare compare)
{
    void *found = sysv_lfind(key, base, count, size, compare);
    if (found != NULL) {
        return found;
    }
    void *slot = (uint8_t *)base + *count * size;
    memcpy(slot, key, size);
    (*count)++;
    return slot;
}

/* tsearch: an unbalanced binary search tree; the walk order is the key order, as with glibc's balanced one. */
typedef struct Node {
    const void *key;
    struct Node *left;
    struct Node *right;
} Node;

typedef void (SYSV *WalkAction)(const void *, int, int);
enum { VISIT_PREORDER = 0, VISIT_POSTORDER = 1, VISIT_ENDORDER = 2, VISIT_LEAF = 3 };

LIBC_FN void *sysv_tfind(const void *key, void *const *root, Compare compare)
{
    for (Node *at = root != NULL ? (Node *)*root : NULL; at != NULL;) {
        int order = compare(key, at->key);
        if (order == 0) {
            return at;
        }
        at = order < 0 ? at->left : at->right;
    }
    return NULL;
}

LIBC_FN void *sysv_tsearch(const void *key, void **root, Compare compare)
{
    Node **link = (Node **)root;
    while (*link != NULL) {
        int order = compare(key, (*link)->key);
        if (order == 0) {
            return *link;
        }
        link = order < 0 ? &(*link)->left : &(*link)->right;
    }
    Node *node = (Node *)sysv_malloc(sizeof(Node));
    if (node != NULL) {
        *node = (Node){.key = key};
        *link = node;
    }
    return node;
}

LIBC_FN void *sysv_tdelete(const void *key, void **root, Compare compare)
{
    Node **link = (Node **)root;
    Node *parent = NULL;
    while (*link != NULL) {
        int order = compare(key, (*link)->key);
        if (order == 0) {
            break;
        }
        parent = *link;
        link = order < 0 ? &(*link)->left : &(*link)->right;
    }
    Node *gone = *link;
    if (gone == NULL) {
        return NULL;
    }
    if (gone->left == NULL || gone->right == NULL) {
        *link = gone->left != NULL ? gone->left : gone->right;
    } else {
        Node **successor_link = &gone->right;
        while ((*successor_link)->left != NULL) {
            successor_link = &(*successor_link)->left;
        }
        Node *successor = *successor_link;
        *successor_link = successor->right;
        successor->left = gone->left;
        successor->right = gone->right;
        *link = successor;
    }
    sysv_free(gone);
    return parent != NULL ? parent : (void *)*root;
}

static void walk(const Node *node, WalkAction action, int depth)
{
    if (node == NULL) {
        return;
    }
    if (node->left == NULL && node->right == NULL) {
        action(node, VISIT_LEAF, depth);
        return;
    }
    action(node, VISIT_PREORDER, depth);
    walk(node->left, action, depth + 1);
    action(node, VISIT_POSTORDER, depth);
    walk(node->right, action, depth + 1);
    action(node, VISIT_ENDORDER, depth);
}

LIBC_FN void sysv_twalk(const void *root, WalkAction action)
{
    walk((const Node *)root, action, 0);
}

LIBC_FN void sysv_tdestroy(void *root, void (SYSV *free_key)(void *))
{
    Node *node = (Node *)root;
    if (node == NULL) {
        return;
    }
    sysv_tdestroy(node->left, free_key);
    sysv_tdestroy(node->right, free_key);
    if (free_key != NULL) {
        free_key((void *)node->key);
    }
    sysv_free(node);
}

/* hsearch: one global table, open addressing with linear probing, keys compared as strings. */
typedef struct {
    char *key;
    void *data;
} Entry;

typedef struct {
    Entry *slots;
    size_t size;
    size_t used;
} Table;

static Table g_table;

static size_t hash_text(const char *text)
{
    size_t hash = 5381;
    while (*text != '\0') {
        hash = hash * 33 + (unsigned char)*text++;
    }
    return hash;
}

LIBC_FN int sysv_hcreate(size_t count)
{
    g_table.size = count * 2 + 1;
    g_table.used = 0;
    g_table.slots = (Entry *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, g_table.size * sizeof(Entry));
    return g_table.slots != NULL ? 1 : shim_fail(LINUX_ENOMEM) + 1;
}

LIBC_FN void sysv_hdestroy(void)
{
    HeapFree(GetProcessHeap(), 0, g_table.slots);
    g_table = (Table){0};
}

/* ACTION is FIND (0) or ENTER (1); ENTRY is {key, data}. */
LIBC_FN Entry *sysv_hsearch(Entry item, int action)
{
    if (g_table.slots == NULL) {
        shim_set_errno(LINUX_ESRCH);
        return NULL;
    }
    size_t at = hash_text(item.key) % g_table.size;
    for (size_t probed = 0; probed < g_table.size; probed++, at = (at + 1) % g_table.size) {
        Entry *slot = &g_table.slots[at];
        if (slot->key == NULL) {
            if (action == 0 || g_table.used + 1 >= g_table.size) {
                shim_set_errno(action == 0 ? LINUX_ESRCH : LINUX_ENOMEM);
                return NULL;
            }
            *slot = item;
            g_table.used++;
            return slot;
        }
        if (strcmp(slot->key, item.key) == 0) {
            return slot;
        }
    }
    shim_set_errno(LINUX_ENOMEM);
    return NULL;
}
