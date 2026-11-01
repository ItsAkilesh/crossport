/*
 * File: tests/corpus/gl_window/main.c
 * Purpose: GLX client that opens an X window, creates an OpenGL context, draws pixel-aligned rectangles, reads them back and prints what it saw
 * Author: Akilesh
 * Date: 2026-11-01
 *
 * Differential test for libGL and libGLX: the same program runs on Linux (Mesa under Xvfb) and on Windows
 * through the shim (WGL), and the output must match. It prints only what every conforming implementation
 * produces: the configuration it asked for, read-back pixels of rectangles aligned to the pixel grid, and a
 * hash of the whole frame. Nothing it prints names the driver or its version.
 */

#include <stdint.h>
#include <stdio.h>

#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define WIDTH 200
#define HEIGHT 150
#define CHANNELS 4
#define FNV_OFFSET 1469598103934665603ull
#define FNV_PRIME 1099511628211ull

typedef void (*GenBuffersFn)(GLsizei count, GLuint *buffers);
typedef void (*DeleteBuffersFn)(GLsizei count, const GLuint *buffers);

static uint8_t g_pixels[WIDTH * HEIGHT * CHANNELS];

static void wait_for_expose(Display *display)
{
    XEvent event;
    do {
        XNextEvent(display, &event);
    } while (event.type != Expose);
}

static void print_configuration(Display *display, XVisualInfo *visual)
{
    int major = 0;
    int minor = 0;
    int value = 0;
    glXQueryVersion(display, &major, &minor);
    printf("glx_at_least_1_3=%d\n", major > 1 || (major == 1 && minor >= 3));
    printf("visual_depth=%d\n", visual->depth);
    glXGetConfig(display, visual, GLX_RGBA, &value);
    printf("rgba=%d\n", value);
    glXGetConfig(display, visual, GLX_DOUBLEBUFFER, &value);
    printf("double_buffer=%d\n", value);
    glXGetConfig(display, visual, GLX_DEPTH_SIZE, &value);
    printf("depth_at_least_16=%d\n", value >= 16);
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

static void draw_frame(float background_red)
{
    glViewport(0, 0, WIDTH, HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, WIDTH, 0, HEIGHT, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(background_red, 32.0f / 255.0f, 48.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_rectangle(1.0f, 0.0f, 0.0f, 10, 10, 60, 50);
    draw_rectangle(0.0f, 1.0f, 0.0f, 80, 20, 140, 50);
    glFinish();
}

static void read_frame(void)
{
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, g_pixels);
}

static void print_pixel(const char *name, int x, int y)
{
    const uint8_t *pixel = &g_pixels[((size_t)y * WIDTH + (size_t)x) * CHANNELS];
    printf("%s=%02x%02x%02x%02x\n", name, pixel[0], pixel[1], pixel[2], pixel[3]);
}

static void print_frame(const char *label)
{
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < sizeof(g_pixels); i++) {
        hash = (hash ^ g_pixels[i]) * FNV_PRIME;
    }
    printf("%s:\n", label);
    print_pixel("  background", 190, 140);
    print_pixel("  red", 20, 20);
    print_pixel("  green", 100, 30);
    print_pixel("  red_edge_inside", 59, 49);
    print_pixel("  red_edge_outside", 60, 50);
    printf("  hash=%016llx\n", (unsigned long long)hash);
}

static void exercise_entry_points(void)
{
    GenBuffersFn generate = (GenBuffersFn)glXGetProcAddress((const GLubyte *)"glGenBuffers");
    DeleteBuffersFn destroy = (DeleteBuffersFn)glXGetProcAddress((const GLubyte *)"glDeleteBuffers");
    GLuint buffer = 0;
    printf("proc_address=%d\n", generate != NULL && destroy != NULL);
    if (generate != NULL && destroy != NULL) {
        generate(1, &buffer);
        printf("buffer_created=%d\n", buffer != 0);
        destroy(1, &buffer);
    }
    printf("version_string=%d\n", glGetString(GL_VERSION) != NULL);
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        puts("no display");
        return 1;
    }
    int attributes[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16, None};
    XVisualInfo *visual = glXChooseVisual(display, DefaultScreen(display), attributes);
    if (visual == NULL) {
        puts("no visual");
        return 1;
    }
    print_configuration(display, visual);
    Window root = RootWindow(display, visual->screen);
    XSetWindowAttributes window_attributes = {0};
    window_attributes.colormap = XCreateColormap(display, root, visual->visual, AllocNone);
    window_attributes.event_mask = ExposureMask | StructureNotifyMask;
    Window window = XCreateWindow(display, root, 40, 40, WIDTH, HEIGHT, 0, visual->depth, InputOutput, visual->visual,
                                  CWColormap | CWEventMask, &window_attributes);
    XMapWindow(display, window);
    wait_for_expose(display);
    GLXContext context = glXCreateContext(display, visual, NULL, True);
    printf("context=%d\n", context != NULL);
    printf("made_current=%d\n", glXMakeCurrent(display, window, context));
    printf("is_current=%d drawable_is_window=%d\n", glXGetCurrentContext() == context, glXGetCurrentDrawable() == window);
    exercise_entry_points();
    draw_frame(16.0f / 255.0f);
    read_frame();
    print_frame("first frame");
    glXSwapBuffers(display, window);
    draw_frame(64.0f / 255.0f);
    read_frame();
    print_frame("second frame");
    glXSwapBuffers(display, window);
    printf("gl_error=%u\n", glGetError());
    glXMakeCurrent(display, None, NULL);
    glXDestroyContext(display, context);
    XFree(visual);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    puts("done");
    return 0;
}
