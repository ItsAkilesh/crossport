/*
 * File: tests/corpus/vulkan_triangle/pipeline.h
 * Purpose: Render pass and graphics pipeline for the triangle sample
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef VULKAN_TRIANGLE_PIPELINE_H
#define VULKAN_TRIANGLE_PIPELINE_H

#include "gpu.h"

#define TARGET_WIDTH 128
#define TARGET_HEIGHT 128
#define TARGET_FORMAT VK_FORMAT_R8G8B8A8_UNORM

typedef struct {
    VkRenderPass render_pass;
    VkPipelineLayout layout;
    VkPipeline pipeline;
} Pipeline;

void pipeline_create(const Gpu *gpu, Pipeline *pipeline);
void pipeline_destroy(const Gpu *gpu, Pipeline *pipeline);

#endif
