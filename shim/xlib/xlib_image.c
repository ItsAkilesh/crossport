/*
 * File: shim/xlib/xlib_image.c
 * Purpose: XImage support: XCreateImage, XPutImage, XGetImage and the pixel accessors reached through the XImage function table
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * Supported formats: ZPixmap with 32 or 24 bits per pixel (the layout of the window buffers) and XYBitmap
 * with one bit per pixel. Pixels are little-endian BGRX, which is what the visual's masks describe.
 */

#include <string.h>

#include "xlib_internal.h"

#define FORMAT_XY_BITMAP 0
#define FORMAT_Z_PIXMAP 2
#define LSB_FIRST 0
#define BITS_PER_BYTE 8
#define BITS_PER_PIXEL_32 32
#define BITS_PER_PIXEL_24 24

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);

static SYSV uint64_t get_pixel(XlImage *image, int32_t x, int32_t y);
static SYSV int32_t put_pixel(XlImage *image, int32_t x, int32_t y, uint64_t pixel);
static SYSV int32_t destroy_image(XlImage *image);
static SYSV XlImage *sub_image(XlImage *image, int32_t x, int32_t y, uint32_t width, uint32_t height);
static SYSV int32_t add_pixel(XlImage *image, int64_t value);

static void install_functions(XlImage *image)
{
    image->f.destroy_image = destroy_image;
    image->f.get_pixel = get_pixel;
    image->f.put_pixel = put_pixel;
    image->f.sub_image = sub_image;
    image->f.add_pixel = add_pixel;
}

static int32_t bits_for_depth(int32_t depth)
{
    return depth == 1 ? 1 : BITS_PER_PIXEL_32;
}

static void describe(XlImage *image, int32_t depth, int32_t format, int32_t width, int32_t height, int32_t pad)
{
    image->width = width;
    image->height = height;
    image->format = format;
    image->depth = depth;
    image->byte_order = LSB_FIRST;
    image->bitmap_unit = BITS_PER_PIXEL_32;
    image->bitmap_bit_order = LSB_FIRST;
    image->bitmap_pad = pad;
    image->bits_per_pixel = format == FORMAT_Z_PIXMAP ? bits_for_depth(depth) : 1;
    image->red_mask = depth == 1 ? 0 : 0xff0000;
    image->green_mask = depth == 1 ? 0 : 0x00ff00;
    image->blue_mask = depth == 1 ? 0 : 0x0000ff;
    install_functions(image);
}

static int32_t default_stride(const XlImage *image)
{
    int32_t pad = image->bitmap_pad > 0 ? image->bitmap_pad : BITS_PER_PIXEL_32;
    int32_t bits = image->width * image->bits_per_pixel;
    return (bits + pad - 1) / pad * pad / BITS_PER_BYTE;
}

LIBC_FN XlImage *sysv_XCreateImage(XlDisplay *display, XlVisual *visual, unsigned depth, int format, int offset, char *data,
                                   unsigned width, unsigned height, int pad, int bytes_per_line)
{
    (void)display;
    (void)visual;
    XlImage *image = (XlImage *)sysv_malloc(sizeof(*image));
    if (image == NULL) {
        return NULL;
    }
    memset(image, 0, sizeof(*image));
    describe(image, (int32_t)depth, format, (int32_t)width, (int32_t)height, pad);
    image->xoffset = offset;
    image->data = data;
    image->bytes_per_line = bytes_per_line > 0 ? bytes_per_line : default_stride(image);
    return image;
}

static SYSV XlImage *allocate_image(int32_t depth, int32_t width, int32_t height)
{
    XlImage *image = (XlImage *)sysv_malloc(sizeof(*image));
    if (image == NULL) {
        return NULL;
    }
    memset(image, 0, sizeof(*image));
    describe(image, depth, FORMAT_Z_PIXMAP, width, height, BITS_PER_PIXEL_32);
    image->bytes_per_line = default_stride(image);
    image->data = (char *)sysv_malloc((size_t)image->bytes_per_line * (size_t)(height > 0 ? height : 1));
    if (image->data == NULL) {
        sysv_free(image);
        return NULL;
    }
    memset(image->data, 0, (size_t)image->bytes_per_line * (size_t)(height > 0 ? height : 1));
    return image;
}

static SYSV uint64_t get_pixel(XlImage *image, int32_t x, int32_t y)
{
    const uint8_t *row = (const uint8_t *)image->data + (size_t)y * (size_t)image->bytes_per_line;
    int32_t column = x + image->xoffset;
    if (image->bits_per_pixel == 1) {
        return (row[column / BITS_PER_BYTE] >> (column % BITS_PER_BYTE)) & 1u;
    }
    if (image->bits_per_pixel == BITS_PER_PIXEL_24) {
        const uint8_t *pixel = row + (size_t)column * 3;
        return (uint64_t)pixel[0] | ((uint64_t)pixel[1] << 8) | ((uint64_t)pixel[2] << 16);
    }
    uint32_t pixel;
    memcpy(&pixel, row + (size_t)column * sizeof(pixel), sizeof(pixel));
    return pixel;
}

static SYSV int32_t put_pixel(XlImage *image, int32_t x, int32_t y, uint64_t pixel)
{
    uint8_t *row = (uint8_t *)image->data + (size_t)y * (size_t)image->bytes_per_line;
    int32_t column = x + image->xoffset;
    if (image->bits_per_pixel == 1) {
        uint8_t bit = (uint8_t)(1u << (column % BITS_PER_BYTE));
        row[column / BITS_PER_BYTE] = (pixel & 1u) ? (uint8_t)(row[column / BITS_PER_BYTE] | bit) : (uint8_t)(row[column / BITS_PER_BYTE] & ~bit);
    } else if (image->bits_per_pixel == BITS_PER_PIXEL_24) {
        row[(size_t)column * 3] = (uint8_t)pixel;
        row[(size_t)column * 3 + 1] = (uint8_t)(pixel >> 8);
        row[(size_t)column * 3 + 2] = (uint8_t)(pixel >> 16);
    } else {
        uint32_t narrow = (uint32_t)pixel;
        memcpy(row + (size_t)column * sizeof(narrow), &narrow, sizeof(narrow));
    }
    return 1;
}

static SYSV int32_t destroy_image(XlImage *image)
{
    sysv_free(image->data);
    sysv_free(image);
    return 1;
}

static SYSV XlImage *sub_image(XlImage *image, int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    XlImage *copy = allocate_image(image->depth, (int32_t)width, (int32_t)height);
    for (int32_t row = 0; copy != NULL && row < (int32_t)height; row++) {
        for (int32_t column = 0; column < (int32_t)width; column++) {
            put_pixel(copy, column, row, get_pixel(image, x + column, y + row));
        }
    }
    return copy;
}

static SYSV int32_t add_pixel(XlImage *image, int64_t value)
{
    for (int32_t row = 0; row < image->height; row++) {
        for (int32_t column = 0; column < image->width; column++) {
            put_pixel(image, column, row, get_pixel(image, column, row) + (uint64_t)value);
        }
    }
    return 1;
}

LIBC_FN uint64_t sysv__XGetPixel(XlImage *image, int x, int y)
{
    return get_pixel(image, x, y);
}

LIBC_FN int sysv__XPutPixel(XlImage *image, int x, int y, uint64_t pixel)
{
    return put_pixel(image, x, y, pixel);
}

LIBC_FN int sysv__XDestroyImage(XlImage *image)
{
    return destroy_image(image);
}

LIBC_FN XlImage *sysv__XSubImage(XlImage *image, int x, int y, unsigned width, unsigned height)
{
    return sub_image(image, x, y, width, height);
}

LIBC_FN int sysv__XAddPixel(XlImage *image, int64_t value)
{
    return add_pixel(image, value);
}

/* The pixel to store for source pixel `value` of an image: bitmaps choose the GC's foreground or background. */
static SYSV uint32_t source_pixel(const XlImage *image, const XlGc *gc, int32_t x, int32_t y)
{
    uint64_t value = get_pixel((XlImage *)image, x, y);
    if (image->format == FORMAT_XY_BITMAP || image->bits_per_pixel == 1) {
        return xl_pixel(value != 0 ? gc->values.foreground : gc->values.background);
    }
    return xl_pixel(value);
}

LIBC_FN int sysv_XPutImage(XlDisplay *display, XlId id, const XlGc *gc, const XlImage *image, int sx, int sy, int dx, int dy,
                           unsigned width, unsigned height)
{
    xl_next_request(display);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(id);
    int is_window = drawable != NULL && drawable->is_window;
    for (int32_t row = 0; drawable != NULL && row < (int32_t)height; row++) {
        for (int32_t column = 0; column < (int32_t)width; column++) {
            int32_t x = dx + column;
            int32_t y = dy + row;
            int is_inside = x >= 0 && y >= 0 && x < drawable->width && y < drawable->height;
            int is_source = sx + column < image->width && sy + row < image->height;
            if (is_inside && is_source) {
                uint32_t pixel = source_pixel(image, gc, sx + column, sy + row);
                uint32_t *slot = &drawable->pixels[(size_t)y * (size_t)drawable->width + (size_t)x];
                *slot = xl_combine(pixel, *slot, gc->values.function);
            }
        }
    }
    xl_unlock();
    if (is_window) {
        xl_invalidate(id, dx, dy, (int32_t)width, (int32_t)height);
    }
    return 1;
}

LIBC_FN XlImage *sysv_XGetImage(XlDisplay *display, XlId id, int x, int y, unsigned width, unsigned height, uint64_t plane_mask,
                                int format)
{
    (void)plane_mask;
    (void)format;
    xl_next_request(display);
    xl_lock();
    XlDrawable *drawable = xl_drawable_find(id);
    XlImage *image = drawable != NULL ? allocate_image(drawable->depth, (int32_t)width, (int32_t)height) : NULL;
    for (int32_t row = 0; image != NULL && row < (int32_t)height; row++) {
        for (int32_t column = 0; column < (int32_t)width; column++) {
            int32_t source_x = x + column;
            int32_t source_y = y + row;
            int is_inside = source_x >= 0 && source_y >= 0 && source_x < drawable->width && source_y < drawable->height;
            put_pixel(image, column, row, is_inside ? drawable->pixels[(size_t)source_y * (size_t)drawable->width + (size_t)source_x] : 0);
        }
    }
    xl_unlock();
    return image;
}
