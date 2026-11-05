/*
 * File: shim/plugin_load.c
 * Purpose: Opening translated plugins at run time: find the DLL, load the libraries it needs first, then activate it
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * The translator writes each plugin the program may dlopen as a DLL of its own, named after the library with
 * ".dll" added, beside the executable (the bundle is flat, so the directory in a dlopen path is ignored).
 * The DLL's start-info table lists the plugin libraries it needs that are not loaded yet, dependencies first.
 */

#include <string.h>

#include "libc_abi.h"
#include "startinfo.h"

#define MAX_PLUGIN_NAME 256
#define DLL_SUFFIX ".dll"

/* Recursive: a plugin's constructor may open another plugin. */
static CRITICAL_SECTION g_lock;

void shim_plugins_init(void)
{
    InitializeCriticalSection(&g_lock);
}

static int is_name_character(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '+' || c == '-';
}

/* "dir/libfoo.so.1" becomes "libfoo.so.1.dll"; false for a name the translator would never have written. */
static int dll_name_for(const char *library_name, char *out, size_t size)
{
    const char *slash = strrchr(library_name, '/');
    const char *backslash = strrchr(library_name, '\\');
    const char *last = slash > backslash ? slash : backslash;
    const char *base = last != NULL ? last + 1 : library_name;
    size_t length = strlen(base);
    if (length == 0 || length + sizeof(DLL_SUFFIX) > size || base[0] == '.') {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        if (!is_name_character(base[i])) {
            return 0;
        }
    }
    memcpy(out, base, length);
    memcpy(out + length, DLL_SUFFIX, sizeof(DLL_SUFFIX));
    return 1;
}

/* Loads one DLL after the plugin libraries it lists, and activates it; NULL when anything is wrong with it. */
static const LoadedModule *open_dll(const char *dll_name, unsigned depth)
{
    HMODULE handle = depth <= MAX_MODULES ? LoadLibraryExA(dll_name, NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR) : NULL;
    if (handle == NULL) {
        return NULL;
    }
    const LoadedModule *existing = shim_find_module(handle);
    const StartInfo *info = (const StartInfo *)(void *)GetProcAddress(handle, INFO_SYMBOL);
    if (existing != NULL || !shim_info_is_valid(info)) {
        return existing;
    }
    const uint32_t *names = (const uint32_t *)((const uint8_t *)handle + info->modules_rva);
    for (uint32_t i = 0; i < info->module_count; i++) {
        if (open_dll((const char *)handle + names[i], depth + 1) == NULL) {
            return NULL;
        }
    }
    return shim_activate_module(info, handle);
}

void *shim_load_plugin(const char *library_name)
{
    char dll_name[MAX_PLUGIN_NAME];
    if (!dll_name_for(library_name, dll_name, sizeof(dll_name))) {
        return NULL;
    }
    EnterCriticalSection(&g_lock);
    const LoadedModule *module = open_dll(dll_name, 0);
    LeaveCriticalSection(&g_lock);
    return module != NULL ? (void *)module->base : NULL;
}
