/*
 * File: tests/shim/xcb_layout_check.c
 * Purpose: Compile-time check, against libxcb's real headers, of the structure sizes shim/xcb/xcb_abi.h asserts
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The shim's own headers cannot include libxcb's (they target Linux types), so the numbers are repeated
 * here. A build of this file on Linux fails if the real layouts differ from what the shim assumes.
 */

#include <stddef.h>
#include <xcb/xcb.h>

_Static_assert(sizeof(xcb_setup_t) == 40, "xcb_setup_t is 40 bytes");
_Static_assert(sizeof(xcb_screen_t) == 40, "xcb_screen_t is 40 bytes");
_Static_assert(sizeof(xcb_screen_iterator_t) == 16, "xcb_screen_iterator_t is 16 bytes");
_Static_assert(sizeof(xcb_key_press_event_t) == 32, "input events are 32 bytes");
_Static_assert(sizeof(xcb_client_message_event_t) == 32, "client messages are 32 bytes");
_Static_assert(sizeof(xcb_expose_event_t) == 20, "expose events are 20 bytes");
_Static_assert(sizeof(xcb_configure_notify_event_t) == 28, "configure events are 28 bytes");
_Static_assert(sizeof(xcb_intern_atom_reply_t) == 12, "intern atom replies are 12 bytes");
_Static_assert(sizeof(xcb_get_geometry_reply_t) == 24, "geometry replies are 24 bytes");
_Static_assert(offsetof(xcb_client_message_event_t, data) == 12, "client message data starts at byte 12");

int main(void)
{
    return 0;
}
