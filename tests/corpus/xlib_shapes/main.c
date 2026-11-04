/*
 * File: tests/corpus/xlib_shapes/main.c
 * Purpose: Xlib client that fills polygons and draws rectangles into a pixmap and prints the pixels each shape covers
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * Differential test for polygon filling: the same program runs against a real X server (Xvfb) on Linux and
 * against the shim on Windows, and the output must match pixel for pixel. It covers convex and concave polygons,
 * a self-intersecting star under both fill rules, relative coordinates, and diagonal edges that pass exactly
 * through pixel centres. Arcs are approximate in the shim, so they are only checked at their centre and corner.
 */

#include <stdint.h>
#include <stdio.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define WIDTH 120
#define HEIGHT 100
#define BACKGROUND 0x102030ul
#define FOREGROUND 0xe0c040ul
#define PIXEL_MASK 0xfffffful
#define FNV_OFFSET 1469598103934665603ull
#define FNV_PRIME 1099511628211ull

static Display *g_display;
static Pixmap g_pixmap;
static GC g_gc;

static void clear(void)
{
    XSetForeground(g_display, g_gc, BACKGROUND);
    XFillRectangle(g_display, g_pixmap, g_gc, 0, 0, WIDTH, HEIGHT);
    XSetForeground(g_display, g_gc, FOREGROUND);
}

/* Prints how many pixels differ from the background and a hash of where they are. */
static void report(const char *name)
{
    XImage *image = XGetImage(g_display, g_pixmap, 0, 0, WIDTH, HEIGHT, AllPlanes, ZPixmap);
    uint64_t hash = FNV_OFFSET;
    int covered = 0;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            int is_set = (XGetPixel(image, x, y) & PIXEL_MASK) == FOREGROUND;
            covered += is_set;
            hash = (hash ^ (uint64_t)(is_set ? (y * WIDTH + x + 1) : 0)) * FNV_PRIME;
        }
    }
    printf("%s: pixels=%d hash=%016llx\n", name, covered, (unsigned long long)hash);
    XDestroyImage(image);
}

static void polygon(const char *name, XPoint *points, int count, int shape, int mode, int rule)
{
    clear();
    XSetFillRule(g_display, g_gc, rule);
    XFillPolygon(g_display, g_pixmap, g_gc, points, count, shape, mode);
    report(name);
}

static void polygons(void)
{
    XPoint triangle[] = {{10, 10}, {60, 10}, {35, 50}};
    XPoint arrow[] = {{10, 40}, {40, 40}, {40, 20}, {70, 50}, {40, 80}, {40, 60}, {10, 60}};
    XPoint star[] = {{60, 5}, {80, 95}, {10, 35}, {110, 35}, {40, 95}};
    XPoint diagonal[] = {{0, 0}, {20, 20}, {0, 20}};
    XPoint steep[] = {{3, 3}, {5, 40}, {1, 60}, {30, 30}};
    XPoint relative[] = {{20, 20}, {30, 0}, {0, 30}, {-30, 0}};
    polygon("triangle", triangle, 3, Convex, CoordModeOrigin, EvenOddRule);
    polygon("arrow", arrow, 7, Nonconvex, CoordModeOrigin, EvenOddRule);
    polygon("star even-odd", star, 5, Complex, CoordModeOrigin, EvenOddRule);
    polygon("star winding", star, 5, Complex, CoordModeOrigin, WindingRule);
    polygon("diagonal through centres", diagonal, 3, Convex, CoordModeOrigin, EvenOddRule);
    polygon("steep", steep, 4, Nonconvex, CoordModeOrigin, EvenOddRule);
    polygon("relative", relative, 4, Convex, CoordModePrevious, EvenOddRule);
}

static void rectangles(void)
{
    XRectangle filled[] = {{5, 5, 20, 10}, {30, 20, 15, 15}};
    XRectangle outlines[] = {{50, 50, 30, 20}, {10, 60, 8, 8}};
    clear();
    XFillRectangles(g_display, g_pixmap, g_gc, filled, 2);
    report("fill rectangles");
    clear();
    XDrawRectangles(g_display, g_pixmap, g_gc, outlines, 2);
    report("draw rectangles");
}

/* Arcs differ slightly between servers, so only what every correct arc shares is printed. */
static void arc(void)
{
    clear();
    XFillArc(g_display, g_pixmap, g_gc, 20, 20, 60, 60, 0, 360 * 64);
    XImage *image = XGetImage(g_display, g_pixmap, 0, 0, WIDTH, HEIGHT, AllPlanes, ZPixmap);
    printf("arc: centre_filled=%d corner_empty=%d far_empty=%d\n", (XGetPixel(image, 50, 50) & PIXEL_MASK) == FOREGROUND,
           (XGetPixel(image, 21, 21) & PIXEL_MASK) == BACKGROUND, (XGetPixel(image, 110, 90) & PIXEL_MASK) == BACKGROUND);
    XDestroyImage(image);
}

int main(void)
{
    g_display = XOpenDisplay(NULL);
    if (g_display == NULL) {
        puts("no display");
        return 1;
    }
    g_pixmap = XCreatePixmap(g_display, DefaultRootWindow(g_display), WIDTH, HEIGHT, DefaultDepth(g_display, 0));
    g_gc = XCreateGC(g_display, g_pixmap, 0, NULL);
    polygons();
    rectangles();
    arc();
    XFreeGC(g_display, g_gc);
    XFreePixmap(g_display, g_pixmap);
    XCloseDisplay(g_display);
    puts("done");
    return 0;
}
