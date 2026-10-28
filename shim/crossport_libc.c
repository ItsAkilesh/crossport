/*
 * File: shim/crossport_libc.c
 * Purpose: libc shim: starts a translated ELF program and its libraries on Win32 and provides puts
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * Translated code keeps Linux (System V) conventions, so every function it
 * calls is exported with sysv_abi. crossport_start is the exception: the PE
 * entry stub calls it with the Microsoft x64 convention.
 *
 * The start-info layout is a contract with translator/src/shim_abi.rs and
 * translator/src/startup.rs; change them together.
 */

#include <stdint.h>
#include <string.h>
#include <windows.h>

#define SYSV __attribute__((sysv_abi))
#define EXPORT __declspec(dllexport)

#define INFO_MAGIC 0x54525043u /* "CPRT", little-endian */
#define INFO_VERSION 2u
#define INFO_SYMBOL "__crossport_info"
#define MAX_MODULES 256u
#define MAX_RECORDS (1u << 22)
#define INITIAL_STACK_SLOTS 8
#define AT_NULL 0
#define PUTS_FAILURE (-1)
#define PUTS_SUCCESS 1
#define STARTUP_FAILURE_EXIT_CODE 127

/* *slot = *source + addend. `source` is an IAT entry, or an RVA in the executable. */
typedef struct {
    uint32_t slot_rva;
    uint32_t source_rva;
    int64_t addend;
} Binding;

/* memcpy(executable + dest_rva, *iat_rva, size): an R_X86_64_COPY performed at startup. */
typedef struct {
    uint32_t dest_rva;
    uint32_t iat_rva;
    uint64_t size;
} CopyRecord;

/* Fixed 56-byte header; the records follow it in memory. */
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
    uint32_t iat_binding_count;
    uint32_t exe_binding_count;
    uint32_t copy_count;
    uint32_t module_count;
    uint32_t modules_rva;
} StartInfo;

typedef int (SYSV *MainFn)(int, char **, char **);
typedef void (SYSV *InitFn)(int, char **, char **);
typedef void (SYSV *FiniFn)(void);

typedef struct {
    const StartInfo *info;
    uint8_t *base;
} LoadedModule;

typedef struct {
    uint64_t slots[INITIAL_STACK_SLOTS];
    char program_path[MAX_PATH];
} InitialStack;

/* Dependencies first, the executable last: the order constructors run in. */
static LoadedModule g_modules[MAX_MODULES + 1];
static size_t g_module_count;
static uint8_t *g_executable_base;

static const Binding *iat_bindings(const StartInfo *info)
{
    return (const Binding *)(info + 1);
}

static const Binding *exe_bindings(const StartInfo *info)
{
    return iat_bindings(info) + info->iat_binding_count;
}

static const CopyRecord *copy_records(const StartInfo *info)
{
    return (const CopyRecord *)(exe_bindings(info) + info->exe_binding_count);
}

static void fail_startup(const char *message)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, (DWORD)strlen(message), &written, NULL);
    ExitProcess(STARTUP_FAILURE_EXIT_CODE);
}

static const StartInfo *validated(const StartInfo *info)
{
    if (info == NULL || info->magic != INFO_MAGIC || info->version != INFO_VERSION ||
        info->iat_binding_count > MAX_RECORDS || info->exe_binding_count > MAX_RECORDS ||
        info->copy_count > MAX_RECORDS || info->module_count > MAX_MODULES) {
        fail_startup("crossport: bad start info\n");
    }
    return info;
}

/* Finds every dependency DLL the executable lists and records its start info. */
static void load_modules(const StartInfo *executable)
{
    const uint32_t *names = (const uint32_t *)(g_executable_base + executable->modules_rva);
    for (uint32_t i = 0; i < executable->module_count; i++) {
        const char *name = (const char *)(g_executable_base + names[i]);
        HMODULE handle = LoadLibraryExA(name, NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
        if (handle == NULL) {
            fail_startup("crossport: cannot load a bundled library\n");
        }
        const StartInfo *info = validated((const StartInfo *)(void *)GetProcAddress(handle, INFO_SYMBOL));
        g_modules[g_module_count].info = info;
        g_modules[g_module_count].base = (uint8_t *)handle;
        g_module_count++;
    }
    g_modules[g_module_count].info = executable;
    g_modules[g_module_count].base = g_executable_base;
    g_module_count++;
}

/* The loader fills each IAT; the ELF code reads its GOT. Copy one into the other. */
static void bind_module(const LoadedModule *module)
{
    const StartInfo *info = module->info;
    const Binding *imported = iat_bindings(info);
    for (uint32_t i = 0; i < info->iat_binding_count; i++) {
        *(uint64_t *)(module->base + imported[i].slot_rva) =
            *(uint64_t *)(module->base + imported[i].source_rva) + (uint64_t)imported[i].addend;
    }
    const Binding *in_executable = exe_bindings(info);
    for (uint32_t i = 0; i < info->exe_binding_count; i++) {
        *(uint64_t *)(module->base + in_executable[i].slot_rva) =
            (uint64_t)(uintptr_t)(g_executable_base + in_executable[i].source_rva) + (uint64_t)in_executable[i].addend;
    }
}

/* Copy relocations run after every binding, like ld.so, so copied data is already relocated. */
static void perform_copies(const LoadedModule *module)
{
    const StartInfo *info = module->info;
    const CopyRecord *records = copy_records(info);
    for (uint32_t i = 0; i < info->copy_count; i++) {
        const void *source = (const void *)(uintptr_t) * (uint64_t *)(module->base + records[i].iat_rva);
        CopyMemory(module->base + records[i].dest_rva, source, (SIZE_T)records[i].size);
    }
}

static void run_init(const LoadedModule *module, int argc, char **argv, char **envp)
{
    const StartInfo *info = module->info;
    if (info->init_rva != 0) {
        ((InitFn)(module->base + info->init_rva))(argc, argv, envp);
    }
    if (info->init_array_rva != 0) {
        InitFn *array = (InitFn *)(module->base + info->init_array_rva);
        for (uint32_t i = 0; i < info->init_array_count; i++) {
            array[i](argc, argv, envp);
        }
    }
}

static void run_fini(const LoadedModule *module)
{
    const StartInfo *info = module->info;
    if (info->fini_array_rva != 0) {
        FiniFn *array = (FiniFn *)(module->base + info->fini_array_rva);
        for (uint32_t i = info->fini_array_count; i > 0; i--) {
            array[i - 1]();
        }
    }
    if (info->fini_rva != 0) {
        ((FiniFn)(module->base + info->fini_rva))();
    }
}

static void finish(int status)
{
    for (size_t i = g_module_count; i > 0; i--) {
        run_fini(&g_modules[i - 1]);
    }
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
    for (size_t i = 0; i < g_module_count; i++) {
        run_init(&g_modules[i], argc, argv, envp);
    }
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
    validated(info);
    g_executable_base = (uint8_t *)GetModuleHandleW(NULL);
    load_modules(info);
    for (size_t i = 0; i < g_module_count; i++) {
        bind_module(&g_modules[i]);
    }
    perform_copies(&g_modules[g_module_count - 1]);

    _Alignas(16) InitialStack stack;
    fill_initial_stack(&stack);
    void *entry = g_executable_base + info->entry_rva;
    /* Switch to the initial stack block and enter the ELF _start (rdx = 0: no rtld_fini). */
    __asm__ volatile("mov %0, %%rsp\n\t"
                     "xor %%edx, %%edx\n\t"
                     "jmp *%1"
                     :
                     : "b"(&stack), "c"(entry)
                     : "memory");
    __builtin_unreachable();
}
