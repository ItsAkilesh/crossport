/*
 * File: shim/crossport_libc.c
 * Purpose: Startup of a translated program: load its libraries, bind imports, set up TLS, run constructors and main
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * crossport_start is called by the PE entry stub with the Microsoft x64
 * convention; everything translated code calls uses sysv_abi (see internal.h).
 */

#include <string.h>

#include "env_internal.h"
#include "args.h"
#include "libc_abi.h"
#include "startinfo.h"
#include "stdio_internal.h"
#include "threads.h"
#include "tls.h"


typedef int (SYSV *MainFn)(int, char **, char **);
typedef void (SYSV *InitFn)(int, char **, char **);
typedef void (SYSV *FiniFn)(void);

/* Dependencies first, the executable last: the order constructors run in. */
static LoadedModule g_modules[MAX_MODULES + 1];
static size_t g_module_count;
static uint8_t *g_executable_base;

static const StartInfo *validated(const StartInfo *info)
{
    if (info == NULL || info->magic != INFO_MAGIC || info->version != INFO_VERSION ||
        info->iat_binding_count > MAX_RECORDS || info->exe_binding_count > MAX_RECORDS ||
        info->copy_count > MAX_RECORDS || info->slot_fixup_count > MAX_RECORDS ||
        info->module_count > MAX_MODULES) {
        shim_die("crossport: bad start info\n");
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
            shim_die("crossport: cannot load a bundled library\n");
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
    const Binding *imported = info_iat_bindings(info);
    for (uint32_t i = 0; i < info->iat_binding_count; i++) {
        *(uint64_t *)(module->base + imported[i].slot_rva) =
            *(uint64_t *)(module->base + imported[i].source_rva) + (uint64_t)imported[i].addend;
    }
    const Binding *in_executable = info_exe_bindings(info);
    for (uint32_t i = 0; i < info->exe_binding_count; i++) {
        *(uint64_t *)(module->base + in_executable[i].slot_rva) =
            (uint64_t)(uintptr_t)(g_executable_base + in_executable[i].source_rva) +
            (uint64_t)in_executable[i].addend;
    }
}

/* Copy relocations run after every binding, like ld.so, so copied data is already relocated. */
static void perform_copies(const LoadedModule *module)
{
    const StartInfo *info = module->info;
    const CopyRecord *records = info_copies(info);
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

void shim_run_destructors(void)
{
    for (size_t i = g_module_count; i > 0; i--) {
        run_fini(&g_modules[i - 1]);
    }
}

NORETURN void shim_finish(int status)
{
    ExitProcess((UINT)status);
    __builtin_unreachable();
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
    shim_exit(main_fn(argc, argv, envp));
}

EXPORT void crossport_start(const StartInfo *info)
{
    validated(info);
    g_executable_base = (uint8_t *)GetModuleHandleW(NULL);
    load_modules(info);
    tls_init(g_modules, g_module_count);
    threads_init_main();
    fd_init();
    stdio_init();
    env_init();
    for (size_t i = 0; i < g_module_count; i++) {
        bind_module(&g_modules[i]);
    }
    perform_copies(&g_modules[g_module_count - 1]);

    size_t slot_count = 0;
    uint64_t *template_block = args_build_stack(&slot_count);
    void *entry = g_executable_base + info->entry_rva;
    /* Copy the block onto the thread's stack, then enter the ELF _start (rdx = 0: no rtld_fini). */
    __asm__ volatile("mov %%rcx, %%rax\n\t"
                     "shl $3, %%rax\n\t"
                     "sub %%rax, %%rsp\n\t"
                     "and $-16, %%rsp\n\t"
                     "mov %%rsp, %%rdi\n\t"
                     "cld\n\t"
                     "rep movsq\n\t"
                     "xor %%edx, %%edx\n\t"
                     "jmp *%2"
                     :
                     : "S"(template_block), "c"(slot_count), "b"(entry)
                     : "rax", "rdi", "memory");
    __builtin_unreachable();
}
