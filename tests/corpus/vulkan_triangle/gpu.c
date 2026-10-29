/*
 * File: tests/corpus/vulkan_triangle/gpu.c
 * Purpose: Create a Vulkan instance, pick the first device with a graphics queue and make a logical device
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <stdio.h>
#include <stdlib.h>

#include "gpu.h"

#define MAX_DEVICES 8
#define MAX_QUEUE_FAMILIES 16
#define QUEUE_PRIORITY 1.0f

void gpu_check(VkResult result, const char *step)
{
    if (result != VK_SUCCESS) {
        fprintf(stderr, "vulkan: %s failed with result %d\n", step, (int)result);
        exit(1);
    }
}

static void create_instance(Gpu *gpu)
{
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    gpu_check(vkCreateInstance(&info, NULL, &gpu->instance), "vkCreateInstance");
}

static int find_graphics_family(VkPhysicalDevice device, uint32_t *family)
{
    VkQueueFamilyProperties families[MAX_QUEUE_FAMILIES];
    uint32_t count = MAX_QUEUE_FAMILIES;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families);
    for (uint32_t i = 0; i < count; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *family = i;
            return 1;
        }
    }
    return 0;
}

static void pick_device(Gpu *gpu)
{
    VkPhysicalDevice devices[MAX_DEVICES];
    uint32_t count = MAX_DEVICES;
    VkResult result = vkEnumeratePhysicalDevices(gpu->instance, &count, devices);
    if (result != VK_INCOMPLETE) {
        gpu_check(result, "vkEnumeratePhysicalDevices");
    }
    for (uint32_t i = 0; i < count; i++) {
        if (find_graphics_family(devices[i], &gpu->queue_family)) {
            gpu->physical = devices[i];
            return;
        }
    }
    fprintf(stderr, "vulkan: no device with a graphics queue\n");
    exit(1);
}

static void create_device(Gpu *gpu)
{
    float priority = QUEUE_PRIORITY;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                     .queueFamilyIndex = gpu->queue_family,
                                     .queueCount = 1,
                                     .pQueuePriorities = &priority};
    VkDeviceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                               .queueCreateInfoCount = 1,
                               .pQueueCreateInfos = &queue};
    gpu_check(vkCreateDevice(gpu->physical, &info, NULL, &gpu->device), "vkCreateDevice");
    vkGetDeviceQueue(gpu->device, gpu->queue_family, 0, &gpu->queue);
}

void gpu_create(Gpu *gpu)
{
    create_instance(gpu);
    pick_device(gpu);
    vkGetPhysicalDeviceMemoryProperties(gpu->physical, &gpu->memory);
    create_device(gpu);
}

void gpu_destroy(Gpu *gpu)
{
    vkDestroyDevice(gpu->device, NULL);
    vkDestroyInstance(gpu->instance, NULL);
}

uint32_t gpu_find_memory(const Gpu *gpu, uint32_t type_bits, VkMemoryPropertyFlags wanted)
{
    for (uint32_t i = 0; i < gpu->memory.memoryTypeCount; i++) {
        int is_allowed = (type_bits & (1u << i)) != 0;
        if (is_allowed && (gpu->memory.memoryTypes[i].propertyFlags & wanted) == wanted) {
            return i;
        }
    }
    fprintf(stderr, "vulkan: no suitable memory type\n");
    exit(1);
}
