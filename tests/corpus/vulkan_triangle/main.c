/*
 * File: tests/corpus/vulkan_triangle/main.c
 * Purpose: Render a triangle offscreen with Vulkan and print driver-independent results: coarse colour probes and a coverage checksum
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <stdio.h>
#include <stdlib.h>

#include "render.h"

#define FNV_OFFSET 0xcbf29ce484222325ull
#define FNV_PRIME 0x100000001b3ull
/* Colour channels are shown in sixteenths: drivers may differ by one step in interpolated colours. */
#define COLOUR_SHIFT 4

typedef struct {
    const char *label;
    int x;
    int y;
} Probe;

static const Probe PROBES[] = {{"corner", 2, 2}, {"top", TARGET_WIDTH / 2, TARGET_HEIGHT / 4 + 8},
                               {"centre", TARGET_WIDTH / 2, TARGET_HEIGHT / 2},
                               {"bottom-left", TARGET_WIDTH / 4, TARGET_HEIGHT * 7 / 8 - 4},
                               {"bottom-right", TARGET_WIDTH * 3 / 4, TARGET_HEIGHT * 7 / 8 - 4}};

/* Hash of which pixels the triangle covers. Coverage is fixed by Vulkan's rasterisation rules, colours are not. */
static uint64_t coverage_checksum(const uint8_t *pixels)
{
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < (size_t)TARGET_WIDTH * TARGET_HEIGHT; i++) {
        const uint8_t *at = pixels + i * BYTES_PER_PIXEL;
        uint8_t is_covered = (at[0] | at[1] | at[2]) != 0;
        hash = (hash ^ is_covered) * FNV_PRIME;
    }
    return hash;
}

static void print_probe(const uint8_t *pixels, const Probe *probe)
{
    const uint8_t *at = pixels + ((size_t)probe->y * TARGET_WIDTH + (size_t)probe->x) * BYTES_PER_PIXEL;
    printf("%s (%d,%d): %u %u %u %u\n", probe->label, probe->x, probe->y, at[0] >> COLOUR_SHIFT, at[1] >> COLOUR_SHIFT,
           at[2] >> COLOUR_SHIFT, at[3] >> COLOUR_SHIFT);
}

/* Writes the full image as a PPM, for looking at; the corpus run does not pass a path. */
static int write_ppm(const char *path, const uint8_t *pixels)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "P6\n%d %d\n255\n", TARGET_WIDTH, TARGET_HEIGHT);
    for (size_t i = 0; i < (size_t)TARGET_WIDTH * TARGET_HEIGHT; i++) {
        fwrite(pixels + i * BYTES_PER_PIXEL, 1, 3, file);
    }
    return fclose(file);
}

int main(int argc, char **argv)
{
    static uint8_t pixels[IMAGE_BYTES];
    Gpu gpu;
    gpu_create(&gpu);
    render_triangle(&gpu, pixels);
    gpu_destroy(&gpu);
    printf("rendered %dx%d\n", TARGET_WIDTH, TARGET_HEIGHT);
    for (size_t i = 0; i < sizeof(PROBES) / sizeof(PROBES[0]); i++) {
        print_probe(pixels, &PROBES[i]);
    }
    printf("coverage %016llx\n", (unsigned long long)coverage_checksum(pixels));
    if (argc > 1 && write_ppm(argv[1], pixels) != 0) {
        fprintf(stderr, "cannot write %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
