/*
 * File: shim/xlib/xlib_display.c
 * Purpose: libX11 connection functions: XOpenDisplay, XCloseDisplay, XSync, atoms and the Display accessor functions
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * There is no X server to connect to. A Display is a block of memory that looks like Xlib's (the part
 * programs read through macros is laid out exactly) and the "server" is the set of Win32 windows and pixel
 * buffers behind the other xlib files. The display name is accepted and ignored.
 */

#include <string.h>

#include "fd.h"
#include "xlib_internal.h"

#define VENDOR_NAME "Crossport"
#define VENDOR_RELEASE 1
#define MAX_REQUEST_WORDS 262140u
#define MOTION_BUFFER_SIZE 256
#define XL_TRUE_COLOR 4
#define BITS_PER_RGB 8
#define COLORMAP_ENTRIES 256
#define DISPLAY_NAME ":0"
#define PROTOCOL_MAJOR 11
#define ROOT_COLORMAP_ID 0x20u
#define ROOT_VISUAL_NUMBER 0x21u
#define MILLIMETERS_PER_INCH_X10 254
#define ASSUMED_DPI 96

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);
LIBC_FN int sysv_eventfd(unsigned initial, int flags);
LIBC_FN int sysv_close(int fd);
LIBC_FN char *sysv_strdup(const char *text);
LIBC_FN uint32_t sysv_xcb_generate_id(void *connection);
LIBC_FN int sysv_eventfd_write(int fd, uint64_t value);
LIBC_FN int sysv_eventfd_read(int fd, uint64_t *value);

/* The display whose descriptor the poll engine pumps messages for. */
static XlDisplay *g_display;

XlDisplayData *xl_display_data(XlDisplay *display)
{
    return (XlDisplayData *)display;
}

uint64_t xl_next_request(XlDisplay *display)
{
    display->last_request_read = ++display->request;
    return display->request;
}

SYSV XlId xl_allocate_id(XlDisplay *display)
{
    (void)display;
    return sysv_xcb_generate_id(NULL);
}

SYSV void xl_sync_descriptor(XlDisplay *display)
{
    XlDisplayData *data = xl_display_data(display);
    int is_pending = data->queue_count > 0 || xcb_events_pending();
    if (is_pending == data->is_signalled) {
        return;
    }
    uint64_t counter = 1;
    if (is_pending) {
        sysv_eventfd_write(display->fd, 1);
    } else {
        sysv_eventfd_read(display->fd, &counter);
    }
    data->is_signalled = is_pending;
}

static void SYSV pump_display(void)
{
    xcb_events_pump();
    if (g_display != NULL) {
        xl_sync_descriptor(g_display);
    }
}

static XlId SYSV allocate_for_macro(XlDisplay *display)
{
    return xl_allocate_id(display);
}

static int32_t millimeters(int32_t pixels)
{
    return pixels * MILLIMETERS_PER_INCH_X10 / (ASSUMED_DPI * 10);
}

static void init_screen(XlDisplayData *data)
{
    const XcbScreen *root = &xcb_roots()->screens[0];
    data->visual = (XlVisual){.visual_id = root->root_visual, .visual_class = XL_TRUE_COLOR, .red_mask = 0xff0000,
                              .green_mask = 0x00ff00, .blue_mask = 0x0000ff, .bits_per_rgb = BITS_PER_RGB,
                              .map_entries = COLORMAP_ENTRIES};
    data->depth = (XlDepth){.depth = XL_ROOT_DEPTH, .visual_count = 1, .visuals = &data->visual};
    data->default_gc.values = (XlGcValues){.function = XL_GX_COPY, .plane_mask = XL_ALL_PLANES, .foreground = root->black_pixel,
                                          .background = root->white_pixel, .graphics_exposures = 1};
    data->screen = (XlScreen){.display = &data->pub, .root = root->root, .width = root->width_in_pixels,
                              .height = root->height_in_pixels, .width_mm = millimeters(root->width_in_pixels),
                              .height_mm = millimeters(root->height_in_pixels), .depth_count = 1,
                              .depths = &data->depth, .root_depth = XL_ROOT_DEPTH, .root_visual = &data->visual,
                              .default_gc = &data->default_gc, .colormap = root->default_colormap,
                              .white_pixel = root->white_pixel, .black_pixel = root->black_pixel, .max_maps = 1,
                              .min_maps = 1};
}

static SYSV void init_display(XlDisplayData *data)
{
    const XcbSetup *setup = &xcb_roots()->setup;
    init_screen(data);
    data->formats[0] = (XlScreenFormat){.depth = 1, .bits_per_pixel = 1, .scanline_pad = 32};
    data->formats[1] = (XlScreenFormat){.depth = XL_ROOT_DEPTH, .bits_per_pixel = 32, .scanline_pad = 32};
    data->formats[2] = (XlScreenFormat){.depth = 32, .bits_per_pixel = 32, .scanline_pad = 32};
    data->pub = (XlDisplay){.fd = sysv_eventfd(0, 0), .proto_major_version = PROTOCOL_MAJOR,
                            .vendor = (char *)VENDOR_NAME, .resource_alloc = allocate_for_macro, .bitmap_unit = 32,
                            .bitmap_pad = 32, .format_count = 3, .pixmap_formats = data->formats,
                            .release = VENDOR_RELEASE, .max_request_size = MAX_REQUEST_WORDS,
                            .display_name = (char *)DISPLAY_NAME, .screen_count = 1, .screens = &data->screen,
                            .motion_buffer = MOTION_BUFFER_SIZE, .min_keycode = setup->min_keycode,
                            .max_keycode = setup->max_keycode};
}

LIBC_FN XlDisplay *sysv_XOpenDisplay(const char *name)
{
    (void)name;
    XlDisplayData *data = (XlDisplayData *)sysv_malloc(sizeof(*data));
    if (data == NULL) {
        return NULL;
    }
    memset(data, 0, sizeof(*data));
    init_display(data);
    xcb_events_register_class();
    xcb_events_set_hooks(xl_paint_window, xl_size_window);
    g_display = &data->pub;
    shim_poll_set_message_source(data->pub.fd, pump_display);
    return &data->pub;
}

LIBC_FN int sysv_XCloseDisplay(XlDisplay *display)
{
    XlDisplayData *data = xl_display_data(display);
    if (g_display == display) {
        shim_poll_set_message_source(-1, NULL);
        g_display = NULL;
    }
    sysv_close(display->fd);
    sysv_free(data->queue);
    sysv_free(data);
    return 0;
}

LIBC_FN int sysv_XFlush(XlDisplay *display)
{
    (void)display;
    xl_present_all();
    return 1;
}

LIBC_FN int sysv_XSync(XlDisplay *display, int should_discard)
{
    xl_present_all();
    xl_events_fill(display, 0);
    if (should_discard) {
        XlEvent discarded;
        while (xl_events_take(display, NULL, NULL, &discarded, 1)) {
        }
    }
    return 1;
}

LIBC_FN void *sysv_XSynchronize(XlDisplay *display, int is_enabled)
{
    (void)display;
    (void)is_enabled;
    return NULL;
}

/* The handlers are stored and returned, never called: this display reports no protocol errors. */
static void *g_error_handler;
static void *g_io_error_handler;

LIBC_FN void *sysv_XSetErrorHandler(void *handler)
{
    void *previous = g_error_handler;
    g_error_handler = handler;
    return previous;
}

LIBC_FN void *sysv_XSetIOErrorHandler(void *handler)
{
    void *previous = g_io_error_handler;
    g_io_error_handler = handler;
    return previous;
}

LIBC_FN int sysv_XFree(void *block)
{
    sysv_free(block);
    return 1;
}

LIBC_FN int sysv_XInitThreads(void)
{
    return 1;
}

LIBC_FN void sysv_XLockDisplay(XlDisplay *display)
{
    (void)display;
}

LIBC_FN void sysv_XUnlockDisplay(XlDisplay *display)
{
    (void)display;
}

LIBC_FN XlId sysv_XInternAtom(XlDisplay *display, const char *name, int only_if_exists)
{
    (void)display;
    (void)only_if_exists;
    xl_next_request(display);
    return xcb_atom_for(name, strlen(name));
}

LIBC_FN int sysv_XInternAtoms(XlDisplay *display, char **names, int count, int only_if_exists, XlId *atoms)
{
    for (int i = 0; i < count; i++) {
        atoms[i] = sysv_XInternAtom(display, names[i], only_if_exists);
    }
    return 1;
}

LIBC_FN char *sysv_XGetAtomName(XlDisplay *display, XlId atom)
{
    xl_next_request(display);
    const char *name = xcb_atom_name((XcbId)atom);
    return name != NULL ? sysv_strdup(name) : NULL;
}

LIBC_FN char *sysv_XDisplayName(const char *name)
{
    return (char *)(name != NULL ? name : DISPLAY_NAME);
}

LIBC_FN char *sysv_XServerVendor(XlDisplay *display)
{
    return display->vendor;
}

LIBC_FN int sysv_XVendorRelease(XlDisplay *display)
{
    return display->release;
}

LIBC_FN int sysv_XProtocolVersion(XlDisplay *display)
{
    return display->proto_major_version;
}

LIBC_FN int sysv_XNoOp(XlDisplay *display)
{
    (void)display;
    return 1;
}

/* The function forms of the display macros, for programs that call them by name. */
LIBC_FN int sysv_XDefaultScreen(XlDisplay *display)
{
    return display->default_screen;
}

LIBC_FN XlId sysv_XDefaultRootWindow(XlDisplay *display)
{
    return display->screens[display->default_screen].root;
}

LIBC_FN XlId sysv_XRootWindow(XlDisplay *display, int screen)
{
    return display->screens[screen].root;
}

LIBC_FN XlVisual *sysv_XDefaultVisual(XlDisplay *display, int screen)
{
    return display->screens[screen].root_visual;
}

LIBC_FN int sysv_XDefaultDepth(XlDisplay *display, int screen)
{
    return display->screens[screen].root_depth;
}

LIBC_FN XlId sysv_XDefaultColormap(XlDisplay *display, int screen)
{
    return display->screens[screen].colormap;
}

LIBC_FN void *sysv_XDefaultGC(XlDisplay *display, int screen)
{
    return display->screens[screen].default_gc;
}

LIBC_FN int sysv_XDisplayWidth(XlDisplay *display, int screen)
{
    return display->screens[screen].width;
}

LIBC_FN int sysv_XDisplayHeight(XlDisplay *display, int screen)
{
    return display->screens[screen].height;
}

LIBC_FN int sysv_XDisplayWidthMM(XlDisplay *display, int screen)
{
    return display->screens[screen].width_mm;
}

LIBC_FN int sysv_XDisplayHeightMM(XlDisplay *display, int screen)
{
    return display->screens[screen].height_mm;
}

LIBC_FN uint64_t sysv_XBlackPixel(XlDisplay *display, int screen)
{
    return display->screens[screen].black_pixel;
}

LIBC_FN uint64_t sysv_XWhitePixel(XlDisplay *display, int screen)
{
    return display->screens[screen].white_pixel;
}

LIBC_FN int sysv_XScreenCount(XlDisplay *display)
{
    return display->screen_count;
}

LIBC_FN int sysv_XConnectionNumber(XlDisplay *display)
{
    return display->fd;
}

LIBC_FN int sysv_XQLength(XlDisplay *display)
{
    return display->queue_length;
}

LIBC_FN XlScreen *sysv_XScreenOfDisplay(XlDisplay *display, int screen)
{
    return &display->screens[screen];
}

LIBC_FN XlScreen *sysv_XDefaultScreenOfDisplay(XlDisplay *display)
{
    return &display->screens[display->default_screen];
}

LIBC_FN int sysv_XDisplayPlanes(XlDisplay *display, int screen)
{
    return display->screens[screen].root_depth;
}

LIBC_FN uint64_t sysv_XAllPlanes(void)
{
    return XL_ALL_PLANES;
}
