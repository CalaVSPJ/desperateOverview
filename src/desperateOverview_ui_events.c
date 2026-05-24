#define _GNU_SOURCE

#include "desperateOverview_ui_events.h"

#include <math.h>
#include <string.h>

#include "desperateOverview_core.h"
#include "desperateOverview_config.h"
#include "desperateOverview_geometry.h"
#include "desperateOverview_ui.h"
#include "desperateOverview_ui_drag.h"
#include "desperateOverview_ui_render.h"
#include "desperateOverview_ui_state.h"

static const double G_DRAG_HOLD_MOVE_THRESHOLD = 3.0;

static void follow_drop_to_workspace(int target_ws) {
    if (target_ws <= 0 || target_ws >= MAX_WS)
        return;
    const OverlayConfig *cfg = config_get();
    if (!cfg || !cfg->follow_drop)
        return;
    desperateOverview_ui_refresh_active_workspace_view(target_ws);
    const char *name = desperateOverview_ui_workspace_display_name(target_ws);
    desperateOverview_core_switch_workspace(name, target_ws);
}

static int resolve_workspace_id(gpointer data) {
    int wsid = GPOINTER_TO_INT(data);
    if (wsid == 0)
        wsid = g_active_workspace;
    return wsid;
}

static WindowInfo *hit_test_window_view(int wsid, double px, double py) {
    if (wsid <= 0 || wsid >= MAX_WS)
        return NULL;

    WorkspaceWindows *W = &g_ws[wsid];
    /* Iterate in reverse so the topmost-drawn (last in list) window wins
     * when floating windows overlap tiled ones. */
    for (int i = W->count - 1; i >= 0; --i) {
        WindowInfo *win = &W->wins[i];
        if (!win->top_preview_valid)
            continue;
        double x1 = win->top_preview_x + win->top_preview_w;
        double y1 = win->top_preview_y + win->top_preview_h;
        if (px >= win->top_preview_x && px <= x1 &&
            py >= win->top_preview_y && py <= y1)
            return win;
    }
    return NULL;
}


void desperateOverview_ui_set_hover_window(WindowInfo *win) {
    if (!g_overlay_visible)
        return;
    if (win == g_hover_window)
        return;
    g_hover_window = win;
    desperateOverview_ui_queue_cells_redraw();
}

static gboolean drag_hold_timeout_cb(DragState *drag, gpointer data) {
    (void)data;
    if (!drag || !drag->hold_widget || !GTK_IS_WIDGET(drag->hold_widget) ||
        !drag->hold_event_copy || !drag->active_window) {
        ui_drag_cancel_hold(drag);
        return G_SOURCE_REMOVE;
    }

    GdkEventButton *btn = (GdkEventButton *)drag->hold_event_copy;
    GtkTargetList *list = gtk_target_list_new(g_drag_targets,
                                              g_drag_targets_count);
    gtk_drag_begin_with_coordinates(
        drag->hold_widget,
        list,
        GDK_ACTION_MOVE,
        btn->button,
        drag->hold_event_copy,
        (int)btn->x,
        (int)btn->y
    );
    gtk_target_list_unref(list);

    desperateOverview_ui_queue_cells_redraw();
    ui_drag_cancel_hold(drag);
    return G_SOURCE_REMOVE;
}

void desperateOverview_ui_start_drag_hold_timer(GtkWidget *widget, GdkEventButton *event) {
    desperateOverview_ui_cancel_drag_hold_timer();
    if (!widget || !event)
        return;

    g_drag.hold_start_x = event->x;
    g_drag.hold_start_y = event->y;
    const OverlayConfig *cfg = config_get();
    guint delay = cfg->drag_hold_delay_ms > 0 ? cfg->drag_hold_delay_ms : 150;
    ui_drag_start_hold(&g_drag, widget, event, delay, drag_hold_timeout_cb, NULL);
}

void desperateOverview_ui_cancel_drag_hold_timer(void) {
    ui_drag_cancel_hold(&g_drag);
}

gboolean desperateOverview_ui_on_cell_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    (void)widget;
    int wsid = resolve_workspace_id(data);
    WindowInfo *hit = hit_test_window_view(wsid, event->x, event->y);
    desperateOverview_ui_set_hover_window(hit);

    if (g_drag.hold_source_id && widget == g_drag.hold_widget) {
        double dx = fabs(event->x - g_drag.hold_start_x);
        double dy = fabs(event->y - g_drag.hold_start_y);
        if (dx > G_DRAG_HOLD_MOVE_THRESHOLD || dy > G_DRAG_HOLD_MOVE_THRESHOLD)
            desperateOverview_ui_cancel_drag_hold_timer();
    }
    return FALSE;
}

gboolean desperateOverview_ui_on_cell_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
    (void)widget;
    (void)event;
    (void)data;
    if (!g_drag.in_progress)
        desperateOverview_ui_set_hover_window(NULL);
    return FALSE;
}

static void set_drag_icon_from_window(GdkDragContext *context, WindowInfo *win) {
    if (!context || !win)
        return;

    GdkPixbuf *icon = NULL;

    if (win->thumb_pixbuf) {
        GdkPixbuf *source = desperateOverview_ui_orient_pixbuf(win->thumb_pixbuf);
        if (source) {
            int src_w = gdk_pixbuf_get_width(source);
            int src_h = gdk_pixbuf_get_height(source);
            if (src_w > 0 && src_h > 0) {
                int target_w = src_w;
                int target_h = src_h;
                const int max_dim = 320;
                const int min_dim = 48;

                if (src_w > max_dim || src_h > max_dim) {
                    double sx = (double)max_dim / (double)src_w;
                    double sy = (double)max_dim / (double)src_h;
                    double scale = (sx < sy) ? sx : sy;
                    target_w = (int)((double)src_w * scale + 0.5);
                    target_h = (int)((double)src_h * scale + 0.5);
                }

                target_w = desperateOverview_geometry_clamp_int(target_w, min_dim, max_dim);
                target_h = desperateOverview_geometry_clamp_int(target_h, min_dim, max_dim);

                if (target_w == src_w && target_h == src_h) {
                    icon = source;
                    source = NULL;
                } else {
                    icon = gdk_pixbuf_scale_simple(
                        source,
                        target_w,
                        target_h,
                        GDK_INTERP_BILINEAR
                    );
                }
            }
            if (source)
                g_object_unref(source);
        }
    }

    if (!icon) {
        int w = 200;
        int h = 120;
        icon = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
        if (icon) {
            gdk_pixbuf_fill(icon, 0x44aaff88);
        }
    }

    if (icon) {
        int hot_x = gdk_pixbuf_get_width(icon) / 2;
        int hot_y = gdk_pixbuf_get_height(icon) / 2;
        gtk_drag_set_icon_pixbuf(context, icon, hot_x, hot_y);
        g_object_unref(icon);
    }
}

gboolean desperateOverview_ui_on_cell_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button != GDK_BUTTON_PRIMARY)
        return FALSE;

    desperateOverview_ui_cancel_drag_hold_timer();
    int wsid = resolve_workspace_id(data);
    WindowInfo *hit = hit_test_window_view(wsid, event->x, event->y);
    if (hit && hit->close_btn_valid) {
        double dx = event->x - hit->close_btn_cx;
        double dy = event->y - hit->close_btn_cy;
        double r  = hit->close_btn_r + 3.0;
        if (dx * dx + dy * dy <= r * r) {
            desperateOverview_core_close_window(hit->addr);
            return TRUE;
        }
    }
    if (hit) {
        g_drag.active_window = hit;
        g_drag.source_workspace = wsid;
        g_drag.pending_window_click = TRUE;
        g_drag.pending_ws_click = FALSE;
        g_drag.pending_ws_id = wsid;
        desperateOverview_ui_start_drag_hold_timer(widget, event);
    } else {
        g_drag.active_window = NULL;
        g_drag.source_workspace = -1;
        g_drag.pending_ws_click = TRUE;
        g_drag.pending_window_click = FALSE;
        g_drag.pending_ws_id = wsid;
    }

    return TRUE;
}

gboolean desperateOverview_ui_on_cell_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    if (event->button != GDK_BUTTON_PRIMARY)
        return FALSE;

    desperateOverview_ui_cancel_drag_hold_timer();

    int wsid = resolve_workspace_id(data);

    if (!g_drag.in_progress &&
        (g_drag.pending_window_click || g_drag.pending_ws_click) &&
        wsid == g_drag.pending_ws_id) {
        g_drag.pending_window_click = FALSE;
        g_drag.pending_ws_click = FALSE;
        g_drag.pending_ws_id = -1;
        desperateOverview_ui_refresh_active_workspace_view(wsid);
        close_overlay();
        desperateOverview_core_switch_workspace(desperateOverview_ui_workspace_display_name(wsid), wsid);
        return TRUE;
    }

    if (!g_drag.in_progress) {
        g_drag.active_window = NULL;
        g_drag.source_workspace = -1;
    }

    g_drag.pending_ws_click = FALSE;
    g_drag.pending_ws_id = -1;
    g_drag.pending_window_click = FALSE;
    return TRUE;
}

void desperateOverview_ui_on_cell_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer data) {
    (void)widget;
    int wsid = resolve_workspace_id(data);
    desperateOverview_ui_cancel_drag_hold_timer();

    if (!g_drag.active_window || wsid != g_drag.source_workspace) {
        gdk_drag_abort(context, GDK_CURRENT_TIME);
        return;
    }

    g_drag.in_progress = TRUE;
    g_drag.pending_ws_click = FALSE;
    g_drag.pending_window_click = FALSE;
    g_drag.pending_ws_id = -1;
    set_drag_icon_from_window(context, g_drag.active_window);
    desperateOverview_ui_queue_cells_redraw();
}

void desperateOverview_ui_on_cell_drag_end(GtkWidget *widget, GdkDragContext *context, gpointer data) {
    (void)widget; (void)context; (void)data;
    desperateOverview_ui_cancel_drag_hold_timer();
    g_drag.in_progress = FALSE;
    g_drag.active_window = NULL;
    g_drag.source_workspace = -1;
    g_drag.pending_ws_click = FALSE;
    g_drag.pending_window_click = FALSE;
    g_drag.pending_ws_id = -1;
    desperateOverview_ui_queue_cells_redraw();
}

void desperateOverview_ui_on_cell_drag_data_get(GtkWidget *widget,
                                                GdkDragContext *context,
                                                GtkSelectionData *selection_data,
                                                guint info,
                                                guint time,
                                                gpointer data) {
    (void)widget; (void)context; (void)info; (void)time; (void)data;

    if (!g_drag.active_window || !g_drag.active_window->addr[0])
        return;

    gtk_selection_data_set(
        selection_data,
        gdk_atom_intern_static_string(g_drag_target_name),
        8,
        (const guchar *)g_drag.active_window->addr,
        (gint)(strlen(g_drag.active_window->addr) + 1)
    );
}

void desperateOverview_ui_on_cell_drag_data_received(GtkWidget *widget,
                                                     GdkDragContext *context,
                                                     gint x,
                                                     gint y,
                                                     GtkSelectionData *selection_data,
                                                     guint info,
                                                     guint time,
                                                     gpointer data) {
    (void)widget; (void)x; (void)y; (void)info;

    gboolean success = FALSE;
    int target_ws = resolve_workspace_id(data);

    if (target_ws > 0 && target_ws < MAX_WS &&
        selection_data &&
        gtk_selection_data_get_length(selection_data) > 0) {

        const guchar *payload = gtk_selection_data_get_data(selection_data);
        gint len = gtk_selection_data_get_length(selection_data);

        if (payload && len > 0) {
            char addr[64];
            gint copy = len >= (gint)sizeof(addr) ? (gint)sizeof(addr) - 1 : len;
            memcpy(addr, payload, (size_t)copy);
            addr[copy] = '\0';

            if (addr[0]) {
                            desperateOverview_core_move_window(addr, target_ws);
                success = TRUE;
            }
        }
    }

    gtk_drag_finish(context, success, FALSE, time);

    if (success) {
        follow_drop_to_workspace(target_ws);
        desperateOverview_ui_queue_cells_redraw();
    }
}

gboolean desperateOverview_ui_on_cell_drag_drop(GtkWidget *widget,
                                                GdkDragContext *context,
                                                gint x,
                                                gint y,
                                                guint time,
                                                gpointer data) {
    (void)x; (void)y; (void)data;

    gtk_drag_get_data(
        widget,
        context,
        gdk_atom_intern_static_string(g_drag_target_name),
        time
    );

    return TRUE;
}

gboolean desperateOverview_ui_on_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget; (void)data;

    if (event->keyval == GDK_KEY_Escape) {
        close_overlay();
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        if (config_workspace_slot(g_active_workspace) >= 0) {
            close_overlay();
            desperateOverview_core_switch_workspace(
                desperateOverview_ui_workspace_display_name(g_active_workspace),
                g_active_workspace);
        }
        return TRUE;
    }

    const OverlayConfig *cfg = config_get();
    int rows = (int)cfg->grid_rows;
    int cols = (int)cfg->grid_cols;
    if (rows <= 0 || cols <= 0 || cfg->workspace_count == 0)
        return FALSE;

    int target_ws = -1;

    /* 1..9 jump keys map to the first nine configured slots. */
    if (event->keyval >= GDK_KEY_1 && event->keyval <= GDK_KEY_9) {
        int slot = (int)(event->keyval - GDK_KEY_1);
        target_ws = config_workspace_at_slot(slot);
    } else {
        int drow = 0, dcol = 0;
        switch (event->keyval) {
            case GDK_KEY_Left:  case GDK_KEY_KP_Left:  dcol = -1; break;
            case GDK_KEY_Right: case GDK_KEY_KP_Right: dcol = +1; break;
            case GDK_KEY_Up:    case GDK_KEY_KP_Up:    drow = -1; break;
            case GDK_KEY_Down:  case GDK_KEY_KP_Down:  drow = +1; break;
            default:                                              return FALSE;
        }

        int start_slot = config_workspace_slot(g_active_workspace);
        if (start_slot < 0)
            start_slot = 0;
        int row = start_slot / cols;
        int col = start_slot % cols;

        /* Walk in the requested direction, skipping unpopulated slots so
         * grids that aren't full rectangles (e.g. 7 workspaces in a 2x5
         * grid) still wrap intuitively. Give up if we loop back to where
         * we started — that only happens when there's no other populated
         * slot reachable along this axis. */
        int new_slot = start_slot;
        for (int steps = 0; steps < rows * cols; ++steps) {
            row = (row + drow + rows) % rows;
            col = (col + dcol + cols) % cols;
            int candidate = row * cols + col;
            if (candidate == start_slot)
                break;
            if (config_workspace_at_slot(candidate) > 0) {
                new_slot = candidate;
                break;
            }
        }
        target_ws = config_workspace_at_slot(new_slot);
        if (target_ws <= 0)
            return TRUE;
    }

    if (target_ws > 0 && target_ws < MAX_WS) {
        g_active_workspace = target_ws;
        desperateOverview_ui_set_hover_window(NULL);
        desperateOverview_ui_queue_cells_redraw();
        return TRUE;
    }

    return FALSE;
}

