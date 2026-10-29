/*
 * File: shim/args.c
 * Purpose: Builds the initial stack block a Linux process starts with: argc, argv, envp and the auxiliary vector
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * The command line is split the way Windows programs expect (CommandLineToArgvW) and converted to UTF-8.
 * The strings live on the heap for the rest of the process; the block itself is a template for the stack.
 */

#include "args.h"
#include "libc_abi.h"

#include <shellapi.h>

#define AUXV_END_SLOTS 2
#define BLOCK_FIXED_SLOTS 3 /* argc, the argv terminator and the envp terminator */

extern char **crossport_data_environ;

static char *utf8_copy(const wchar_t *wide)
{
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    char *text = needed > 0 ? (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)needed) : NULL;
    if (text == NULL || WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, needed, NULL, NULL) <= 0) {
        shim_die("crossport: cannot convert the command line\n");
    }
    return text;
}

static size_t environment_count(void)
{
    size_t count = 0;
    while (crossport_data_environ != NULL && crossport_data_environ[count] != NULL) {
        count++;
    }
    return count;
}

uint64_t *args_build_stack(size_t *slot_count)
{
    int argc = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (wide == NULL || argc < 1) {
        shim_die("crossport: cannot read the command line\n");
    }
    size_t env_count = environment_count();
    size_t slots = BLOCK_FIXED_SLOTS + (size_t)argc + env_count + AUXV_END_SLOTS;
    uint64_t *block = (uint64_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, slots * sizeof(uint64_t));
    if (block == NULL) {
        shim_die("crossport: cannot allocate the initial stack\n");
    }
    block[0] = (uint64_t)argc;
    for (int i = 0; i < argc; i++) {
        block[1 + i] = (uint64_t)(uintptr_t)utf8_copy(wide[i]);
    }
    for (size_t i = 0; i < env_count; i++) {
        block[2 + (size_t)argc + i] = (uint64_t)(uintptr_t)crossport_data_environ[i];
    }
    LocalFree(wide);
    *slot_count = slots;
    return block;
}
