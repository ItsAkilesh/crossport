/*
 * File: shim/xlib/xlib_polygon.c
 * Purpose: Filled polygons (XFillPolygon) and arcs (XDrawArc, XFillArc and their array forms)
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * Polygons follow the X rule exactly: a pixel is inside when its centre is inside, and a centre exactly on an
 * edge counts only for a left edge (the interior lies to its right). Scan conversion is in integers, so the
 * result does not depend on rounding. Arcs are approximated by a polygon or a polyline through points on the
 * ellipse, which differs from a real server by about a pixel along the curve; the fill and the end points are
 * right, and the shape is what a program draws.
 */

#include <math.h>
#include <string.h>

#include "xlib_internal.h"

#define COORD_MODE_PREVIOUS 1
#define WINDING_RULE 1
#define ARC_CHORD 0
#define ANGLE_UNITS_PER_DEGREE 64
#define FULL_CIRCLE_UNITS (360 * ANGLE_UNITS_PER_DEGREE)
#define MIN_ARC_STEPS 8
#define MAX_ARC_STEPS 4096
#define PI 3.14159265358979323846
#define DEGREES_PER_RADIAN (180.0 / PI)

LIBC_FN void *sysv_malloc(size_t size);
LIBC_FN void sysv_free(void *block);

typedef struct {
    int32_t x;
    int32_t y;
} Vertex;

typedef struct {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    int32_t direction;
} Edge;

typedef struct {
    int32_t pixel;
    int32_t direction;
} Crossing;

/* ceil(numerator / denominator) for a positive denominator. */
static int32_t ceil_divide(int64_t numerator, int64_t denominator)
{
    int64_t quotient = numerator / denominator;
    return (int32_t)((numerator % denominator != 0 && numerator > 0) ? quotient + 1 : quotient);
}

/* Non-horizontal edges, each oriented downwards, with the direction it was drawn in. Returns how many. */
static size_t collect_edges(const Vertex *vertices, size_t count, Edge *edges)
{
    size_t kept = 0;
    for (size_t i = 0; i < count; i++) {
        Vertex from = vertices[i];
        Vertex to = vertices[(i + 1) % count];
        if (from.y != to.y) {
            int is_downward = to.y > from.y;
            edges[kept++] = is_downward ? (Edge){from.x, from.y, to.x, to.y, 1} : (Edge){to.x, to.y, from.x, from.y, -1};
        }
    }
    return kept;
}

/* The first pixel whose centre is at or right of where the edge crosses the centre line of row y. */
static int32_t crossing_pixel(const Edge *edge, int32_t y)
{
    int64_t dy = (int64_t)edge->y1 - edge->y0;
    int64_t dx = (int64_t)edge->x1 - edge->x0;
    int64_t numerator = 2 * (int64_t)edge->x0 * dy + (2 * (int64_t)y + 1 - 2 * (int64_t)edge->y0) * dx - dy;
    return ceil_divide(numerator, 2 * dy);
}

static size_t crossings_of_row(const Edge *edges, size_t count, int32_t y, Crossing *out)
{
    size_t found = 0;
    for (size_t i = 0; i < count; i++) {
        if (edges[i].y0 <= y && y < edges[i].y1) {
            Crossing crossing = {crossing_pixel(&edges[i], y), edges[i].direction};
            size_t slot = found++;
            for (; slot > 0 && out[slot - 1].pixel > crossing.pixel; slot--) {
                out[slot] = out[slot - 1];
            }
            out[slot] = crossing;
        }
    }
    return found;
}

/* The spans of a row as [left, right) pairs: pairs of crossings for even-odd, runs of non-zero winding otherwise. */
static size_t spans_of_row(const Crossing *crossings, size_t count, int is_winding, int32_t *bounds)
{
    size_t spans = 0;
    int32_t winding = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t before = winding;
        winding = is_winding ? winding + crossings[i].direction : (winding == 0 ? 1 : 0);
        if (before == 0 && winding != 0) {
            bounds[2 * spans] = crossings[i].pixel;
        } else if (before != 0 && winding == 0) {
            bounds[2 * spans + 1] = crossings[i].pixel;
            spans++;
        }
    }
    return spans;
}

static void row_range(const Edge *edges, size_t count, int32_t height, int32_t *first, int32_t *last)
{
    int32_t top = edges[0].y0;
    int32_t bottom = edges[0].y1;
    for (size_t i = 1; i < count; i++) {
        top = edges[i].y0 < top ? edges[i].y0 : top;
        bottom = edges[i].y1 > bottom ? edges[i].y1 : bottom;
    }
    *first = top < 0 ? 0 : top;
    *last = bottom > height ? height : bottom;
}

static SYSV void fill_vertices(XlId drawable, const XlGc *gc, const Vertex *vertices, size_t count)
{
    int32_t width = 0;
    int32_t height = 0;
    if (count < 3 || !xl_drawable_extent(drawable, &width, &height)) {
        return;
    }
    Edge *edges = (Edge *)sysv_malloc(count * sizeof(Edge));
    Crossing *crossings = (Crossing *)sysv_malloc(count * sizeof(Crossing));
    int32_t *bounds = (int32_t *)sysv_malloc(count * sizeof(int32_t));
    size_t edge_count = (edges != NULL && crossings != NULL && bounds != NULL) ? collect_edges(vertices, count, edges) : 0;
    int32_t first = 0;
    int32_t last = 0;
    if (edge_count > 0) {
        row_range(edges, edge_count, height, &first, &last);
    }
    for (int32_t y = first; y < last; y++) {
        size_t found = crossings_of_row(edges, edge_count, y, crossings);
        size_t spans = spans_of_row(crossings, found, gc->values.fill_rule == WINDING_RULE, bounds);
        xl_fill_spans(drawable, gc, y, bounds, spans);
    }
    sysv_free(edges);
    sysv_free(crossings);
    sysv_free(bounds);
}

LIBC_FN int sysv_XFillPolygon(XlDisplay *display, XlId drawable, const XlGc *gc, const XlPoint *points, int count, int shape,
                              int mode)
{
    (void)shape;
    xl_next_request(display);
    Vertex *vertices = count > 2 ? (Vertex *)sysv_malloc((size_t)count * sizeof(Vertex)) : NULL;
    for (int i = 0; vertices != NULL && i < count; i++) {
        int32_t previous_x = i > 0 ? vertices[i - 1].x : 0;
        int32_t previous_y = i > 0 ? vertices[i - 1].y : 0;
        vertices[i].x = mode == COORD_MODE_PREVIOUS && i > 0 ? previous_x + points[i].x : points[i].x;
        vertices[i].y = mode == COORD_MODE_PREVIOUS && i > 0 ? previous_y + points[i].y : points[i].y;
    }
    if (vertices != NULL) {
        fill_vertices(drawable, gc, vertices, (size_t)count);
        sysv_free(vertices);
    }
    return 1;
}

static int32_t round_to_pixel(double value)
{
    return (int32_t)floor(value + 0.5);
}

/* Points along the arc, starting at angle1 and sweeping angle2 (in 64ths of a degree, counter-clockwise from 3 o'clock). */
static size_t arc_points(const XlArc *arc, Vertex *out)
{
    double radius_x = arc->width / 2.0;
    double radius_y = arc->height / 2.0;
    double centre_x = arc->x + radius_x;
    double centre_y = arc->y + radius_y;
    int32_t sweep_units = arc->angle2 > FULL_CIRCLE_UNITS ? FULL_CIRCLE_UNITS : arc->angle2 < -FULL_CIRCLE_UNITS ? -FULL_CIRCLE_UNITS : arc->angle2;
    double start = (double)arc->angle1 / ANGLE_UNITS_PER_DEGREE / DEGREES_PER_RADIAN;
    double sweep = (double)sweep_units / ANGLE_UNITS_PER_DEGREE / DEGREES_PER_RADIAN;
    double longest = radius_x > radius_y ? radius_x : radius_y;
    int steps = (int)ceil(fabs(sweep) * longest);
    steps = steps < MIN_ARC_STEPS ? MIN_ARC_STEPS : steps > MAX_ARC_STEPS ? MAX_ARC_STEPS : steps;
    for (int i = 0; i <= steps; i++) {
        double angle = start + sweep * i / steps;
        out[i].x = round_to_pixel(centre_x + radius_x * cos(angle));
        out[i].y = round_to_pixel(centre_y - radius_y * sin(angle));
    }
    return (size_t)steps + 1;
}

static SYSV void draw_one_arc(XlId drawable, const XlGc *gc, const XlArc *arc)
{
    Vertex *points = arc->width > 0 && arc->height > 0 ? (Vertex *)sysv_malloc((MAX_ARC_STEPS + 2) * sizeof(Vertex)) : NULL;
    if (points == NULL) {
        return;
    }
    size_t count = arc_points(arc, points);
    for (size_t i = 1; i < count; i++) {
        xl_draw_line(drawable, gc, points[i - 1].x, points[i - 1].y, points[i].x, points[i].y);
    }
    sysv_free(points);
}

static SYSV void fill_one_arc(XlId drawable, const XlGc *gc, const XlArc *arc)
{
    Vertex *points = arc->width > 0 && arc->height > 0 ? (Vertex *)sysv_malloc((MAX_ARC_STEPS + 2) * sizeof(Vertex)) : NULL;
    if (points == NULL) {
        return;
    }
    size_t count = arc_points(arc, points);
    int is_partial = arc->angle2 > -FULL_CIRCLE_UNITS && arc->angle2 < FULL_CIRCLE_UNITS;
    if (is_partial && gc->values.arc_mode != ARC_CHORD) {
        points[count].x = round_to_pixel(arc->x + arc->width / 2.0);
        points[count].y = round_to_pixel(arc->y + arc->height / 2.0);
        count++;
    }
    fill_vertices(drawable, gc, points, count);
    sysv_free(points);
}

LIBC_FN int sysv_XDrawArc(XlDisplay *display, XlId drawable, const XlGc *gc, int x, int y, unsigned width, unsigned height,
                          int angle1, int angle2)
{
    xl_next_request(display);
    XlArc arc = {(int16_t)x, (int16_t)y, (uint16_t)width, (uint16_t)height, (int16_t)angle1, (int16_t)angle2};
    draw_one_arc(drawable, gc, &arc);
    return 1;
}

LIBC_FN int sysv_XDrawArcs(XlDisplay *display, XlId drawable, const XlGc *gc, const XlArc *arcs, int count)
{
    xl_next_request(display);
    for (int i = 0; i < count; i++) {
        draw_one_arc(drawable, gc, &arcs[i]);
    }
    return 1;
}

LIBC_FN int sysv_XFillArc(XlDisplay *display, XlId drawable, const XlGc *gc, int x, int y, unsigned width, unsigned height,
                          int angle1, int angle2)
{
    xl_next_request(display);
    XlArc arc = {(int16_t)x, (int16_t)y, (uint16_t)width, (uint16_t)height, (int16_t)angle1, (int16_t)angle2};
    fill_one_arc(drawable, gc, &arc);
    return 1;
}

LIBC_FN int sysv_XFillArcs(XlDisplay *display, XlId drawable, const XlGc *gc, const XlArc *arcs, int count)
{
    xl_next_request(display);
    for (int i = 0; i < count; i++) {
        fill_one_arc(drawable, gc, &arcs[i]);
    }
    return 1;
}
