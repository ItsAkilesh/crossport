/*
 * File: shim/vulkan/vulkan_wsi.h
 * Purpose: The window-system part of the Vulkan shim: xcb surfaces on top of Win32 surfaces
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef CROSSPORT_VULKAN_WSI_H
#define CROSSPORT_VULKAN_WSI_H

#include "vulkan_loader.h"

#define VULKAN_MAX_EXTENSIONS 64

/* Space for a rewritten copy of a VkInstanceCreateInfo and its extension name list. */
typedef struct {
    uint8_t info[64];
    const char *names[VULKAN_MAX_EXTENSIONS];
} VulkanInstanceStorage;

/*
 * The create info to hand the real loader: the program's own, with VK_KHR_xcb_surface replaced by
 * VK_KHR_win32_surface (the extension that actually exists on Windows). Returns `info` itself when
 * nothing needs to change.
 */
const void *vulkan_wsi_instance_info(const void *info, VulkanInstanceStorage *storage);

/* The hand-written WSI entry point for `name` (vkCreateXcbSurfaceKHR and friends), or NULL. */
void *vulkan_wsi_entry(const char *name);

LIBC_FN int32_t sysv_vkEnumerateInstanceExtensionProperties(const char *layer, uint32_t *count, void *properties);
LIBC_FN int32_t sysv_vkCreateXcbSurfaceKHR(void *instance, const void *info, const void *allocator, uint64_t *surface);
LIBC_FN uint32_t sysv_vkGetPhysicalDeviceXcbPresentationSupportKHR(void *physical, uint32_t queue_family,
                                                                   void *connection, uint32_t visual);

#endif
