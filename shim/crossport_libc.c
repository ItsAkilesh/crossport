/*
 * File: shim/crossport_libc.c
 * Purpose: Milestone 1 libc shim: starts a translated ELF program on Win32 and provides puts
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * Translated code keeps Linux (System V) conventions, so every function it
 * calls is exported with sysv_abi. crossport_start is the exception: the PE
 * entry stub calls it with the Microsoft x64 convention.
 */

#include <stdint.h>
#include <string.h>
#include <windows.h>

#define SYSV __attribute__((sysv_abi))
#define EXPORT __declspec(dllexport)

/* "CPRT" as a little-endian u32. Must match translator/src/stub.rs. */
#define START_INFO_MAGIC 0x54525043u
#define START_INFO_VERSION 1u
#define INITIAL_STACK_SLOTS 8
#define AT_NULL 0
#define PUTS_FAILURE (-1)
#define PUTS_SUCCESS 1
#define STARTUP_FAILURE_EXIT_CODE 127

typedef struct {
    uint32_t got_rva;
    uint32_t iat_rva;
} GotPair;

/* Header fields mirror translator/src/stub.rs; `pairs` follows in memory. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_rva;
    uint32_t init_rva;
    uint32_t init_array_rva;
    uint32_t init_array_count;
    uint32_t fini_rva;
    uint32_t fini_array_rva;
    uint32_t fini_array_count;
    uint32_t pair_count;
    GotPair pairs[];
} StartInfo;

typedef int (SYSV *MainFn)(int, char **, char **);
typedef void (SYSV *InitFn)(int, char **, char **);
typedef void (SYSV *FiniFn)(void);

typedef struct {
    uint64_t slots[INITIAL_STACK_SLOTS];
    char program_path[MAX_PATH];
} InitialStack;

static uint8_t *g_image_base;
static const StartInfo *g_info;

static void fail_startup(const char *message)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, (DWORD)strlen(message), &written, NULL);
    ExitProcess(STARTUP_FAILURE_EXIT_CODE);
}

/* The loader fills the IAT; the ELF code reads its GOT. Copy one into the other. */
static void bind_got(void)
{
    for (uint32_t i = 0; i < g_info->pair_count; i++) {
        const GotPair *pair = &g_info->pairs[i];
        *(uint64_t *)(g_image_base + pair->got_rva) = *(uint64_t *)(g_image_base + pair->iat_rva);
    }
}

static void run_fini(void)
{
    if (g_info->fini_array_rva != 0) {
        FiniFn *array = (FiniFn *)(g_image_base + g_info->fini_array_rva);
        for (uint32_t i = g_info->fini_array_count; i > 0; i--) {
            array[i - 1]();
        }
    }
    if (g_info->fini_rva != 0) {
        ((FiniFn)(g_image_base + g_info->fini_rva))();
    }
}

static void run_init(int argc, char **argv, char **envp)
{
    if (g_info->init_rva != 0) {
        ((InitFn)(g_image_base + g_info->init_rva))(argc, argv, envp);
    }
    if (g_info->init_array_rva != 0) {
        InitFn *array = (InitFn *)(g_image_base + g_info->init_array_rva);
        for (uint32_t i = 0; i < g_info->init_array_count; i++) {
            array[i](argc, argv, envp);
        }
    }
}

static void finish(int status)
{
    run_fini();
    ExitProcess((UINT)status);
}

static BOOL write_all(HANDLE handle, const char *data, size_t length)
{
    while (length > 0) {
        DWORD written = 0;
        if (!WriteFile(handle, data, (DWORD)length, &written, NULL) || written == 0) {
            return FALSE;
        }
        data += written;
        length -= written;
    }
    return TRUE;
}

EXPORT SYSV int puts(const char *text)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!write_all(out, text, strlen(text)) || !write_all(out, "\n", 1)) {
        return PUTS_FAILURE;
    }
    return PUTS_SUCCESS;
}

EXPORT SYSV int __libc_start_main(MainFn main_fn, int argc, char **argv, void (*init)(void),
                                  void (*fini)(void), void (*rtld_fini)(void), void *stack_end)
{
    (void)init;
    (void)fini;
    (void)rtld_fini;
    (void)stack_end;
    char **envp = argv + argc + 1;
    run_init(argc, argv, envp);
    finish(main_fn(argc, argv, envp));
    return 0;
}

/* Build the stack the kernel would hand to _start: argc, argv, envp, auxv. */
static void fill_initial_stack(InitialStack *stack)
{
    memset(stack, 0, sizeof(*stack));
    GetModuleFileNameA(NULL, stack->program_path, MAX_PATH);
    stack->slots[0] = 1;
    stack->slots[1] = (uint64_t)(uintptr_t)stack->program_path;
    stack->slots[4] = AT_NULL;
}

EXPORT void crossport_start(const StartInfo *info)
{
    if (info->magic != START_INFO_MAGIC || info->version != START_INFO_VERSION) {
        fail_startup("crossport: bad start info\n");
    }
    g_info = info;
    g_image_base = (uint8_t *)GetModuleHandleW(NULL);
    bind_got();

    _Alignas(16) InitialStack stack;
    fill_initial_stack(&stack);
    void *entry = g_image_base + info->entry_rva;
    /* Switch to the initial stack block and enter the ELF _start (rdx = 0: no rtld_fini). */
    __asm__ volatile("mov %0, %%rsp\n\t"
                     "xor %%edx, %%edx\n\t"
                     "jmp *%1"
                     :
                     : "b"(&stack), "c"(entry)
                     : "memory");
    __builtin_unreachable();
}
