/*
 * File: tests/corpus/sdl2_input/main.c
 * Purpose: SDL2 program that injects keys, pointer moves, a click and a wheel notch with XTest and prints the SDL events it receives
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * Differential test for input through a real toolkit: the same libSDL2 runs against Xvfb on Linux and against
 * the shim on Windows. The program asks SDL for its X display, injects input through XTest, and prints every
 * keyboard, text, mouse and wheel event SDL delivers, with the modifiers and coordinates. The output must match.
 */

#include <stdint.h>
#include <stdio.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#define WIDTH 200
#define HEIGHT 150
#define ORIGIN 40
#define FIRST_X 100
#define FIRST_Y 90
#define SECOND_X 120
#define SECOND_Y 100
#define PARK_X 500
#define PARK_Y 400
#define EXPOSE_TIMEOUT_MS 5000
#define COLLECT_MS 3000
#define POLL_DELAY_MS 10
#define WHEEL_UP 4
#define REPORTED_MODS (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT)

static int g_last_x = -1;
static int g_last_y = -1;

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

static void print_key(const SDL_KeyboardEvent *key)
{
    printf("%s scancode=%d keycode=%x mod=%x repeat=%d\n", key->type == SDL_KEYDOWN ? "KEYDOWN" : "KEYUP",
           (int)key->keysym.scancode, (unsigned)key->keysym.sym, (unsigned)(key->keysym.mod & REPORTED_MODS), key->repeat);
}

/* Pointer moves are reported once per distinct position: Windows can post two messages for one move. */
static void print_motion(const SDL_MouseMotionEvent *motion)
{
    if (motion->x == g_last_x && motion->y == g_last_y) {
        return;
    }
    g_last_x = motion->x;
    g_last_y = motion->y;
    printf("MOUSEMOTION x=%d y=%d state=%x\n", motion->x, motion->y, (unsigned)motion->state);
}

static void print_event(const SDL_Event *event)
{
    switch (event->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        print_key(&event->key);
        break;
    case SDL_TEXTINPUT:
        printf("TEXTINPUT text=%s\n", event->text.text);
        break;
    case SDL_MOUSEMOTION:
        print_motion(&event->motion);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        printf("%s button=%d x=%d y=%d clicks=%d\n", event->type == SDL_MOUSEBUTTONDOWN ? "MOUSEBUTTONDOWN" : "MOUSEBUTTONUP",
               event->button.button, event->button.x, event->button.y, event->button.clicks);
        break;
    case SDL_MOUSEWHEEL:
        printf("MOUSEWHEEL x=%d y=%d direction=%d\n", event->wheel.x, event->wheel.y, (int)event->wheel.direction);
        break;
    default:
        break;
    }
}

static void tap(Display *display, KeySym keysym, int is_press)
{
    XTestFakeKeyEvent(display, XKeysymToKeycode(display, keysym), is_press, 0);
}

static void type_with_modifier(Display *display, KeySym modifier)
{
    tap(display, modifier, True);
    tap(display, XK_a, True);
    tap(display, XK_a, False);
    tap(display, modifier, False);
}

static void inject_input(Display *display)
{
    XTestFakeMotionEvent(display, DefaultScreen(display), FIRST_X, FIRST_Y, 0);
    type_with_modifier(display, XK_Shift_L);
    type_with_modifier(display, XK_Control_L);
    XTestFakeButtonEvent(display, Button1, True, 0);
    XTestFakeButtonEvent(display, Button1, False, 0);
    XTestFakeButtonEvent(display, WHEEL_UP, True, 0);
    XTestFakeButtonEvent(display, WHEEL_UP, False, 0);
    XTestFakeMotionEvent(display, DefaultScreen(display), SECOND_X, SECOND_Y, 0);
    XFlush(display);
}

static void collect_events(void)
{
    SDL_Event event;
    Uint32 started = SDL_GetTicks();
    while (SDL_GetTicks() - started < COLLECT_MS) {
        while (SDL_PollEvent(&event)) {
            print_event(&event);
        }
        SDL_Delay(POLL_DELAY_MS);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    SDL_SetHint("SDL_VIDEODRIVER", "x11");
    SDL_SetHint("SDL_FRAMEBUFFER_ACCELERATION", "0");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("sdl2_input", ORIGIN, ORIGIN, WIDTH, HEIGHT, SDL_WINDOW_HIDDEN);
    if (window == NULL) {
        printf("window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_X11) {
        puts("no X11 window");
        return 1;
    }
    /* The pointer keeps its position between runs on Windows; park it away from where the window will appear. */
    XTestFakeMotionEvent(info.info.x11.display, DefaultScreen(info.info.x11.display), PARK_X, PARK_Y, 0);
    XSync(info.info.x11.display, False);
    SDL_ShowWindow(window);
    printf("exposed=%d\n", wait_for_expose());
    SDL_SetWindowInputFocus(window);
    SDL_StartTextInput();
    XSync(info.info.x11.display, False);
    inject_input(info.info.x11.display);
    collect_events();
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("done");
    return 0;
}
