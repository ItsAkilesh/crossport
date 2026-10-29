/*
 * File: tests/corpus/vulkan_window/presenter.h
 * Purpose: Vulkan instance, device and swapchain for presenting solid-colour frames into an xcb window
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef VULKAN_WINDOW_PRESENTER_H
#define VULKAN_WINDOW_PRESENTER_H

#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>

#include "window.h"

#define MAX_SWAPCHAIN_IMAGES 8

typedef struct {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    uint32_t family;
    VkDevice device;
    VkQueue queue;
    VkSwapchainKHR swapchain;
    uint32_t image_count;
    VkImage images[MAX_SWAPCHAIN_IMAGES];
    VkCommandPool pool;
    VkCommandBuffer commands;
    VkSemaphore acquired;
    VkSemaphore rendered;
} Presenter;

/* Aborts the program with a message naming the failed step. */
void presenter_check(VkResult result, const char *step);

void presenter_create(Presenter *presenter, const Window *window);
void presenter_destroy(Presenter *presenter);

/* Clears the next swapchain image to a colour that depends on `frame` and presents it. */
void presenter_draw_frame(Presenter *presenter, unsigned frame);

#endif
