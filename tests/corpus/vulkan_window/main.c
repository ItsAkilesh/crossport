/*
 * File: tests/corpus/vulkan_window/main.c
 * Purpose: Open an xcb window, present a fixed number of Vulkan frames into it, and close it
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <stdio.h>
#include <stdlib.h>

#include "presenter.h"

#define FRAME_COUNT 30
#define WINDOW_TITLE "crossport vulkan window"

int main(void)
{
    Window window;
    Presenter presenter;
    if (window_create(&window, WINDOW_TITLE) != 0) {
        fprintf(stderr, "cannot connect to the display\n");
        return EXIT_FAILURE;
    }
    presenter_create(&presenter, &window);
    printf("swapchain ready: %s\n", presenter.image_count >= 1 ? "yes" : "no");
    unsigned presented = 0;
    for (unsigned frame = 0; frame < FRAME_COUNT && !window_should_close(&window); frame++) {
        presenter_draw_frame(&presenter, frame);
        presented++;
    }
    printf("presented %u frames\n", presented);
    presenter_destroy(&presenter);
    window_destroy(&window);
    return EXIT_SUCCESS;
}
