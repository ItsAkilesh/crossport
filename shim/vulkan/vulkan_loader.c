/*
 * File: shim/vulkan/vulkan_loader.c
 * Purpose: Find the Windows Vulkan loader, resolve real entry points for the thunks, and implement instance and GetProcAddr calls
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * vulkan-1.dll is loaded on first use, so programs that never touch Vulkan do not need it. Entry points
 * the loader exports are bound directly; the rest (extension commands) come from vkGetInstanceProcAddr
 * with the first instance the program created. Vulkan's data types are identical on both sides, so only
 * the calling convention differs, and the generated thunks bridge that.
 */

#include <stdlib.h>
#include <string.h>

#include "vulkan_loader.h"

#define VULKAN_LOADER_NAME "vulkan-1.dll"
#define VK_ERROR_INITIALIZATION_FAILED (-3)
#define MAX_COMMANDS 2048

typedef void *(*GetInstanceProcAddrFn)(void *instance, const char *name);
typedef void *(*GetDeviceProcAddrFn)(void *device, const char *name);
typedef int32_t (*CreateInstanceFn)(const void *info, const void *allocator, void **instance);
typedef void (*DestroyInstanceFn)(void *instance, const void *allocator);

static INIT_ONCE g_load_once = INIT_ONCE_STATIC_INIT;
static HMODULE g_loader;
static void *volatile g_targets[MAX_COMMANDS];
static void *volatile g_instance_hint;

static BOOL CALLBACK load_loader(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    g_loader = LoadLibraryA(VULKAN_LOADER_NAME);
    return TRUE;
}

static HMODULE loader_module(void)
{
    InitOnceExecuteOnce(&g_load_once, load_loader, NULL, NULL);
    return g_loader;
}

static void *loader_export(const char *name)
{
    HMODULE module = loader_module();
    return module != NULL ? (void *)GetProcAddress(module, name) : NULL;
}

/* Keeps the first pointer stored for a command; later ones are equivalent. */
static void remember(size_t index, void *target)
{
    InterlockedCompareExchangePointer((PVOID volatile *)&g_targets[index], target, NULL);
}

static void *instance_lookup(void *instance, const char *name)
{
    GetInstanceProcAddrFn lookup = (GetInstanceProcAddrFn)loader_export("vkGetInstanceProcAddr");
    return (lookup != NULL && instance != NULL) ? lookup(instance, name) : NULL;
}

void *vulkan_target(size_t index)
{
    void *target = g_targets[index];
    if (target == NULL) {
        target = loader_export(g_vulkan_command_names[index]);
        if (target == NULL) {
            target = instance_lookup(g_instance_hint, g_vulkan_command_names[index]);
        }
        if (target != NULL) {
            remember(index, target);
        }
    }
    return target;
}

static int compare_name(const void *key, const void *entry)
{
    return strcmp((const char *)key, *(const char *const *)entry);
}

static ptrdiff_t command_index(const char *name)
{
    const char *const *found = (const char *const *)bsearch(name, g_vulkan_command_names, g_vulkan_command_count,
                                                             sizeof(g_vulkan_command_names[0]), compare_name);
    return found != NULL ? found - g_vulkan_command_names : -1;
}

LIBC_FN int32_t sysv_vkCreateInstance(const void *info, const void *allocator, void **instance)
{
    CreateInstanceFn create = (CreateInstanceFn)loader_export("vkCreateInstance");
    if (create == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    int32_t result = create(info, allocator, instance);
    if (result == 0 && instance != NULL) {
        InterlockedCompareExchangePointer((PVOID volatile *)&g_instance_hint, *instance, NULL);
    }
    return result;
}

LIBC_FN void sysv_vkDestroyInstance(void *instance, const void *allocator)
{
    DestroyInstanceFn destroy = (DestroyInstanceFn)loader_export("vkDestroyInstance");
    if (destroy == NULL) {
        return;
    }
    InterlockedCompareExchangePointer((PVOID volatile *)&g_instance_hint, NULL, instance);
    destroy(instance, allocator);
}

/* The hand-written entry points, returned by GetProcAddr under their real names. */
static void *handwritten_entry(const char *name)
{
    if (strcmp(name, "vkCreateInstance") == 0) {
        return (void *)sysv_vkCreateInstance;
    }
    if (strcmp(name, "vkDestroyInstance") == 0) {
        return (void *)sysv_vkDestroyInstance;
    }
    if (strcmp(name, "vkGetInstanceProcAddr") == 0) {
        return (void *)sysv_vkGetInstanceProcAddr;
    }
    return strcmp(name, "vkGetDeviceProcAddr") == 0 ? (void *)sysv_vkGetDeviceProcAddr : NULL;
}

/* The thunk for `name` when the real loader or driver also provides it for `instance`, else NULL. */
LIBC_FN void *sysv_vkGetInstanceProcAddr(void *instance, const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    void *own = handwritten_entry(name);
    if (own != NULL) {
        return own;
    }
    ptrdiff_t index = command_index(name);
    if (index < 0) {
        return NULL;
    }
    void *real = instance_lookup(instance, name);
    if (real == NULL) {
        real = loader_export(name);
    }
    if (real == NULL) {
        return NULL;
    }
    remember((size_t)index, real);
    return (void *)g_vulkan_thunks[index];
}

LIBC_FN void *sysv_vkGetDeviceProcAddr(void *device, const char *name)
{
    GetDeviceProcAddrFn lookup = (GetDeviceProcAddrFn)loader_export("vkGetDeviceProcAddr");
    if (name == NULL || lookup == NULL) {
        return NULL;
    }
    void *own = handwritten_entry(name);
    ptrdiff_t index = command_index(name);
    void *real = device != NULL ? lookup(device, name) : NULL;
    if (real == NULL) {
        return NULL;
    }
    if (own != NULL || index < 0) {
        return own;
    }
    remember((size_t)index, real);
    return (void *)g_vulkan_thunks[index];
}
