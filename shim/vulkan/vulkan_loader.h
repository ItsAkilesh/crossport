/*
 * File: shim/vulkan/vulkan_loader.h
 * Purpose: Declarations shared by the generated Vulkan thunks and the hand-written loader entry points
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef CROSSPORT_VULKAN_LOADER_H
#define CROSSPORT_VULKAN_LOADER_H

#include "libc_abi.h"

/* Sorted by name; the thunk at the same index in g_vulkan_thunks calls the command of that name. */
extern const size_t g_vulkan_command_count;
extern const char *const g_vulkan_command_names[];
extern const void *const g_vulkan_thunks[];

/* The real (Microsoft convention) entry point behind a generated thunk, or NULL when no loader or driver provides it. */
void *vulkan_target(size_t index);

/* Hand-written entry points; everything else in the Vulkan API is generated. */
LIBC_FN int32_t sysv_vkCreateInstance(const void *info, const void *allocator, void **instance);
LIBC_FN void sysv_vkDestroyInstance(void *instance, const void *allocator);
LIBC_FN void *sysv_vkGetInstanceProcAddr(void *instance, const char *name);
LIBC_FN void *sysv_vkGetDeviceProcAddr(void *device, const char *name);

#endif
