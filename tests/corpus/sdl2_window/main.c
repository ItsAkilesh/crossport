/*
 * File: tests/corpus/sdl2_window/main.c
 * Purpose: SDL2 program that opens an X11 window, waits for it to be exposed, draws into the window surface and prints what it reads back
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Differential test of a real toolkit on the in-process Xlib: the same libSDL2 runs against Xvfb on Linux
 * and against the shim on Windows. Only the X11 video driver is allowed, and only the window surface (no
 * renderer, no OpenGL) is used.
 */

#include <stdint.h>
#include <stdio.h>

#include <SDL2/SDL.h>

#define WIDTH 200
#define HEIGHT 150
#define EXPOSE_TIMEOUT_MS 5000
#define POLL_DELAY_MS 10
#define FNV_OFFSET 1469598103934665603ull
#define FNV_PRIME 1099511628211ull
#define PIXEL_MASK 0xffffffu

static int wait_for_expose(void)
{
    SDL_Event event;
    Uint32 started = SDL_GetTicks();
    while (SDL_GetTicks() - started < EXPOSE_TIMEOUT_MS) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                return 1;
            }
        }
        SDL_Delay(POLL_DELAY_MS);
    }
    return 0;
}

static uint32_t pixel_at(const SDL_Surface *surface, int x, int y)
{
    const uint8_t *row = (const uint8_t *)surface->pixels + (size_t)y * (size_t)surface->pitch;
    return ((const uint32_t *)row)[x] & PIXEL_MASK;
}

static void draw(SDL_Surface *surface)
{
    SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0x10, 0x20, 0x30));
    SDL_Rect red = {10, 10, 50, 40};
    SDL_Rect green = {80, 20, 60, 30};
    SDL_FillRect(surface, &red, SDL_MapRGB(surface->format, 0xff, 0x00, 0x00));
    SDL_FillRect(surface, &green, SDL_MapRGB(surface->format, 0x00, 0xff, 0x00));
}

static void print_surface(const SDL_Surface *surface)
{
    uint64_t hash = FNV_OFFSET;
    for (int y = 0; y < surface->h; y++) {
        for (int x = 0; x < surface->w; x++) {
            hash = (hash ^ pixel_at(surface, x, y)) * FNV_PRIME;
        }
    }
    printf("surface: %dx%d bpp=%d masks=%06x/%06x/%06x\n", surface->w, surface->h, surface->format->BitsPerPixel,
           surface->format->Rmask & PIXEL_MASK, surface->format->Gmask & PIXEL_MASK, surface->format->Bmask & PIXEL_MASK);
    printf("pixels: bg=%06x red=%06x green=%06x hash=%016llx\n", pixel_at(surface, 190, 140), pixel_at(surface, 20, 20),
           pixel_at(surface, 100, 30), (unsigned long long)hash);
}

int main(void)
{
    /* Unbuffered, so that what was printed before a hang survives the harness's timeout. */
    setvbuf(stdout, NULL, _IONBF, 0);
    SDL_SetHint("SDL_VIDEODRIVER", "x11");
    /* Without this, SDL first tries to back the window surface with an accelerated renderer (OpenGL). */
    SDL_SetHint("SDL_FRAMEBUFFER_ACCELERATION", "0");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("init failed: %s\n", SDL_GetError());
        return 1;
    }
    printf("driver=%s\n", SDL_GetCurrentVideoDriver());
    SDL_Window *window = SDL_CreateWindow("sdl2_window", 40, 40, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    if (window == NULL) {
        printf("window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    printf("window=%dx%d exposed=%d\n", width, height, wait_for_expose());
    SDL_Surface *surface = SDL_GetWindowSurface(window);
    if (surface == NULL) {
        printf("surface failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    puts("surface acquired");
    draw(surface);
    puts("surface drawn");
    printf("update=%d\n", SDL_UpdateWindowSurface(window));
    print_surface(surface);
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("done");
    return 0;
}
