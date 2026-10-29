/*
 * File: tests/corpus/vulkan_triangle/pipeline.c
 * Purpose: Build the render pass and the graphics pipeline that draws the generated triangle shaders
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <stddef.h>

#include "pipeline.h"
#include "shaders.h"

static VkShaderModule make_module(const Gpu *gpu, const uint32_t *words, size_t byte_count)
{
    VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                     .codeSize = byte_count,
                                     .pCode = words};
    VkShaderModule module;
    gpu_check(vkCreateShaderModule(gpu->device, &info, NULL, &module), "vkCreateShaderModule");
    return module;
}

static void make_render_pass(const Gpu *gpu, Pipeline *pipeline)
{
    VkAttachmentDescription color = {.format = TARGET_FORMAT,
                                     .samples = VK_SAMPLE_COUNT_1_BIT,
                                     .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                     .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                     .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                     .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                     .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                     .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    VkAttachmentReference reference = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &reference};
    VkRenderPassCreateInfo info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                   .attachmentCount = 1,
                                   .pAttachments = &color,
                                   .subpassCount = 1,
                                   .pSubpasses = &subpass};
    gpu_check(vkCreateRenderPass(gpu->device, &info, NULL, &pipeline->render_pass), "vkCreateRenderPass");
}

static void make_layout(const Gpu *gpu, Pipeline *pipeline)
{
    VkPipelineLayoutCreateInfo info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    gpu_check(vkCreatePipelineLayout(gpu->device, &info, NULL, &pipeline->layout), "vkCreatePipelineLayout");
}

static void make_pipeline(const Gpu *gpu, Pipeline *pipeline, VkShaderModule vertex, VkShaderModule fragment)
{
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragment, .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo vertex_input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                       .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {0.0f, 0.0f, (float)TARGET_WIDTH, (float)TARGET_HEIGHT, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {TARGET_WIDTH, TARGET_HEIGHT}};
    VkPipelineViewportStateCreateInfo viewport_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                        .viewportCount = 1, .pViewports = &viewport,
                                                        .scissorCount = 1, .pScissors = &scissor};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
                                                     .frontFace = VK_FRONT_FACE_CLOCKWISE, .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo multisample = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState attachment = {.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                 .attachmentCount = 1, .pAttachments = &attachment};
    VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                         .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex_input,
                                         .pInputAssemblyState = &assembly, .pViewportState = &viewport_state,
                                         .pRasterizationState = &raster, .pMultisampleState = &multisample,
                                         .pColorBlendState = &blend, .layout = pipeline->layout,
                                         .renderPass = pipeline->render_pass, .subpass = 0};
    gpu_check(vkCreateGraphicsPipelines(gpu->device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline->pipeline),
              "vkCreateGraphicsPipelines");
}

void pipeline_create(const Gpu *gpu, Pipeline *pipeline)
{
    VkShaderModule vertex = make_module(gpu, triangle_vert_spv, sizeof(triangle_vert_spv));
    VkShaderModule fragment = make_module(gpu, triangle_frag_spv, sizeof(triangle_frag_spv));
    make_render_pass(gpu, pipeline);
    make_layout(gpu, pipeline);
    make_pipeline(gpu, pipeline, vertex, fragment);
    vkDestroyShaderModule(gpu->device, vertex, NULL);
    vkDestroyShaderModule(gpu->device, fragment, NULL);
}

void pipeline_destroy(const Gpu *gpu, Pipeline *pipeline)
{
    vkDestroyPipeline(gpu->device, pipeline->pipeline, NULL);
    vkDestroyPipelineLayout(gpu->device, pipeline->layout, NULL);
    vkDestroyRenderPass(gpu->device, pipeline->render_pass, NULL);
}
