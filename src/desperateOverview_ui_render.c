#define _GNU_SOURCE

#include "desperateOverview_ui_render.h"

#include <math.h>
#include <string.h>

#include "desperateOverview_config.h"
#include "desperateOverview_geometry.h"
#include "desperateOverview_ui_drawing.h"
#include "desperateOverview_ui_state.h"

static const double G_WINDOW_BORDER_WIDTH      = 2.0;
static const double G_WINDOW_BORDER_HOVER_W    = 2.5;
static const double G_CLOSE_BTN_RADIUS         = 13.0;
static const double G_CLOSE_BTN_RADIUS_SMALL   = 9.0;

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
    gboolean is_hovered = !is_dragged && (win == g_hover_window);

    if (is_dragged)
        cairo_push_group(cr);

    /* Border: highlight when hovered, normal otherwise. */
    {
        double bw = is_hovered ? G_WINDOW_BORDER_HOVER_W : G_WINDOW_BORDER_WIDTH;
        cairo_save(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_add_rounded_rect(cr, rx, ry, rw, rh, cfg->window_corner_radius);
        double inner_r = fmax(0.0, cfg->window_corner_radius - bw);
        cairo_add_rounded_rect(cr, rx + bw, ry + bw, rw - 2.0 * bw, rh - 2.0 * bw, inner_r);
        if (is_hovered)
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.80);
        else
            cairo_set_source_rgba_color(cr, &cfg->window_border);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    /* Content: thumbnail or placeholder drawn inside the inner area. */
    double bw = is_hovered ? G_WINDOW_BORDER_HOVER_W : G_WINDOW_BORDER_WIDTH;
    double ix = rx + bw;
    double iy = ry + bw;
    double iw = rw - 2.0 * bw;
    double ih = rh - 2.0 * bw;
    double ir = fmax(0.0, cfg->window_corner_radius - bw);

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

        /* Title label centered in thumbnail when hovered. */
        if (is_hovered && iw > 20 && ih > 14) {
            const char *label = NULL;
            if (win->class_name && *win->class_name)
                label = win->class_name;
            else if (win->initial_class && *win->initial_class)
                label = win->initial_class;
            else if (win->title && *win->title)
                label = win->title;

            if (label && *label) {
                double font_sz = fmin(fmax(ih * 0.16, 10.0), 14.0);
                double pad     = font_sz + 8.0;

                cairo_save(cr);
                cairo_add_rounded_rect(cr, ix, iy, iw, ih, ir);
                cairo_clip(cr);

                /* Semi-transparent overlay covering the full thumbnail. */
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.45);
                cairo_rectangle(cr, ix, iy, iw, ih);
                cairo_fill(cr);

                cairo_select_font_face(cr, "sans",
                                       CAIRO_FONT_SLANT_NORMAL,
                                       CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(cr, font_sz);

                cairo_text_extents_t te;
                cairo_text_extents(cr, label, &te);
                double tx = ix + (iw - te.width) / 2.0 - te.x_bearing;
                double ty = iy + (ih - te.height) / 2.0 - te.y_bearing;
                if (tx < ix + pad) tx = ix + pad;

                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
                cairo_move_to(cr, tx, ty);
                cairo_show_text(cr, label);

                cairo_restore(cr);
            }
        }
    }

    /* Close button in top-right corner when hovered. */
    if (is_hovered) {
        double btn_r = (fmin(rw, rh) < 40.0) ? G_CLOSE_BTN_RADIUS_SMALL : G_CLOSE_BTN_RADIUS;
        double btn_cx = rx + btn_r + 3.0;
        double btn_cy = ry + btn_r + 3.0;

        win->close_btn_cx    = btn_cx;
        win->close_btn_cy    = btn_cy;
        win->close_btn_r     = btn_r;
        win->close_btn_valid = TRUE;

        cairo_save(cr);
        cairo_arc(cr, btn_cx, btn_cy, btn_r, 0, 2.0 * M_PI);
        cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.88);
        cairo_fill(cr);

        cairo_arc(cr, btn_cx, btn_cy, btn_r, 0, 2.0 * M_PI);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.55);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        double xoff = btn_r * 0.42;
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
        cairo_set_line_width(cr, 1.5);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, btn_cx - xoff, btn_cy - xoff);
        cairo_line_to(cr, btn_cx + xoff, btn_cy + xoff);
        cairo_move_to(cr, btn_cx + xoff, btn_cy - xoff);
        cairo_line_to(cr, btn_cx - xoff, btn_cy + xoff);
        cairo_stroke(cr);
        cairo_restore(cr);
    } else {
        win->close_btn_valid = FALSE;
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
        win->close_btn_valid   = FALSE;
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

