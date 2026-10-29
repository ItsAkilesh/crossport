/*
 * File: tests/corpus/vulkan_triangle/render.h
 * Purpose: Offscreen rendering of the triangle into a host-readable RGBA image
 * Author: Akilesh
 * Date: 2026-10-29
 */

#ifndef VULKAN_TRIANGLE_RENDER_H
#define VULKAN_TRIANGLE_RENDER_H

#include <stdint.h>

#include "gpu.h"
#include "pipeline.h"

#define BYTES_PER_PIXEL 4
#define IMAGE_BYTES (TARGET_WIDTH * TARGET_HEIGHT * BYTES_PER_PIXEL)

/* Draws the triangle and copies the finished image, row by row, into `pixels` (IMAGE_BYTES long). */
void render_triangle(const Gpu *gpu, uint8_t *pixels);

#endif
