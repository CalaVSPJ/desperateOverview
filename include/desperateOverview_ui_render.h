#ifndef DESPERATEOVERVIEW_UI_RENDER_H
#define DESPERATEOVERVIEW_UI_RENDER_H

#include <gtk/gtk.h>
#include <cairo.h>

#include "desperateOverview_types.h"

GdkPixbuf *desperateOverview_ui_orient_pixbuf(GdkPixbuf *src);

void desperateOverview_ui_cache_window_preview(WindowInfo *win,
                                               double rx,
                                               double ry,
                                               double rw,
                                               double rh,
                                               gboolean bottom_view);

gboolean desperateOverview_ui_draw_background(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean desperateOverview_ui_draw_current_workspace(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean desperateOverview_ui_draw_cell(GtkWidget *widget, cairo_t *cr, gpointer data);

#endif /* DESPERATEOVERVIEW_UI_RENDER_H */

