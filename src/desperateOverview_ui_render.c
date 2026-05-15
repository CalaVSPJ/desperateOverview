#define _GNU_SOURCE

#include "desperateOverview_ui_render.h"

#include <math.h>

#include "desperateOverview_config.h"
#include "desperateOverview_geometry.h"
#include "desperateOverview_ui_drawing.h"
#include "desperateOverview_ui_state.h"

static const double G_WINDOW_BORDER_WIDTH = 2.0;

GdkPixbuf *desperateOverview_ui_orient_pixbuf(GdkPixbuf *src) {
    if (!src)
        return NULL;

    int t = g_mon_transform % 4;
    if (t < 0)
        t += 4;

    if (t == 0)
        return g_object_ref(src);

    GdkPixbufRotation rotation = GDK_PIXBUF_ROTATE_NONE;
    switch (t) {
        case 1:
            rotation = GDK_PIXBUF_ROTATE_CLOCKWISE;
            break;
        case 2:
            rotation = GDK_PIXBUF_ROTATE_UPSIDEDOWN;
            break;
        case 3:
            rotation = GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE;
            break;
        default:
            rotation = GDK_PIXBUF_ROTATE_NONE;
            break;
    }

    GdkPixbuf *rotated = gdk_pixbuf_rotate_simple(src, rotation);
    if (!rotated)
        return g_object_ref(src);
    return rotated;
}

static void draw_window_preview(cairo_t *cr,
                                WindowInfo *win,
                                GdkPixbuf *source,
                                double rx,
                                double ry,
                                double rw,
                                double rh,
                                const OverlayConfig *cfg) {
    if (!win || !cfg)
        return;

    gboolean is_dragged = g_drag.in_progress && g_drag.active_window == win;
    if (is_dragged)
        cairo_push_group(cr);

    /* Border: filled ring between outer rect and inset inner rect.
     * EVEN_ODD rule cuts out the inner area, leaving only the frame.
     * Corners are solid filled arcs — visible against any background. */
    {
        cairo_save(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_add_rounded_rect(cr, rx, ry, rw, rh, cfg->window_corner_radius);
        double inner_r = fmax(0.0, cfg->window_corner_radius - G_WINDOW_BORDER_WIDTH);
        cairo_add_rounded_rect(cr,
                               rx + G_WINDOW_BORDER_WIDTH,
                               ry + G_WINDOW_BORDER_WIDTH,
                               rw - 2.0 * G_WINDOW_BORDER_WIDTH,
                               rh - 2.0 * G_WINDOW_BORDER_WIDTH,
                               inner_r);
        cairo_set_source_rgba_color(cr, &cfg->window_border);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    /* Content: thumbnail or placeholder drawn inside the inner area. */
    double ix = rx + G_WINDOW_BORDER_WIDTH;
    double iy = ry + G_WINDOW_BORDER_WIDTH;
    double iw = rw - 2.0 * G_WINDOW_BORDER_WIDTH;
    double ih = rh - 2.0 * G_WINDOW_BORDER_WIDTH;
    double ir = fmax(0.0, cfg->window_corner_radius - G_WINDOW_BORDER_WIDTH);

    if (iw > 0 && ih > 0) {
        if (source) {
            GdkPixbuf *oriented = desperateOverview_ui_orient_pixbuf(source);
            if (oriented) {
                int tw = (int)ceil(iw);
                int th = (int)ceil(ih);
                if (tw < 1) tw = 1;
                if (th < 1) th = 1;
                GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
                    oriented, tw, th, GDK_INTERP_BILINEAR);
                if (scaled) {
                    cairo_save(cr);
                    cairo_add_rounded_rect(cr, ix, iy, iw, ih, ir);
                    cairo_clip(cr);
                    gdk_cairo_set_source_pixbuf(cr, scaled, ix, iy);
                    cairo_paint(cr);
                    cairo_restore(cr);
                    g_object_unref(scaled);
                }
                g_object_unref(oriented);
            }
        } else {
            cairo_save(cr);
            cairo_add_rounded_rect(cr, ix, iy, iw, ih, ir);
            cairo_clip(cr);
            ui_draw_window_placeholder(cr, ix, iy, iw, ih, cfg);
            cairo_restore(cr);
        }
    }

    if (is_dragged) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, 0.35);
    }
}

void desperateOverview_ui_cache_window_preview(WindowInfo *win,
                                               double rx,
                                               double ry,
                                               double rw,
                                               double rh) {
    if (!win)
        return;
    win->top_preview_x = rx;
    win->top_preview_y = ry;
    win->top_preview_w = rw;
    win->top_preview_h = rh;
    win->top_preview_valid = TRUE;
}

gboolean desperateOverview_ui_draw_background(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    double width  = alloc.width  > 0 ? alloc.width  : (double)desperateOverview_ui_get_effective_mon_width();
    double height = alloc.height > 0 ? alloc.height : (double)desperateOverview_ui_get_effective_mon_height();

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    const OverlayConfig *cfg = config_get();
    cairo_set_source_rgba_color(cr, &cfg->overlay_bg);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    return TRUE;
}

gboolean desperateOverview_ui_draw_cell(GtkWidget *widget, cairo_t *cr, gpointer data) {
    int wsid = GPOINTER_TO_INT(data);

    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    double W = a.width;
    double H = a.height;

    cairo_save(cr);
    const OverlayConfig *cfg = config_get();
    cairo_add_rounded_rect(cr, 2.0, 2.0, W - 4.0, H - 4.0, cfg->workspace_corner_radius);
    if (wsid == g_active_workspace)
        cairo_set_source_rgba_color(cr, &cfg->active_ws_bg);
    else
        cairo_set_source_rgba_color(cr, &cfg->inactive_ws_bg);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 3.0);
    if (wsid == g_active_workspace)
        cairo_set_source_rgba_color(cr, &cfg->active_ws_border);
    else
        cairo_set_source_rgba_color(cr, &cfg->inactive_ws_border);
    cairo_stroke(cr);

    cairo_restore(cr);

    cairo_save(cr);

    WorkspaceWindows *Wws = &g_ws[wsid];
    if (Wws->count <= 0)
        goto out_restore_cell;

    double pad_top    = 8.0;
    double pad_sides  = 8.0;
    double pad_bottom = 10.0;

    double inner_w = W - 2.0 * pad_sides;
    double inner_h = H - pad_top - pad_bottom;
    if (inner_w <= 0 || inner_h <= 0)
        goto out_restore_cell;

    double ix = pad_sides;
    double iy = pad_top;

    for (int i = 0; i < Wws->count; ++i) {
        WindowInfo *win = &Wws->wins[i];
        win->top_preview_valid = FALSE;
        if (win->w <= 0 || win->h <= 0)
            continue;

        OverviewRect norm;
        desperateOverview_geometry_window_to_normalized(
            g_mon_width, g_mon_height, g_mon_off_x, g_mon_off_y, g_mon_transform,
            win->x, win->y, win->w, win->h, &norm);

        double rx = ix + norm.x * inner_w;
        double ry = iy + norm.y * inner_h;
        double rw = norm.w * inner_w;
        double rh = norm.h * inner_h;

        rw = desperateOverview_geometry_clamp(rw, 5.0, inner_w);
        rh = desperateOverview_geometry_clamp(rh, 5.0, inner_h);

        if (rx < ix) rx = ix;
        if (ry < iy) ry = iy;
        if (rx + rw > ix + inner_w) rw = (ix + inner_w) - rx;
        if (ry + rh > iy + inner_h) rh = (iy + inner_h) - ry;

        if (rw <= 0 || rh <= 0)
            continue;

        desperateOverview_ui_cache_window_preview(win, rx, ry, rw, rh);
        GdkPixbuf *preview = win->live_pixbuf ? win->live_pixbuf : win->thumb_pixbuf;
        draw_window_preview(cr, win, preview, rx, ry, rw, rh, cfg);
    }

out_restore_cell:
    cairo_restore(cr);
    return TRUE;
}

