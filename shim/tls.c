/*
 * File: shim/tls.c
 * Purpose: Per-thread TLS blocks, the thread pointer in a Windows TLS slot, stack canaries and __tls_get_addr
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * Windows resets a user-set FS base at the next context switch, so translated
 * code never uses %fs. The translator rewrites those accesses to read the thread
 * pointer from a Windows TLS slot (gs:[0x1480 + 8 * slot]). All modules' TLS
 * blocks live below that pointer in one allocation, exactly like Linux's static
 * TLS, so every offset is a translation-time constant.
 */

#include "internal.h"
#include "tls.h"

#include <bcrypt.h>
#include <intrin.h>
#include <string.h>

#define TEB_TLS_SLOTS_OFFSET 0x1480u
#define MAX_WINDOWS_TLS_SLOTS 64u
/* Thread control block above the thread pointer; glibc-compatible landmarks. */
#define TCB_BYTES 0x100u
#define TCB_SELF_OFFSET 0x00u
#define TCB_PTHREAD_SELF_OFFSET 0x10u
#define TCB_CANARY_OFFSET 0x28u
#define TCB_RAW_BLOCK_OFFSET 0x80u /* private: where the unaligned allocation is remembered */
#define MIN_THREAD_POINTER_ALIGN 64u
#define STACK_SMASH_EXIT_CODE 134 /* 128 + SIGABRT, as a shell would report */

typedef struct {
    int64_t module; /* the module's block offset from the thread pointer (see relink/patch.rs) */
    uint64_t offset;
} TlsIndex;

static DWORD g_slot = TLS_OUT_OF_INDEXES;
static size_t g_total_size;
static size_t g_alignment = MIN_THREAD_POINTER_ALIGN;
static const LoadedModule *g_modules;
static size_t g_module_count;
static uint64_t g_canary;

static void generate_canary(void)
{
    if (BCryptGenRandom(NULL, (PUCHAR)&g_canary, sizeof(g_canary), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        shim_die("crossport: cannot gather randomness for the stack canary\n");
    }
    g_canary &= ~0xFFull; /* a zero low byte stops string overflows from copying the canary */
}

/* Writes the TLS slot displacement into every stub and seals the stub section. */
static void patch_module(const LoadedModule *module)
{
    const StartInfo *info = module->info;
    if (info->canary_cell_rva != 0) {
        *(uint64_t *)(module->base + info->canary_cell_rva) = g_canary;
    }
    const uint32_t *fixups = info_slot_fixups(info);
    for (uint32_t i = 0; i < info->slot_fixup_count; i++) {
        *(uint32_t *)(module->base + fixups[i]) = TEB_TLS_SLOTS_OFFSET + 8u * g_slot;
    }
    if (info->stubs_size != 0) {
        DWORD previous;
        uint8_t *stubs = module->base + info->stubs_rva;
        if (!VirtualProtect(stubs, info->stubs_size, PAGE_EXECUTE_READ, &previous)) {
            shim_die("crossport: cannot make the thread-local stubs executable\n");
        }
        FlushInstructionCache(GetCurrentProcess(), stubs, info->stubs_size);
    }
}

void tls_init(const LoadedModule *modules, size_t count)
{
    const StartInfo *executable = modules[count - 1].info;
    g_modules = modules;
    g_module_count = count;
    g_total_size = executable->tls_total_size;
    if (executable->tls_max_align > g_alignment) {
        g_alignment = executable->tls_max_align;
    }
    g_slot = TlsAlloc();
    if (g_slot == TLS_OUT_OF_INDEXES || g_slot >= MAX_WINDOWS_TLS_SLOTS) {
        shim_die("crossport: no free inline Windows TLS slot (the process uses 64 already)\n");
    }
    generate_canary();
    for (size_t i = 0; i < count; i++) {
        patch_module(&modules[i]);
    }
    if (tls_attach_thread() == NULL) {
        shim_die("crossport: cannot allocate the main thread's TLS block\n");
    }
}

uint8_t *tls_thread_pointer(void)
{
    return (uint8_t *)__readgsqword(TEB_TLS_SLOTS_OFFSET + 8u * g_slot);
}

/* Copies each module's initial-data template to its place below the thread pointer. */
static void initialise_templates(uint8_t *thread_pointer)
{
    for (size_t i = 0; i < g_module_count; i++) {
        const StartInfo *info = g_modules[i].info;
        if (info->tls_mem_size != 0 && info->tls_file_size != 0) {
            memcpy(thread_pointer + info->tls_offset, g_modules[i].base + info->tls_init_rva, info->tls_file_size);
        }
    }
}

uint8_t *tls_attach_thread(void)
{
    size_t size = g_total_size + TCB_BYTES + g_alignment;
    uint8_t *raw = (uint8_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (raw == NULL) {
        return NULL;
    }
    uintptr_t aligned = ((uintptr_t)raw + g_alignment - 1) & ~((uintptr_t)g_alignment - 1);
    uint8_t *thread_pointer = (uint8_t *)aligned + g_total_size;
    *(uint8_t **)(thread_pointer + TCB_SELF_OFFSET) = thread_pointer;
    *(uint8_t **)(thread_pointer + TCB_PTHREAD_SELF_OFFSET) = thread_pointer;
    *(uint64_t *)(thread_pointer + TCB_CANARY_OFFSET) = g_canary;
    *(uint8_t **)(thread_pointer + TCB_RAW_BLOCK_OFFSET) = raw;
    initialise_templates(thread_pointer);
    if (!TlsSetValue(g_slot, thread_pointer)) {
        HeapFree(GetProcessHeap(), 0, raw);
        return NULL;
    }
    return thread_pointer;
}

void tls_detach_thread(void)
{
    uint8_t *thread_pointer = tls_thread_pointer();
    if (thread_pointer == NULL) {
        return;
    }
    uint8_t *raw = *(uint8_t **)(thread_pointer + TCB_RAW_BLOCK_OFFSET);
    TlsSetValue(g_slot, NULL);
    HeapFree(GetProcessHeap(), 0, raw);
}

/* General-dynamic and local-dynamic TLS: all blocks are static, so this is one addition. */
EXPORT SYSV void *__tls_get_addr(const TlsIndex *index)
{
    uint8_t *thread_pointer = tls_thread_pointer();
    if (thread_pointer == NULL) {
        shim_die("crossport: thread-local access on a thread without a TLS block\n");
    }
    return thread_pointer + index->module + index->offset;
}

EXPORT SYSV NORETURN void __stack_chk_fail(void)
{
    static const char message[] = "*** stack smashing detected ***: terminated\n";
    shim_write_all(GetStdHandle(STD_ERROR_HANDLE), message, sizeof(message) - 1);
    ExitProcess(STACK_SMASH_EXIT_CODE);
    __builtin_unreachable();
}
