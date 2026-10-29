/*
 * File: tests/corpus/vulkan_window/presenter.c
 * Purpose: Create the instance, surface, device and swapchain, and draw and present one frame at a time
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <stdio.h>
#include <stdlib.h>

#include "presenter.h"

#define MAX_DEVICES 8
#define MAX_FAMILIES 16
#define MAX_FORMATS 32
#define QUEUE_PRIORITY 1.0f
#define COLOUR_STEPS 8

void presenter_check(VkResult result, const char *step)
{
    if (result != VK_SUCCESS) {
        fprintf(stderr, "vulkan: %s failed with result %d\n", step, (int)result);
        exit(1);
    }
}

static void create_instance(Presenter *presenter)
{
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
                                 .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
    presenter_check(vkCreateInstance(&info, NULL, &presenter->instance), "vkCreateInstance");
}

static void create_surface(Presenter *presenter, const Window *window)
{
    VkXcbSurfaceCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
                                      .connection = window->connection, .window = window->id};
    presenter_check(vkCreateXcbSurfaceKHR(presenter->instance, &info, NULL, &presenter->surface),
                    "vkCreateXcbSurfaceKHR");
}

static int family_can_present(const Presenter *presenter, VkPhysicalDevice device, uint32_t family)
{
    VkBool32 is_supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, family, presenter->surface, &is_supported);
    return is_supported == VK_TRUE;
}

static void pick_device(Presenter *presenter)
{
    VkPhysicalDevice devices[MAX_DEVICES];
    uint32_t count = MAX_DEVICES;
    VkResult result = vkEnumeratePhysicalDevices(presenter->instance, &count, devices);
    if (result != VK_INCOMPLETE) {
        presenter_check(result, "vkEnumeratePhysicalDevices");
    }
    for (uint32_t d = 0; d < count; d++) {
        VkQueueFamilyProperties families[MAX_FAMILIES];
        uint32_t family_count = MAX_FAMILIES;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &family_count, families);
        for (uint32_t f = 0; f < family_count; f++) {
            if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && family_can_present(presenter, devices[d], f)) {
                presenter->physical = devices[d];
                presenter->family = f;
                return;
            }
        }
    }
    fprintf(stderr, "vulkan: no device can present to the window\n");
    exit(1);
}

static void create_device(Presenter *presenter)
{
    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    float priority = QUEUE_PRIORITY;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                     .queueFamilyIndex = presenter->family, .queueCount = 1,
                                     .pQueuePriorities = &priority};
    VkDeviceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                               .pQueueCreateInfos = &queue, .enabledExtensionCount = 1,
                               .ppEnabledExtensionNames = extensions};
    presenter_check(vkCreateDevice(presenter->physical, &info, NULL, &presenter->device), "vkCreateDevice");
    vkGetDeviceQueue(presenter->device, presenter->family, 0, &presenter->queue);
}

static VkCompositeAlphaFlagBitsKHR pick_alpha(VkCompositeAlphaFlagsKHR supported)
{
    VkCompositeAlphaFlagBitsKHR order[] = {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                           VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR};
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (supported & order[i]) {
            return order[i];
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static void create_swapchain(Presenter *presenter)
{
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR formats[MAX_FORMATS];
    uint32_t format_count = MAX_FORMATS;
    presenter_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(presenter->physical, presenter->surface, &caps),
                    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(presenter->physical, presenter->surface, &format_count, formats);
    if (result != VK_INCOMPLETE) {
        presenter_check(result, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    }
    VkExtent2D extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent
                                                               : (VkExtent2D){WINDOW_WIDTH, WINDOW_HEIGHT};
    VkSwapchainCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                     .surface = presenter->surface, .minImageCount = caps.minImageCount,
                                     .imageFormat = formats[0].format, .imageColorSpace = formats[0].colorSpace,
                                     .imageExtent = extent, .imageArrayLayers = 1,
                                     .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                     .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                     .preTransform = caps.currentTransform,
                                     .compositeAlpha = pick_alpha(caps.supportedCompositeAlpha),
                                     .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE};
    presenter_check(vkCreateSwapchainKHR(presenter->device, &info, NULL, &presenter->swapchain), "vkCreateSwapchainKHR");
    presenter->image_count = MAX_SWAPCHAIN_IMAGES;
    presenter_check(vkGetSwapchainImagesKHR(presenter->device, presenter->swapchain, &presenter->image_count,
                                            presenter->images),
                    "vkGetSwapchainImagesKHR");
}

static void create_sync(Presenter *presenter)
{
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                    .queueFamilyIndex = presenter->family};
    VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    presenter_check(vkCreateCommandPool(presenter->device, &pool, NULL, &presenter->pool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                         .commandPool = presenter->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                         .commandBufferCount = 1};
    presenter_check(vkAllocateCommandBuffers(presenter->device, &alloc, &presenter->commands), "vkAllocateCommandBuffers");
    presenter_check(vkCreateSemaphore(presenter->device, &semaphore, NULL, &presenter->acquired), "vkCreateSemaphore");
    presenter_check(vkCreateSemaphore(presenter->device, &semaphore, NULL, &presenter->rendered), "vkCreateSemaphore");
}

void presenter_create(Presenter *presenter, const Window *window)
{
    create_instance(presenter);
    create_surface(presenter, window);
    pick_device(presenter);
    create_device(presenter);
    create_swapchain(presenter);
    create_sync(presenter);
}

static void transition(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                       VkAccessFlags source, VkAccessFlags destination, VkPipelineStageFlags first,
                       VkPipelineStageFlags second)
{
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = source,
                                    .dstAccessMask = destination, .oldLayout = from, .newLayout = to,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image,
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(commands, first, second, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static void record(const Presenter *presenter, VkImage image, unsigned frame)
{
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    float level = (float)(frame % COLOUR_STEPS) / (float)(COLOUR_STEPS - 1);
    VkClearColorValue colour = {.float32 = {level, 0.25f, 1.0f - level, 1.0f}};
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    presenter_check(vkBeginCommandBuffer(presenter->commands, &begin), "vkBeginCommandBuffer");
    transition(presenter->commands, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdClearColorImage(presenter->commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &range);
    transition(presenter->commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
               VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    presenter_check(vkEndCommandBuffer(presenter->commands), "vkEndCommandBuffer");
}

void presenter_draw_frame(Presenter *presenter, unsigned frame)
{
    uint32_t index;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    presenter_check(vkAcquireNextImageKHR(presenter->device, presenter->swapchain, UINT64_MAX, presenter->acquired,
                                          VK_NULL_HANDLE, &index),
                    "vkAcquireNextImageKHR");
    record(presenter, presenter->images[index], frame);
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1,
                           .pWaitSemaphores = &presenter->acquired, .pWaitDstStageMask = &wait_stage,
                           .commandBufferCount = 1, .pCommandBuffers = &presenter->commands,
                           .signalSemaphoreCount = 1, .pSignalSemaphores = &presenter->rendered};
    presenter_check(vkQueueSubmit(presenter->queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
                                .pWaitSemaphores = &presenter->rendered, .swapchainCount = 1,
                                .pSwapchains = &presenter->swapchain, .pImageIndices = &index};
    presenter_check(vkQueuePresentKHR(presenter->queue, &present), "vkQueuePresentKHR");
    presenter_check(vkQueueWaitIdle(presenter->queue), "vkQueueWaitIdle");
}

void presenter_destroy(Presenter *presenter)
{
    vkDeviceWaitIdle(presenter->device);
    vkDestroySemaphore(presenter->device, presenter->rendered, NULL);
    vkDestroySemaphore(presenter->device, presenter->acquired, NULL);
    vkDestroyCommandPool(presenter->device, presenter->pool, NULL);
    vkDestroySwapchainKHR(presenter->device, presenter->swapchain, NULL);
    vkDestroyDevice(presenter->device, NULL);
    vkDestroySurfaceKHR(presenter->instance, presenter->surface, NULL);
    vkDestroyInstance(presenter->instance, NULL);
}
