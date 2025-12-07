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

    gboolean drew_texture = FALSE;
    if (source) {
        GdkPixbuf *oriented = desperateOverview_ui_orient_pixbuf(source);
        int target_w = (int)ceil(rw);
        int target_h = (int)ceil(rh);
        if (target_w <= 0) target_w = 1;
        if (target_h <= 0) target_h = 1;

        if (oriented) {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
                oriented,
                target_w,
                target_h,
                GDK_INTERP_BILINEAR
            );

            if (scaled) {
                cairo_save(cr);
                cairo_add_rounded_rect(cr, rx, ry, rw, rh, cfg->window_corner_radius);
                cairo_clip(cr);
                gdk_cairo_set_source_pixbuf(cr, scaled, rx, ry);
                cairo_paint(cr);
                cairo_restore(cr);
                g_object_unref(scaled);
                drew_texture = TRUE;
            }
            g_object_unref(oriented);
        }
    }

    if (!drew_texture) {
        cairo_save(cr);
        cairo_add_rounded_rect(cr, rx, ry, rw, rh, cfg->window_corner_radius);
        cairo_clip(cr);
        ui_draw_window_placeholder(cr, rx, ry, rw, rh, cfg);
        cairo_restore(cr);
    }

    if (g_drag.in_progress && g_drag.active_window == win) {
        cairo_save(cr);
        cairo_add_rounded_rect(cr, rx, ry, rw, rh, cfg->window_corner_radius);
        double fill_alpha = fmin(1.0, cfg->drag_highlight.alpha * 0.35);
        cairo_set_source_rgba(cr,
                              cfg->drag_highlight.red,
                              cfg->drag_highlight.green,
                              cfg->drag_highlight.blue,
                              fill_alpha);
        cairo_fill_preserve(cr);
        double dash[] = {6.0, 4.0};
        cairo_set_dash(cr, dash, 2, 0);
        cairo_set_line_width(cr, 3.0);
        cairo_set_source_rgba_color(cr, &cfg->drag_highlight);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    ui_draw_window_border(cr, rx, ry, rw, rh, cfg, G_WINDOW_BORDER_WIDTH);
}

void desperateOverview_ui_cache_window_preview(WindowInfo *win,
                                               double rx,
                                               double ry,
                                               double rw,
                                               double rh,
                                               gboolean bottom_view) {
    if (!win)
        return;
    if (bottom_view) {
        win->bottom_preview_x = rx;
        win->bottom_preview_y = ry;
        win->bottom_preview_w = rw;
        win->bottom_preview_h = rh;
        win->bottom_preview_valid = TRUE;
    } else {
        win->top_preview_x = rx;
        win->top_preview_y = ry;
        win->top_preview_w = rw;
        win->top_preview_h = rh;
        win->top_preview_valid = TRUE;
    }
}

static gboolean monitor_transform_is_rotated(void) {
    return (g_mon_transform % 2) != 0;
}

static int get_effective_mon_width(void) {
    if (monitor_transform_is_rotated() && g_mon_height > 0)
        return g_mon_height;
    return g_mon_width;
}

static int get_effective_mon_height(void) {
    if (monitor_transform_is_rotated() && g_mon_width > 0)
        return g_mon_width;
    return g_mon_height;
}

gboolean desperateOverview_ui_draw_background(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    int eff_w = get_effective_mon_width();
    int eff_h = get_effective_mon_height();
    double width = alloc.width > 0 ? alloc.width : eff_w;
    double height = alloc.height > 0 ? alloc.height : eff_h;
    const OverlayConfig *cfg = config_get();
    cairo_set_source_rgba_color(cr, &cfg->overlay_bg);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
    return TRUE;
}

gboolean desperateOverview_ui_draw_current_workspace(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    double width = alloc.width;
    double height = alloc.height;
    if (width <= 0 || height <= 0)
        return FALSE;

    if (g_active_workspace <= 0 || g_active_workspace >= MAX_WS)
        return TRUE;

    int eff_w = get_effective_mon_width();
    int eff_h = get_effective_mon_height();
    if (eff_w <= 0 || eff_h <= 0)
        return TRUE;

    WorkspaceWindows *Wws = &g_ws[g_active_workspace];
    if (!Wws)
        return TRUE;

    OverviewPreviewTransform transform;
    desperateOverview_geometry_compute_preview_transform(
        eff_w, eff_h, width, height, &transform);

    const OverlayConfig *cfg = config_get();
    cairo_save(cr);
    cairo_add_rounded_rect(cr,
                           transform.offset_x,
                           transform.offset_y,
                           transform.view_w,
                           transform.view_h,
                           cfg->workspace_corner_radius);
    cairo_set_source_rgba_color(cr, &cfg->active_ws_bg);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 3.0);
    cairo_set_source_rgba_color(cr, &cfg->active_ws_border);
    cairo_stroke(cr);

    g_current_preview_rect.x = transform.offset_x;
    g_current_preview_rect.y = transform.offset_y;
    g_current_preview_rect.w = transform.view_w;
    g_current_preview_rect.h = transform.view_h;
    g_current_preview_rect.valid = TRUE;

    if (Wws->count <= 0) {
        cairo_restore(cr);
        return TRUE;
    }

    for (int i = 0; i < Wws->count; ++i) {
        WindowInfo *win = &Wws->wins[i];
        if (!win || win->w <= 0 || win->h <= 0) {
            if (win)
                win->bottom_preview_valid = FALSE;
            continue;
        }
        win->bottom_preview_valid = FALSE;

        OverviewRect norm;
        desperateOverview_geometry_window_to_normalized(
            g_mon_width, g_mon_height, g_mon_off_x, g_mon_off_y, g_mon_transform,
            win->x, win->y, win->w, win->h, &norm);

        double rx = transform.offset_x + norm.x * transform.view_w;
        double ry = transform.offset_y + norm.y * transform.view_h;
        double rw = norm.w * transform.view_w;
        double rh = norm.h * transform.view_h;

        rw = desperateOverview_geometry_clamp(rw, 2.0, transform.view_w);
        rh = desperateOverview_geometry_clamp(rh, 2.0, transform.view_h);

        desperateOverview_ui_cache_window_preview(win, rx, ry, rw, rh, TRUE);
        GdkPixbuf *preview = win->live_pixbuf ? win->live_pixbuf : win->thumb_pixbuf;
        draw_window_preview(cr, win, preview, rx, ry, rw, rh, cfg);
    }

    cairo_restore(cr);
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

        desperateOverview_ui_cache_window_preview(win, rx, ry, rw, rh, FALSE);
        draw_window_preview(cr, win, win->thumb_pixbuf, rx, ry, rw, rh, cfg);
    }

out_restore_cell:
    cairo_restore(cr);
    return TRUE;
}

