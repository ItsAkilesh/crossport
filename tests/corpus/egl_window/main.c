/*
 * File: tests/corpus/egl_window/main.c
 * Purpose: EGL client that renders to a window surface and a pbuffer, makes an OpenGL ES 2 context, and prints what it saw
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * Differential test for libEGL: the same program runs on Linux (Mesa under Xvfb) and on Windows through the
 * shim (WGL), and the output must match. It prints only what every conforming implementation produces: the
 * configuration it asked for, read-back pixels of rectangles aligned to the pixel grid, a hash of the frame,
 * and error codes. Nothing it prints names the driver or its version.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <GL/gl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define WIDTH 200
#define HEIGHT 150
#define PBUFFER_SIZE 64
#define CHANNELS 4
#define FNV_OFFSET 1469598103934665603ull
#define FNV_PRIME 1099511628211ull

typedef const GLubyte *(*GetStringFn)(GLenum name);

static uint8_t g_pixels[WIDTH * HEIGHT * CHANNELS];

static void wait_for_expose(Display *display)
{
    XEvent event;
    do {
        XNextEvent(display, &event);
    } while (event.type != Expose);
}

static EGLConfig choose_config(EGLDisplay display, EGLint renderable, const char *label)
{
    EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, renderable,
                           EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE};
    EGLConfig config = NULL;
    EGLint count = 0;
    EGLint value = 0;
    EGLBoolean is_chosen = eglChooseConfig(display, attributes, &config, 1, &count);
    printf("%s: chosen=%d count_positive=%d\n", label, is_chosen, count > 0);
    if (is_chosen && count > 0) {
        eglGetConfigAttrib(display, config, EGL_RED_SIZE, &value);
        printf("%s: red_at_least_8=%d\n", label, value >= 8);
        eglGetConfigAttrib(display, config, EGL_SURFACE_TYPE, &value);
        printf("%s: window_and_pbuffer=%d\n", label, (value & EGL_WINDOW_BIT) != 0 && (value & EGL_PBUFFER_BIT) != 0);
    }
    return count > 0 ? config : NULL;
}

static void draw_rectangle(float red, float green, float blue, float left, float bottom, float right, float top)
{
    glColor3f(red, green, blue);
    glBegin(GL_QUADS);
    glVertex2f(left, bottom);
    glVertex2f(right, bottom);
    glVertex2f(right, top);
    glVertex2f(left, top);
    glEnd();
}

static void draw_frame(void)
{
    glViewport(0, 0, WIDTH, HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, WIDTH, 0, HEIGHT, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(16.0f / 255.0f, 32.0f / 255.0f, 48.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_rectangle(1.0f, 0.0f, 0.0f, 10, 10, 60, 50);
    draw_rectangle(0.0f, 1.0f, 0.0f, 80, 20, 140, 50);
    glFinish();
}

static void print_pixel(const char *name, int x, int y)
{
    const uint8_t *pixel = &g_pixels[((size_t)y * WIDTH + (size_t)x) * CHANNELS];
    printf("  %s=%02x%02x%02x%02x\n", name, pixel[0], pixel[1], pixel[2], pixel[3]);
}

static void print_frame(void)
{
    uint64_t hash = FNV_OFFSET;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, g_pixels);
    for (size_t i = 0; i < sizeof(g_pixels); i++) {
        hash = (hash ^ g_pixels[i]) * FNV_PRIME;
    }
    print_pixel("background", 190, 140);
    print_pixel("red", 20, 20);
    print_pixel("green", 100, 30);
    print_pixel("red_edge_inside", 59, 49);
    print_pixel("red_edge_outside", 60, 50);
    printf("  hash=%016llx\n", (unsigned long long)hash);
}

static void render_window(EGLDisplay display, EGLConfig config, Window window)
{
    EGLSurface surface = eglCreateWindowSurface(display, config, window, NULL);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    EGLint width = 0;
    EGLint height = 0;
    printf("window_surface=%d context=%d\n", surface != EGL_NO_SURFACE, context != EGL_NO_CONTEXT);
    printf("made_current=%d\n", eglMakeCurrent(display, surface, surface, context));
    eglQuerySurface(display, surface, EGL_WIDTH, &width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &height);
    printf("surface_size=%dx%d\n", width, height);
    printf("current_ok=%d\n", eglGetCurrentContext() == context && eglGetCurrentSurface(EGL_DRAW) == surface &&
                                  eglGetCurrentDisplay() == display);
    printf("window frame:\n");
    draw_frame();
    print_frame();
    printf("swap=%d\n", eglSwapBuffers(display, surface));
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
}

static void render_pbuffer(EGLDisplay display, EGLConfig config)
{
    EGLint attributes[] = {EGL_WIDTH, PBUFFER_SIZE, EGL_HEIGHT, PBUFFER_SIZE, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, attributes);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    uint8_t pixel[CHANNELS] = {0};
    EGLint width = 0;
    printf("pbuffer=%d\n", surface != EGL_NO_SURFACE);
    printf("pbuffer_current=%d\n", eglMakeCurrent(display, surface, surface, context));
    eglQuerySurface(display, surface, EGL_WIDTH, &width);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(PBUFFER_SIZE / 2, PBUFFER_SIZE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("pbuffer_width=%d pixel=%02x%02x%02x%02x\n", width, pixel[0], pixel[1], pixel[2], pixel[3]);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
}

static void try_opengl_es(EGLDisplay display)
{
    EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLBoolean is_bound = eglBindAPI(EGL_OPENGL_ES_API);
    printf("bind_es=%d api_is_es=%d\n", is_bound, eglQueryAPI() == EGL_OPENGL_ES_API);
    EGLConfig config = choose_config(display, EGL_OPENGL_ES2_BIT, "es2");
    EGLContext context = config != NULL ? eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes) : EGL_NO_CONTEXT;
    EGLint version = 0;
    printf("es2_context=%d\n", context != EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT) {
        eglQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION, &version);
        printf("es2_client_version=%d\n", version);
        eglDestroyContext(display, context);
    }
}

int main(void)
{
    Display *x_display = XOpenDisplay(NULL);
    if (x_display == NULL) {
        puts("no display");
        return 1;
    }
    EGLDisplay display = eglGetDisplay((EGLNativeDisplayType)x_display);
    EGLint major = 0;
    EGLint minor = 0;
    printf("initialized=%d\n", eglInitialize(display, &major, &minor));
    printf("egl_at_least_1_4=%d\n", major > 1 || (major == 1 && minor >= 4));
    printf("bind_gl=%d\n", eglBindAPI(EGL_OPENGL_API));
    EGLConfig config = choose_config(display, EGL_OPENGL_BIT, "gl");
    if (config == NULL) {
        return 1;
    }
    EGLint visual_id = 0;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &visual_id);
    XVisualInfo wanted = {.visualid = (VisualID)visual_id};
    int visual_count = 0;
    XVisualInfo *visual = XGetVisualInfo(x_display, VisualIDMask, &wanted, &visual_count);
    if (visual == NULL) {
        puts("no visual");
        return 1;
    }
    XSetWindowAttributes window_attributes = {0};
    window_attributes.colormap = XCreateColormap(x_display, RootWindow(x_display, visual->screen), visual->visual, AllocNone);
    window_attributes.event_mask = ExposureMask;
    Window window = XCreateWindow(x_display, RootWindow(x_display, visual->screen), 40, 40, WIDTH, HEIGHT, 0, visual->depth,
                                  InputOutput, visual->visual, CWColormap | CWEventMask, &window_attributes);
    XMapWindow(x_display, window);
    wait_for_expose(x_display);
    render_window(display, config, window);
    render_pbuffer(display, config);
    try_opengl_es(display);
    printf("error=%x\n", eglGetError());
    eglTerminate(display);
    XFree(visual);
    XDestroyWindow(x_display, window);
    XCloseDisplay(x_display);
    puts("done");
    return 0;
}
