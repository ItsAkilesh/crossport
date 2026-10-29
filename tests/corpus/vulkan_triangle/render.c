/*
 * File: tests/corpus/vulkan_triangle/render.c
 * Purpose: Create the target image, record and submit the draw, and read the pixels back
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <string.h>

#include "render.h"

typedef struct {
    VkImage image;
    VkDeviceMemory image_memory;
    VkImageView view;
    VkFramebuffer framebuffer;
    VkBuffer readback;
    VkDeviceMemory readback_memory;
} Target;

static const VkClearValue CLEAR = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}};

static VkDeviceMemory bind_memory(const Gpu *gpu, VkMemoryRequirements need, VkMemoryPropertyFlags wanted)
{
    VkMemoryAllocateInfo info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 .allocationSize = need.size,
                                 .memoryTypeIndex = gpu_find_memory(gpu, need.memoryTypeBits, wanted)};
    VkDeviceMemory memory;
    gpu_check(vkAllocateMemory(gpu->device, &info, NULL, &memory), "vkAllocateMemory");
    return memory;
}

static void make_image(const Gpu *gpu, Target *target)
{
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                              .format = TARGET_FORMAT, .extent = {TARGET_WIDTH, TARGET_HEIGHT, 1},
                              .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VkMemoryRequirements need;
    gpu_check(vkCreateImage(gpu->device, &info, NULL, &target->image), "vkCreateImage");
    vkGetImageMemoryRequirements(gpu->device, target->image, &need);
    target->image_memory = bind_memory(gpu, need, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    gpu_check(vkBindImageMemory(gpu->device, target->image, target->image_memory, 0), "vkBindImageMemory");
}

static void make_view_and_framebuffer(const Gpu *gpu, const Pipeline *pipeline, Target *target)
{
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = target->image,
                                  .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = TARGET_FORMAT,
                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    gpu_check(vkCreateImageView(gpu->device, &view, NULL, &target->view), "vkCreateImageView");
    VkFramebufferCreateInfo frame = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                     .renderPass = pipeline->render_pass, .attachmentCount = 1,
                                     .pAttachments = &target->view, .width = TARGET_WIDTH,
                                     .height = TARGET_HEIGHT, .layers = 1};
    gpu_check(vkCreateFramebuffer(gpu->device, &frame, NULL, &target->framebuffer), "vkCreateFramebuffer");
}

static void make_readback(const Gpu *gpu, Target *target)
{
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = IMAGE_BYTES,
                               .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkMemoryRequirements need;
    gpu_check(vkCreateBuffer(gpu->device, &info, NULL, &target->readback), "vkCreateBuffer");
    vkGetBufferMemoryRequirements(gpu->device, target->readback, &need);
    target->readback_memory = bind_memory(gpu, need,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    gpu_check(vkBindBufferMemory(gpu->device, target->readback, target->readback_memory, 0), "vkBindBufferMemory");
}

static void record(VkCommandBuffer commands, const Pipeline *pipeline, const Target *target)
{
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                  .renderPass = pipeline->render_pass, .framebuffer = target->framebuffer,
                                  .renderArea = {{0, 0}, {TARGET_WIDTH, TARGET_HEIGHT}},
                                  .clearValueCount = 1, .pClearValues = &CLEAR};
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                              .imageExtent = {TARGET_WIDTH, TARGET_HEIGHT, 1}};
    gpu_check(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer");
    vkCmdBeginRenderPass(commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    vkCmdCopyImageToBuffer(commands, target->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target->readback, 1, &copy);
    gpu_check(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
}

static void submit_and_wait(const Gpu *gpu, const Pipeline *pipeline, const Target *target)
{
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                         .queueFamilyIndex = gpu->queue_family};
    VkCommandPool pool;
    VkCommandBuffer commands;
    gpu_check(vkCreateCommandPool(gpu->device, &pool_info, NULL, &pool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    gpu_check(vkAllocateCommandBuffers(gpu->device, &alloc, &commands), "vkAllocateCommandBuffers");
    record(commands, pipeline, target);
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commands};
    gpu_check(vkQueueSubmit(gpu->queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
    gpu_check(vkQueueWaitIdle(gpu->queue), "vkQueueWaitIdle");
    vkDestroyCommandPool(gpu->device, pool, NULL);
}

static void destroy_target(const Gpu *gpu, Target *target)
{
    vkDestroyFramebuffer(gpu->device, target->framebuffer, NULL);
    vkDestroyImageView(gpu->device, target->view, NULL);
    vkDestroyImage(gpu->device, target->image, NULL);
    vkFreeMemory(gpu->device, target->image_memory, NULL);
    vkDestroyBuffer(gpu->device, target->readback, NULL);
    vkFreeMemory(gpu->device, target->readback_memory, NULL);
}

void render_triangle(const Gpu *gpu, uint8_t *pixels)
{
    Target target;
    Pipeline pipeline;
    void *mapped;
    pipeline_create(gpu, &pipeline);
    make_image(gpu, &target);
    make_view_and_framebuffer(gpu, &pipeline, &target);
    make_readback(gpu, &target);
    submit_and_wait(gpu, &pipeline, &target);
    gpu_check(vkMapMemory(gpu->device, target.readback_memory, 0, IMAGE_BYTES, 0, &mapped), "vkMapMemory");
    memcpy(pixels, mapped, IMAGE_BYTES);
    vkUnmapMemory(gpu->device, target.readback_memory);
    destroy_target(gpu, &target);
    pipeline_destroy(gpu, &pipeline);
}
