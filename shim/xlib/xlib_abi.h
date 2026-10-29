/*
 * File: shim/xlib/xlib_abi.h
 * Purpose: The Xlib structures and constants translated programs read directly (Display, Screen, Visual, events, XImage)
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Xlib hands programs pointers to its own memory, and Xlib's macros (DefaultScreen, DisplayWidth, XGetPixel,
 * ...) read the fields of these structures inline, so the layouts are part of the ABI. Linux x86-64 `long`
 * and pointers are 8 bytes; this compiler's `long` is 4, so every field is spelled with a fixed-size type.
 * tests/shim/xlib_layout_check.c asserts the same numbers against the real headers on Linux.
 */

#ifndef CROSSPORT_XLIB_ABI_H
#define CROSSPORT_XLIB_ABI_H

#include "libc_abi.h"

typedef uint64_t XlId;

typedef struct {
    void *ext_data;
    int32_t depth;
    int32_t bits_per_pixel;
    int32_t scanline_pad;
} XlScreenFormat;

/* XPixmapFormatValues: what XListPixmapFormats returns, one entry per depth. */
typedef struct {
    int32_t depth;
    int32_t bits_per_pixel;
    int32_t scanline_pad;
} XlPixmapFormat;

typedef struct {
    void *ext_data;
    XlId visual_id;
    int32_t visual_class;
    uint64_t red_mask;
    uint64_t green_mask;
    uint64_t blue_mask;
    int32_t bits_per_rgb;
    int32_t map_entries;
} XlVisual;

typedef struct {
    int32_t depth;
    int32_t visual_count;
    XlVisual *visuals;
} XlDepth;

struct XlDisplay;

typedef struct {
    void *ext_data;
    struct XlDisplay *display;
    XlId root;
    int32_t width;
    int32_t height;
    int32_t width_mm;
    int32_t height_mm;
    int32_t depth_count;
    XlDepth *depths;
    int32_t root_depth;
    XlVisual *root_visual;
    void *default_gc;
    XlId colormap;
    uint64_t white_pixel;
    uint64_t black_pixel;
    int32_t max_maps;
    int32_t min_maps;
    int32_t backing_store;
    int32_t save_unders;
    int64_t root_input_mask;
} XlScreen;

/* The part of Display programs may read (Xlib's XPrivDisplay, 296 bytes); the rest is Crossport's. */
typedef struct XlDisplay {
    void *ext_data;
    void *private1;
    int32_t fd;
    int32_t private2;
    int32_t proto_major_version;
    int32_t proto_minor_version;
    char *vendor;
    XlId private3;
    XlId private4;
    XlId private5;
    int32_t private6;
    XlId (SYSV *resource_alloc)(struct XlDisplay *);
    int32_t byte_order;
    int32_t bitmap_unit;
    int32_t bitmap_pad;
    int32_t bitmap_bit_order;
    int32_t format_count;
    XlScreenFormat *pixmap_formats;
    int32_t private8;
    int32_t release;
    void *private9;
    void *private10;
    int32_t queue_length;
    uint64_t last_request_read;
    uint64_t request;
    void *private11;
    void *private12;
    void *private13;
    void *private14;
    uint32_t max_request_size;
    void *database;
    int32_t (SYSV *private15)(struct XlDisplay *);
    char *display_name;
    int32_t default_screen;
    int32_t screen_count;
    XlScreen *screens;
    uint64_t motion_buffer;
    uint64_t private16;
    int32_t min_keycode;
    int32_t max_keycode;
    void *private17;
    void *private18;
    int32_t private19;
    char *xdefaults;
} XlDisplay;

/* XImage (136 bytes) with the function table XGetPixel, XPutPixel and XDestroyImage call through. */
typedef struct XlImage {
    int32_t width;
    int32_t height;
    int32_t xoffset;
    int32_t format;
    char *data;
    int32_t byte_order;
    int32_t bitmap_unit;
    int32_t bitmap_bit_order;
    int32_t bitmap_pad;
    int32_t depth;
    int32_t bytes_per_line;
    int32_t bits_per_pixel;
    uint64_t red_mask;
    uint64_t green_mask;
    uint64_t blue_mask;
    void *obdata;
    struct {
        struct XlImage *(SYSV *create_image)(XlDisplay *, XlVisual *, uint32_t, int32_t, int32_t, char *, uint32_t, uint32_t,
                                             int32_t, int32_t);
        int32_t (SYSV *destroy_image)(struct XlImage *);
        uint64_t (SYSV *get_pixel)(struct XlImage *, int32_t, int32_t);
        int32_t (SYSV *put_pixel)(struct XlImage *, int32_t, int32_t, uint64_t);
        struct XlImage *(SYSV *sub_image)(struct XlImage *, int32_t, int32_t, uint32_t, uint32_t);
        int32_t (SYSV *add_pixel)(struct XlImage *, int64_t);
    } f;
} XlImage;

/* The head every event shares. */
typedef struct {
    int32_t type;
    uint64_t serial;
    int32_t send_event;
    XlDisplay *display;
    XlId window;
} XlAnyEvent;

/* KeyPress, KeyRelease, ButtonPress, ButtonRelease and MotionNotify (where `detail` is the hint flag). */
typedef struct {
    int32_t type;
    uint64_t serial;
    int32_t send_event;
    XlDisplay *display;
    XlId window;
    XlId root;
    XlId subwindow;
    uint64_t time;
    int32_t x;
    int32_t y;
    int32_t x_root;
    int32_t y_root;
    uint32_t state;
    uint32_t detail;
    int32_t same_screen;
} XlInputEvent;

typedef struct {
    int32_t type;
    uint64_t serial;
    int32_t send_event;
    XlDisplay *display;
    XlId window;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t count;
} XlExposeEvent;

typedef struct {
    int32_t type;
    uint64_t serial;
    int32_t send_event;
    XlDisplay *display;
    XlId window;
    int32_t mode;
    int32_t detail;
} XlFocusEvent;

typedef struct {
    int32_t type;
    uint64_t serial;
    int32_t send_event;
    XlDisplay *display;
    XlId window;
    XlId atom;
    uint64_t time;
    int32_t state;
} XlPropertyEvent;

/* EnterNotify and LeaveNotify. */
typedef struct {
    int32_t type;
    uint64_t serial;
    int32_t send_event;
    XlDisplay *display;
    XlId window;
    XlId root;
    XlId subwindow;
    uint64_t time;
    int32_t x;
    int32_t y;
    int32_t x_root;
    int32_t y_root;
    int32_t mode;
    int32_t detail;
    int32_t same_screen;
    int32_t focus;
    uint32_t state;
} XlCrossingEvent;

/* MapNotify, UnmapNotify and DestroyNotify: `event` is the window that was selected on. */
typedef struct {
    int32_t type;
    uint64_t serial;
    int32_t send_event;
    XlDisplay *display;
    XlId event;
    XlId window;
    int32_t flag;
} XlStructureEvent;

typedef struct {
    int32_t type;
    uint64_t serial;
    int32_t send_event;
    XlDisplay *display;
    XlId event;
    XlId window;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t border_width;
    XlId above;
    int32_t override_redirect;
} XlConfigureEvent;

typedef struct {
    int32_t type;
    uint64_t serial;
    int32_t send_event;
    XlDisplay *display;
    XlId window;
    XlId message_type;
    int32_t format;
    int64_t data[5];
} XlClientMessageEvent;

/* XEvent is a union of 192 bytes. */
typedef union {
    int32_t type;
    XlAnyEvent any;
    XlInputEvent input;
    XlExposeEvent expose;
    XlFocusEvent focus;
    XlPropertyEvent property;
    XlCrossingEvent crossing;
    XlStructureEvent structure;
    XlConfigureEvent configure;
    XlClientMessageEvent client;
    int64_t pad[24];
} XlEvent;

/* XSetWindowAttributes (112 bytes) and XWindowAttributes (136 bytes). */
typedef struct {
    XlId background_pixmap;
    uint64_t background_pixel;
    XlId border_pixmap;
    uint64_t border_pixel;
    int32_t bit_gravity;
    int32_t win_gravity;
    int32_t backing_store;
    uint64_t backing_planes;
    uint64_t backing_pixel;
    int32_t save_under;
    int64_t event_mask;
    int64_t do_not_propagate_mask;
    int32_t override_redirect;
    XlId colormap;
    XlId cursor;
} XlSetAttributes;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t border_width;
    int32_t depth;
    XlVisual *visual;
    XlId root;
    int32_t window_class;
    int32_t bit_gravity;
    int32_t win_gravity;
    int32_t backing_store;
    uint64_t backing_planes;
    uint64_t backing_pixel;
    int32_t save_under;
    XlId colormap;
    int32_t map_installed;
    int32_t map_state;
    int64_t all_event_masks;
    int64_t your_event_mask;
    int64_t do_not_propagate_mask;
    int32_t override_redirect;
    XlScreen *screen;
} XlWindowAttributes;

/* XGCValues (128 bytes): only the fields the shim honours are named. */
typedef struct {
    int32_t function;
    uint64_t plane_mask;
    uint64_t foreground;
    uint64_t background;
    int32_t line_width;
    int32_t line_style;
    int32_t cap_style;
    int32_t join_style;
    int32_t fill_style;
    int32_t fill_rule;
    int32_t arc_mode;
    XlId tile;
    XlId stipple;
    int32_t ts_x_origin;
    int32_t ts_y_origin;
    XlId font;
    int32_t subwindow_mode;
    int32_t graphics_exposures;
    int32_t clip_x_origin;
    int32_t clip_y_origin;
    XlId clip_mask;
    int32_t dash_offset;
    char dashes;
} XlGcValues;

typedef struct {
    int16_t x;
    int16_t y;
} XlPoint;

typedef struct {
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
} XlSegment;

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} XlRectangle;

/* Core event numbers and the event masks that select them. */
enum {
    XL_KEY_PRESS = 2, XL_KEY_RELEASE = 3, XL_BUTTON_PRESS = 4, XL_BUTTON_RELEASE = 5, XL_MOTION_NOTIFY = 6,
    XL_ENTER_NOTIFY = 7, XL_LEAVE_NOTIFY = 8, XL_FOCUS_IN = 9, XL_FOCUS_OUT = 10, XL_EXPOSE = 12,
    XL_DESTROY_NOTIFY = 17, XL_UNMAP_NOTIFY = 18, XL_MAP_NOTIFY = 19, XL_CONFIGURE_NOTIFY = 22,
    XL_PROPERTY_NOTIFY = 28, XL_CLIENT_MESSAGE = 33
};
enum {
    XL_MASK_KEY_PRESS = 1 << 0, XL_MASK_KEY_RELEASE = 1 << 1, XL_MASK_BUTTON_PRESS = 1 << 2,
    XL_MASK_BUTTON_RELEASE = 1 << 3, XL_MASK_ENTER = 1 << 4, XL_MASK_LEAVE = 1 << 5,
    XL_MASK_POINTER_MOTION = 1 << 6, XL_MASK_BUTTON_MOTION = 1 << 13, XL_MASK_EXPOSURE = 1 << 15,
    XL_MASK_STRUCTURE = 1 << 17, XL_MASK_FOCUS = 1 << 21, XL_MASK_PROPERTY = 1 << 22
};

_Static_assert(sizeof(XlScreenFormat) == 24, "ScreenFormat is 24 bytes");
_Static_assert(sizeof(XlPixmapFormat) == 12, "XPixmapFormatValues is 12 bytes");
_Static_assert(sizeof(XlVisual) == 56, "Visual is 56 bytes");
_Static_assert(sizeof(XlDepth) == 16, "Depth is 16 bytes");
_Static_assert(sizeof(XlScreen) == 128, "Screen is 128 bytes");
_Static_assert(sizeof(XlDisplay) == 296, "the public Display is 296 bytes");
_Static_assert(offsetof(XlDisplay, default_screen) == 224, "default_screen is at 224");
_Static_assert(offsetof(XlDisplay, screens) == 232, "screens is at 232");
_Static_assert(sizeof(XlImage) == 136, "XImage is 136 bytes");
_Static_assert(offsetof(XlImage, f) == 88, "XImage function table is at 88");
_Static_assert(sizeof(XlAnyEvent) == 40, "XAnyEvent is 40 bytes");
_Static_assert(sizeof(XlInputEvent) == 96, "XKeyEvent is 96 bytes");
_Static_assert(sizeof(XlPropertyEvent) == 64, "XPropertyEvent is 64 bytes");
_Static_assert(sizeof(XlFocusEvent) == 48, "XFocusChangeEvent is 48 bytes");
_Static_assert(sizeof(XlCrossingEvent) == 104, "XCrossingEvent is 104 bytes");
_Static_assert(sizeof(XlExposeEvent) == 64, "XExposeEvent is 64 bytes");
_Static_assert(sizeof(XlStructureEvent) == 56, "XMapEvent is 56 bytes");
_Static_assert(sizeof(XlConfigureEvent) == 88, "XConfigureEvent is 88 bytes");
_Static_assert(sizeof(XlClientMessageEvent) == 96, "XClientMessageEvent is 96 bytes");
_Static_assert(sizeof(XlEvent) == 192, "XEvent is 192 bytes");
_Static_assert(sizeof(XlSetAttributes) == 112, "XSetWindowAttributes is 112 bytes");
_Static_assert(sizeof(XlWindowAttributes) == 136, "XWindowAttributes is 136 bytes");
_Static_assert(sizeof(XlGcValues) == 128, "XGCValues is 128 bytes");

#endif
