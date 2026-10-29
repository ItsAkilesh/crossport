/*
 * File: shim/vulkan/vulkan_wsi.c
 * Purpose: Present Vulkan surfaces for xcb windows through the Windows Vulkan loader's Win32 surfaces
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * A translated program asks for VK_KHR_xcb_surface and gives vkCreateXcbSurfaceKHR an xcb window id.
 * The shim reports the Win32 surface extension under the xcb name, and builds the Win32 surface for the
 * window that the xcb shim created.
 */

#include <string.h>

#include "vulkan_wsi.h"
#include "xcb_internal.h"

#define XCB_SURFACE_EXTENSION "VK_KHR_xcb_surface"
#define WIN32_SURFACE_EXTENSION "VK_KHR_win32_surface"
#define VK_ERROR_EXTENSION_NOT_PRESENT (-7)
#define VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR 1000009000
#define VK_MAX_EXTENSION_NAME_SIZE 256

/* VkInstanceCreateInfo and VkExtensionProperties, as laid out on both sides. */
typedef struct {
    int32_t type;
    const void *next;
    uint32_t flags;
    const void *application;
    uint32_t layer_count;
    const char *const *layer_names;
    uint32_t extension_count;
    const char *const *extension_names;
} InstanceInfo;

typedef struct {
    char name[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t spec_version;
} ExtensionProperties;

typedef struct {
    int32_t type;
    const void *next;
    uint32_t flags;
    void *connection;
    uint32_t window;
} XcbSurfaceInfo;

typedef struct {
    int32_t type;
    const void *next;
    uint32_t flags;
    HINSTANCE instance;
    HWND window;
} Win32SurfaceInfo;

typedef int32_t (*EnumerateExtensionsFn)(const char *layer, uint32_t *count, void *properties);
typedef int32_t (*CreateWin32SurfaceFn)(void *instance, const void *info, const void *allocator, uint64_t *surface);
typedef uint32_t (*Win32PresentationFn)(void *physical, uint32_t queue_family);

_Static_assert(sizeof(InstanceInfo) <= sizeof(((VulkanInstanceStorage *)0)->info), "instance info storage is too small");

const void *vulkan_wsi_instance_info(const void *info, VulkanInstanceStorage *storage)
{
    const InstanceInfo *original = (const InstanceInfo *)info;
    if (original == NULL || original->extension_count > VULKAN_MAX_EXTENSIONS) {
        return info;
    }
    int is_changed = 0;
    for (uint32_t i = 0; i < original->extension_count; i++) {
        is_changed |= strcmp(original->extension_names[i], XCB_SURFACE_EXTENSION) == 0;
        storage->names[i] = strcmp(original->extension_names[i], XCB_SURFACE_EXTENSION) == 0
                                ? WIN32_SURFACE_EXTENSION
                                : original->extension_names[i];
    }
    if (!is_changed) {
        return info;
    }
    InstanceInfo *copy = (InstanceInfo *)storage->info;
    *copy = *original;
    copy->extension_names = storage->names;
    return copy;
}

LIBC_FN int32_t sysv_vkEnumerateInstanceExtensionProperties(const char *layer, uint32_t *count, void *properties)
{
    EnumerateExtensionsFn real = (EnumerateExtensionsFn)vulkan_loader_export("vkEnumerateInstanceExtensionProperties");
    if (real == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    int32_t result = real(layer, count, properties);
    ExtensionProperties *list = (ExtensionProperties *)properties;
    for (uint32_t i = 0; properties != NULL && count != NULL && i < *count; i++) {
        if (strcmp(list[i].name, WIN32_SURFACE_EXTENSION) == 0) {
            strcpy(list[i].name, XCB_SURFACE_EXTENSION);
        }
    }
    return result;
}

LIBC_FN int32_t sysv_vkCreateXcbSurfaceKHR(void *instance, const void *info, const void *allocator, uint64_t *surface)
{
    CreateWin32SurfaceFn create = (CreateWin32SurfaceFn)vulkan_instance_function(instance, "vkCreateWin32SurfaceKHR");
    const XcbSurfaceInfo *xcb = (const XcbSurfaceInfo *)info;
    if (create == NULL) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    Win32SurfaceInfo win32 = {.type = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                              .instance = GetModuleHandleW(NULL), .window = xcb_window_handle(xcb->window)};
    return create(instance, &win32, allocator, surface);
}

LIBC_FN uint32_t sysv_vkGetPhysicalDeviceXcbPresentationSupportKHR(void *physical, uint32_t queue_family,
                                                                   void *connection, uint32_t visual)
{
    (void)connection;
    (void)visual;
    Win32PresentationFn support = (Win32PresentationFn)vulkan_loader_export("vkGetPhysicalDeviceWin32PresentationSupportKHR");
    return support != NULL ? support(physical, queue_family) : 0;
}

void *vulkan_wsi_entry(const char *name)
{
    if (strcmp(name, "vkCreateXcbSurfaceKHR") == 0) {
        return (void *)sysv_vkCreateXcbSurfaceKHR;
    }
    if (strcmp(name, "vkGetPhysicalDeviceXcbPresentationSupportKHR") == 0) {
        return (void *)sysv_vkGetPhysicalDeviceXcbPresentationSupportKHR;
    }
    return strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0 ? (void *)sysv_vkEnumerateInstanceExtensionProperties
                                                                       : NULL;
}
