#include "desperateOverview_ui_drawing.h"

#include <math.h>

void cairo_add_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double radius) {
    double r = radius;
    if (r <= 0.0 || w <= 0.0 || h <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    double max_r = fmin(w, h) / 2.0;
    if (r > max_r)
        r = max_r;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
}

void cairo_set_source_rgba_color(cairo_t *cr, const GdkRGBA *color) {
    cairo_set_source_rgba(cr, color->red, color->green, color->blue, color->alpha);
}

void ui_draw_window_border(cairo_t *cr, double rx, double ry, double rw, double rh, const OverlayConfig *cfg, double border_width) {
    cairo_save(cr);
    cairo_add_rounded_rect(cr, rx + 0.5, ry + 0.5, rw - 1.0, rh - 1.0, cfg->window_corner_radius);
    cairo_set_source_rgba_color(cr, &cfg->window_border);
    cairo_set_line_width(cr, border_width);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void ui_draw_window_placeholder(cairo_t *cr, double rx, double ry, double rw, double rh, const OverlayConfig *cfg) {
    cairo_save(cr);
    cairo_add_rounded_rect(cr, rx, ry, rw, rh, cfg->window_corner_radius);

    cairo_pattern_t *pattern = cairo_pattern_create_linear(rx, ry, rx + rw, ry + rh);
    cairo_pattern_add_color_stop_rgba(pattern, 0.0, 0.16, 0.24, 0.31, 0.65);
    cairo_pattern_add_color_stop_rgba(pattern, 1.0, 0.10, 0.16, 0.21, 0.65);
    cairo_set_source(cr, pattern);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(pattern);

    cairo_set_line_width(cr, 1.5);
    cairo_set_source_rgba_color(cr, &cfg->window_border);
    cairo_stroke(cr);
    cairo_restore(cr);
}

