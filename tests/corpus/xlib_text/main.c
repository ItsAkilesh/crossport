/*
 * File: tests/corpus/xlib_text/main.c
 * Purpose: Xlib client that converts string lists to text properties and stores them as window properties
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * Differential test: XmbTextListToTextProperty, Xutf8TextListToTextProperty, XStringListToTextProperty and
 * XSetTextProperty give the same type, format, length and bytes on a real server (Xvfb) and in the shim. A real
 * SDL2 program set its window title through these calls, and crashed when they did nothing.
 */

#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#define PROPERTY_LONGS 64

static void print_property(Display *display, const char *label, const XTextProperty *text)
{
    char *encoding = text->encoding == None ? NULL : XGetAtomName(display, text->encoding);
    printf("%s: encoding=%s format=%d items=%lu bytes=", label, encoding != NULL ? encoding : "None", text->format,
           text->nitems);
    for (unsigned long i = 0; i < text->nitems; i++) {
        putchar(text->value[i] == '\0' ? '|' : text->value[i]);
    }
    putchar('\n');
    if (encoding != NULL) {
        XFree(encoding);
    }
}

static void store_and_read_back(Display *display, Window window, const XTextProperty *text)
{
    XSetTextProperty(display, window, (XTextProperty *)text, XA_WM_NAME);
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char *data = NULL;
    XGetWindowProperty(display, window, XA_WM_NAME, 0, PROPERTY_LONGS, False, AnyPropertyType, &type, &format, &count,
                       &remaining, &data);
    char *type_name = type == None ? NULL : XGetAtomName(display, type);
    printf("stored: type=%s format=%d count=%lu\n", type_name != NULL ? type_name : "None", format, count);
    if (type_name != NULL) {
        XFree(type_name);
    }
    XFree(data);
}

static void convert(Display *display, Window window, const char *label, int utf8, XICCEncodingStyle style)
{
    char first[] = "hello";
    char second[] = "world";
    char *list[] = {first, second};
    XTextProperty text = {0};
    int status = utf8 ? Xutf8TextListToTextProperty(display, list, 2, style, &text)
                      : XmbTextListToTextProperty(display, list, 2, style, &text);
    printf("%s: status=%d\n", label, status);
    if (status == Success) {
        print_property(display, label, &text);
        store_and_read_back(display, window, &text);
        XFree(text.value);
    }
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        puts("no display");
        return 1;
    }
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 10, 10, 0, 0, 0);
    convert(display, window, "mb-std-icc", 0, XStdICCTextStyle);
    convert(display, window, "mb-string", 0, XStringStyle);
    convert(display, window, "mb-utf8", 0, XUTF8StringStyle);
    convert(display, window, "utf8-utf8", 1, XUTF8StringStyle);
    char only[] = "title";
    char *one[] = {only};
    XTextProperty text = {0};
    printf("string-list: status=%d\n", XStringListToTextProperty(one, 1, &text) != 0 ? 1 : 0);
    print_property(display, "string-list", &text);
    XFree(text.value);
    XCloseDisplay(display);
    puts("done");
    return 0;
}
