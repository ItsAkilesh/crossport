/*
 * File: shim/xlib/xlib_xcb_core.c
 * Purpose: xcb plumbing a toolkit needs beyond the requests: replies, request checks, extensions and the setup's depths, visuals and formats
 * Author: Akilesh
 * Date: 2026-11-05
 *
 * Every request here is carried out when it is made, so its reply already exists when the program asks for it.
 * The reply is kept in a small table under the request's sequence number until the program collects it with a
 * *_reply call, collects it with xcb_wait_for_reply, or throws it away with xcb_discard_reply. Extensions are all
 * reported absent: a toolkit asks first and does without, which is what a server with none installed makes it do.
 */

#include <string.h>

#include "xlib_internal.h"

#define REPLY_SLOTS 64
#define MAX_REQUEST_LENGTH 65535u
#define STR_HEADER_BYTES 1

typedef struct {
    uint32_t sequence;
    void *reply;
} ReplySlot;

static ReplySlot g_replies[REPLY_SLOTS];
static size_t g_next_slot;
static SRWLOCK g_lock = SRWLOCK_INIT;

LIBC_FN void sysv_free(void *block);

/* Keeps a reply (which this takes over) for the request with this sequence number, and returns the cookie. */
XcbCookie xcb_reply_keep(void *reply)
{
    uint32_t sequence = xcb_next_sequence();
    if (reply != NULL) {
        ((XcbReplyHeader *)reply)->sequence = (uint16_t)sequence;
    }
    AcquireSRWLockExclusive(&g_lock);
    ReplySlot *slot = &g_replies[g_next_slot++ % REPLY_SLOTS];
    void *forgotten = slot->reply;
    *slot = (ReplySlot){.sequence = sequence, .reply = reply};
    ReleaseSRWLockExclusive(&g_lock);
    sysv_free(forgotten);
    return (XcbCookie){.sequence = sequence};
}

/* The reply kept for a cookie, now the caller's to free; NULL when there is none. */
void *xcb_reply_take(XcbCookie cookie, void **error)
{
    void *reply = NULL;
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < REPLY_SLOTS && reply == NULL; i++) {
        if (g_replies[i].sequence == cookie.sequence && g_replies[i].reply != NULL) {
            reply = g_replies[i].reply;
            g_replies[i] = (ReplySlot){0};
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (error != NULL) {
        *error = NULL;
    }
    return reply;
}

/* A reply with `extra` bytes after its fixed part, its length field set in 4-byte units. */
void *xcb_reply_allocate(size_t fixed_size, size_t extra)
{
    uint8_t *reply = (uint8_t *)sysv_malloc(fixed_size + extra + 1);
    if (reply != NULL) {
        memset(reply, 0, fixed_size + extra + 1);
        reply[0] = 1;
        ((XcbReplyHeader *)reply)->length = (uint32_t)((extra + 3) / 4);
    }
    return reply;
}

LIBC_FN void *sysv_xcb_wait_for_reply(void *connection, uint32_t request, void **error)
{
    (void)connection;
    return xcb_reply_take((XcbCookie){.sequence = request}, error);
}

LIBC_FN void sysv_xcb_discard_reply(void *connection, uint32_t sequence)
{
    (void)connection;
    sysv_free(xcb_reply_take((XcbCookie){.sequence = sequence}, NULL));
}

/* Requests are carried out when made, so a checked request has already succeeded or failed silently. */
LIBC_FN void *sysv_xcb_request_check(void *connection, XcbCookie cookie)
{
    (void)connection;
    (void)cookie;
    return NULL;
}

LIBC_FN XcbEvent *sysv_xcb_poll_for_queued_event(void *connection)
{
    return sysv_xcb_poll_for_event(connection);
}

/* The generic request path is what extension libraries use; no extension is present, so none gets here. */
LIBC_FN uint32_t sysv_xcb_send_request(void *connection, int flags, void *vector, const void *request)
{
    (void)connection;
    (void)flags;
    (void)vector;
    (void)request;
    return 0;
}

LIBC_FN uint32_t sysv_xcb_send_request_with_fds(void *connection, int flags, void *vector, const void *request,
                                                unsigned count, int *fds)
{
    (void)count;
    (void)fds;
    return sysv_xcb_send_request(connection, flags, vector, request);
}

LIBC_FN int *sysv_xcb_get_reply_fds(void *connection, void *reply, size_t length)
{
    (void)connection;
    (void)reply;
    (void)length;
    return NULL;
}

LIBC_FN XcbCookie sysv_xcb_no_operation(void *connection)
{
    (void)connection;
    return (XcbCookie){.sequence = xcb_next_sequence()};
}

LIBC_FN XcbCookie sysv_xcb_no_operation_checked(void *connection)
{
    return sysv_xcb_no_operation(connection);
}

LIBC_FN uint32_t sysv_xcb_get_maximum_request_length(void *connection)
{
    (void)connection;
    return MAX_REQUEST_LENGTH;
}

LIBC_FN int sysv_xcb_popcount(uint32_t mask)
{
    int count = 0;
    for (; mask != 0; mask &= mask - 1) {
        count++;
    }
    return count;
}

/* xcb_str_t: a length byte, then that many characters. */
LIBC_FN int sysv_xcb_str_sizeof(const void *text)
{
    return STR_HEADER_BYTES + *(const uint8_t *)text;
}

/* Every extension is absent; the reply is shared and read-only, as libxcb's is. */
LIBC_FN const XcbExtensionReply *sysv_xcb_get_extension_data(void *connection, void *extension)
{
    static const XcbExtensionReply absent = {.response_type = 1};
    (void)connection;
    (void)extension;
    return &absent;
}

LIBC_FN void sysv_xcb_prefetch_extension_data(void *connection, void *extension)
{
    (void)connection;
    (void)extension;
}

LIBC_FN XcbDepthIterator sysv_xcb_screen_allowed_depths_iterator(const XcbScreen *screen)
{
    return (XcbDepthIterator){.data = (XcbDepth *)(screen + 1), .rem = screen->allowed_depths_len, .index = 0};
}

LIBC_FN XcbVisualTypeIterator sysv_xcb_depth_visuals_iterator(const XcbDepth *depth)
{
    return (XcbVisualTypeIterator){.data = (XcbVisualType *)(depth + 1), .rem = depth->visuals_len, .index = 0};
}

LIBC_FN void sysv_xcb_depth_next(XcbDepthIterator *iterator)
{
    XcbDepth *current = iterator->data;
    iterator->rem--;
    iterator->index++;
    iterator->data = (XcbDepth *)((XcbVisualType *)(current + 1) + current->visuals_len);
}

LIBC_FN void sysv_xcb_visualtype_next(XcbVisualTypeIterator *iterator)
{
    iterator->rem--;
    iterator->index++;
    iterator->data++;
}

LIBC_FN int sysv_xcb_setup_roots_length(const XcbSetup *setup)
{
    return setup->roots_len;
}

LIBC_FN XcbFormat *sysv_xcb_setup_pixmap_formats(const XcbSetup *setup)
{
    (void)setup;
    return xcb_roots()->formats;
}

LIBC_FN int sysv_xcb_setup_pixmap_formats_length(const XcbSetup *setup)
{
    return setup->pixmap_formats_len;
}

LIBC_FN XcbFormatIterator sysv_xcb_setup_pixmap_formats_iterator(const XcbSetup *setup)
{
    return (XcbFormatIterator){.data = xcb_roots()->formats, .rem = setup->pixmap_formats_len, .index = 0};
}

LIBC_FN void sysv_xcb_format_next(XcbFormatIterator *iterator)
{
    iterator->rem--;
    iterator->index++;
    iterator->data++;
}
