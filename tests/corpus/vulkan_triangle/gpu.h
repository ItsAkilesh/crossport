/*
 * File: tests/corpus/vulkan_triangle/gpu.h
 * Purpose: Instance, device and queue setup shared by the Vulkan sample's files
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef VULKAN_TRIANGLE_GPU_H
#define VULKAN_TRIANGLE_GPU_H

#include <vulkan/vulkan.h>

typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    VkPhysicalDeviceMemoryProperties memory;
} Gpu;

/* Aborts the program with a message naming the failed step. */
void gpu_check(VkResult result, const char *step);

void gpu_create(Gpu *gpu);
void gpu_destroy(Gpu *gpu);

/* Index of a memory type allowed by `type_bits` that has all of `wanted` properties. */
uint32_t gpu_find_memory(const Gpu *gpu, uint32_t type_bits, VkMemoryPropertyFlags wanted);

#endif
