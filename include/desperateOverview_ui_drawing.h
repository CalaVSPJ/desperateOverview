#ifndef DESPERATEOVERVIEW_UI_DRAWING_H
#define DESPERATEOVERVIEW_UI_DRAWING_H

#include <cairo.h>
#include "desperateOverview_config.h"

void cairo_add_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double radius);
void cairo_set_source_rgba_color(cairo_t *cr, const GdkRGBA *color);
void ui_draw_window_border(cairo_t *cr, double rx, double ry, double rw, double rh, const OverlayConfig *cfg, double border_width);
void ui_draw_window_placeholder(cairo_t *cr, double rx, double ry, double rw, double rh, const OverlayConfig *cfg);

#endif /* DESPERATEOVERVIEW_UI_DRAWING_H */

