#include "desperateOverview_ui_layout.h"
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "desperateOverview_core.h"
#include "desperateOverview_config.h"
#include "desperateOverview_geometry.h"
#include "desperateOverview_types.h"
#include "desperateOverview_ui_drag.h"
#include "desperateOverview_ui_drawing.h"
#include "desperateOverview_ui_live.h"
#include "desperateOverview_ui_css.h"
#include "desperateOverview_ui_state.h"
#include "desperateOverview_ui_thumb_cache.h"
#include "desperateOverview_ui_events.h"
#include "desperateOverview_ui_render.h"
#include "desperateOverview_ui.h"

void desperateOverview_ui_set_css_override(const char *path) {
    desperateOverview_css_set_override(path);
}

static void reset_interaction_state(void);
static gboolean on_overlay_window_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
static gboolean point_inside_widget(GtkWidget *target, GtkWidget *relative_to, double px, double py);
static GtkWidget *build_overlay_window(void);
void close_overlay(void);
static void on_overlay_destroy(GtkWidget *widget, gpointer data);
static gboolean show_overlay_idle(gpointer data);
static gboolean hide_overlay_idle(gpointer data);
static gboolean gtk_quit_idle(gpointer data);
static GdkMonitor *match_monitor_for_window(GtkWidget *window);
static void configure_layer_shell(GtkWindow *window);
static void prune_empty_workspaces(void);
static void handle_live_preview_ready(WindowInfo *win, GdkPixbuf *pixbuf, gpointer user_data);

static gboolean g_exit_on_hide = FALSE;

static void clear_window_resources(WindowInfo *win) {
    if (!win)
        return;
    if (win->thumb_pixbuf) {
        g_object_unref(win->thumb_pixbuf);
        win->thumb_pixbuf = NULL;
    }
    if (win->live_pixbuf) {
        g_object_unref(win->live_pixbuf);
        win->live_pixbuf = NULL;
    }
    if (win->thumb_b64) {
        g_free(win->thumb_b64);
        win->thumb_b64 = NULL;
    }
    if (win->class_name) {
        g_free(win->class_name);
        win->class_name = NULL;
    }
    if (win->initial_class) {
        g_free(win->initial_class);
        win->initial_class = NULL;
    }
    if (win->title) {
        g_free(win->title);
        win->title = NULL;
    }
    desperateOverview_live_cancel_tasks(win);
    win->top_preview_valid = FALSE;
    win->bottom_preview_valid = FALSE;
    win->thumb_crc = 0;
}

static void reset_interaction_state(void) {
    desperateOverview_ui_cancel_drag_hold_timer();
    ui_drag_reset(&g_drag);
    g_current_preview_rect.valid = FALSE;
    g_hover_window = NULL;
    g_hover_window_bottom = FALSE;
    if (g_status_label)
        gtk_label_set_text(GTK_LABEL(g_status_label), "");
    g_new_ws_target_hover = FALSE;
}


void desperateOverview_ui_refresh_active_workspace_view(int wsid) {
    if (wsid <= 0 || wsid >= MAX_WS)
        return;
    if (g_active_workspace != wsid)
        g_active_workspace = wsid;
    if (g_overlay_visible)
        desperateOverview_ui_build_live_previews(g_active_workspace, g_ws);
    desperateOverview_ui_set_hover_window(NULL, FALSE);
    desperateOverview_ui_queue_cells_redraw();
}

static void prune_empty_workspaces(void) {
    int write = 0;
    for (int read = 0; read < g_active_count; ++read) {
        int wsid = g_active_list[read];
        WorkspaceWindows *W = &g_ws[wsid];
        if (wsid == g_active_workspace || (W && W->count > 0)) {
            if (write != read)
                g_active_list[write] = g_active_list[read];
            ++write;
        }
    }
    if (write <= 0 && g_active_workspace > 0 && g_active_workspace < MAX_WS) {
        g_active_list[0] = g_active_workspace;
        if (!g_ws[g_active_workspace].name[0])
            g_snprintf(g_ws[g_active_workspace].name, CORE_WS_NAME_LEN, "%d", g_active_workspace);
        write = 1;
    }
    g_active_count = write;
}

static void clear_ui_state(void) {
    for (int wsid = 0; wsid < MAX_WS; ++wsid) {
        WorkspaceWindows *W = &g_ws[wsid];
        for (int i = 0; i < W->count; ++i) {
            clear_window_resources(&W->wins[i]);
        }
        W->count = 0;
        W->name[0] = '\0';
    }
    g_active_count = 0;
    memset(g_active_list, 0, sizeof(g_active_list));
}

static void copy_core_state_to_ui(void) {
    CoreState snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    desperateOverview_core_copy_state(&snapshot);

    clear_ui_state();

    g_mon_id       = snapshot.mon_id;
    g_mon_width    = snapshot.mon_width;
    g_mon_height   = snapshot.mon_height;
    g_mon_off_x    = snapshot.mon_off_x;
    g_mon_off_y    = snapshot.mon_off_y;
    g_mon_transform = snapshot.mon_transform;
    g_active_workspace = snapshot.active_workspace;
    g_active_count = snapshot.active_count;
    memcpy(g_active_list, snapshot.active_list, sizeof(g_active_list));
    g_current_preview_rect.valid = FALSE;
    int eff_w = desperateOverview_ui_get_effective_mon_width();
    int eff_h = desperateOverview_ui_get_effective_mon_height();
    g_aspect_ratio = (eff_w > 0 && eff_h > 0)
                     ? (double)eff_w / (double)eff_h
                     : 16.0 / 9.0;
    gboolean should_decode_thumbs = g_overlay_visible || g_force_decode_thumbs;
    gboolean cache_active = should_decode_thumbs;
    guint64 cache_generation = cache_active ? desperateOverview_thumb_cache_bump_generation() : 0;

    for (int wsid = 0; wsid < MAX_WS; ++wsid) {
        CoreWorkspace *src_ws = &snapshot.workspaces[wsid];
        WorkspaceWindows *dst_ws = &g_ws[wsid];
        dst_ws->count = src_ws->count;
        g_strlcpy(dst_ws->name, src_ws->name, CORE_WS_NAME_LEN);
        for (int j = 0; j < src_ws->count; ++j) {
            CoreWindow *src = &src_ws->wins[j];
            WindowInfo *dst = &dst_ws->wins[j];
            dst->x = src->x;
            dst->y = src->y;
            dst->w = src->w;
            dst->h = src->h;
            strncpy(dst->addr, src->addr, sizeof(dst->addr));
            dst->addr[sizeof(dst->addr) - 1] = '\0';
            dst->thumb_pixbuf = NULL;
            dst->live_pixbuf = NULL;
            dst->thumb_b64 = src->thumb_b64;
            src->thumb_b64 = NULL;
            dst->class_name = src->class_name;
            src->class_name = NULL;
            dst->initial_class = src->initial_class;
            src->initial_class = NULL;
            dst->title = src->title;
            src->title = NULL;
            dst->live_cookie = 0;
            dst->top_preview_valid = FALSE;
            dst->bottom_preview_valid = FALSE;
            dst->thumb_crc = 0;

            if (should_decode_thumbs && dst->thumb_b64 && dst->thumb_b64[0]) {
                guint32 new_crc = desperateOverview_thumb_cache_crc(dst->thumb_b64);
                dst->thumb_crc = new_crc;
                GdkPixbuf *cached = NULL;
                if (cache_active && dst->addr[0]) {
                    cached = desperateOverview_thumb_cache_lookup(dst->addr, new_crc, cache_generation);
                }
                if (cached) {
                    dst->thumb_pixbuf = cached;
                    continue;
                }
                gsize raw_len = 0;
                guchar *raw = g_base64_decode(dst->thumb_b64, &raw_len);
                if (raw && raw_len > 0) {
                    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
                    if (gdk_pixbuf_loader_write(loader, raw, raw_len, NULL)) {
                        gdk_pixbuf_loader_close(loader, NULL);
                        GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(loader);
                        if (pb) {
                            dst->thumb_pixbuf = g_object_ref(pb);
                            if (cache_active && dst->addr[0])
                                desperateOverview_thumb_cache_store(dst->addr, new_crc, dst->thumb_pixbuf, cache_generation);
                        }
                    } else {
                        gdk_pixbuf_loader_close(loader, NULL);
                    }
                    g_object_unref(loader);
                }
                if (raw)
                    g_free(raw);
            }
        }
    }

    if (cache_active)
        desperateOverview_thumb_cache_prune(cache_generation);

    if (g_overlay_visible || g_force_live_previews)
        desperateOverview_ui_build_live_previews(g_active_workspace, g_ws);

    desperateOverview_core_free_state(&snapshot);
    reset_interaction_state();
}

static gboolean point_inside_widget(GtkWidget *target, GtkWidget *relative_to, double px, double py) {
    if (!target || !gtk_widget_get_visible(target))
        return FALSE;

    GtkAllocation alloc;
    gtk_widget_get_allocation(target, &alloc);

    int local_x = 0;
    int local_y = 0;
    if (!gtk_widget_translate_coordinates(relative_to,
                                          target,
                                          (int)px,
                                          (int)py,
                                          &local_x,
                                          &local_y))
        return FALSE;

    return local_x >= 0 && local_x <= alloc.width &&
           local_y >= 0 && local_y <= alloc.height;
}

static gboolean on_overlay_window_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)data;
    if (event->button != GDK_BUTTON_PRIMARY)
        return FALSE;

    gboolean inside_top = point_inside_widget(g_overlay_content, widget, event->x, event->y);
    gboolean inside_bottom = point_inside_widget(g_current_preview, widget, event->x, event->y);

    if (!inside_top && !inside_bottom) {
        close_overlay();
        return TRUE;
    }
    return FALSE;
}

static gboolean show_overlay_idle(gpointer data) {
    (void)data;
    if (g_overlay_visible)
        return G_SOURCE_REMOVE;
    desperateOverview_core_set_thumbnail_capture_enabled(true);
    if (desperateOverview_core_state_needs_refresh())
        desperateOverview_core_request_full_refresh();
    g_force_decode_thumbs = TRUE;
    g_force_live_previews = TRUE;
    copy_core_state_to_ui();
    prune_empty_workspaces();
    g_overlay_window = build_overlay_window();
    g_overlay_visible = TRUE;
    desperateOverview_ui_build_live_previews(g_active_workspace, g_ws);
    return G_SOURCE_REMOVE;
}

static gboolean hide_overlay_idle(gpointer data) {
    (void)data;
    close_overlay();
    return G_SOURCE_REMOVE;
}

static gboolean gtk_quit_idle(gpointer data) {
    (void)data;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

static gboolean active_layout_changed(void) {
    if (!g_overlay_window)
        return FALSE;
    if (g_built_active_count != g_active_count)
        return TRUE;
    if (memcmp(g_built_active_list, g_active_list,
               sizeof(int) * g_active_count) != 0)
        return TRUE;
    for (int i = 0; i < g_active_count; ++i) {
        int wsid = g_active_list[i];
        const char *current = (wsid > 0 && wsid < MAX_WS && g_ws[wsid].name[0])
                              ? g_ws[wsid].name
                              : "";
        if (strncmp(g_built_active_names[i], current,
                    CORE_WS_NAME_LEN) != 0)
            return TRUE;
    }
    int active_idx = desperateOverview_ui_find_active_index(g_active_workspace);
    if (active_idx >= 0 && active_idx < g_active_count &&
        g_active_workspace != g_built_active_list[active_idx])
        return TRUE;
    return FALSE;
}

static gboolean overlay_idle_redraw(gpointer data) {
    (void)data;
    g_mutex_lock(&g_redraw_lock);
    g_redraw_pending = FALSE;
    g_mutex_unlock(&g_redraw_lock);

    copy_core_state_to_ui();
    prune_empty_workspaces();
    if (!g_overlay_visible)
        return G_SOURCE_REMOVE;

    if (active_layout_changed()) {
        desperateOverview_ui_rebuild_overlay_content();
    } else {
        desperateOverview_ui_queue_cells_redraw();
    }
    return G_SOURCE_REMOVE;
}

static void handle_live_preview_ready(WindowInfo *win, GdkPixbuf *pixbuf, gpointer user_data) {
    (void)user_data;
    if (!win)
        return;
    if (win->live_pixbuf)
        g_object_unref(win->live_pixbuf);
    win->live_pixbuf = pixbuf ? g_object_ref(pixbuf) : NULL;
    if (g_overlay_visible)
        desperateOverview_ui_queue_cells_redraw();
}

void desperateOverview_ui_core_redraw_callback(void *user_data) {
    (void)user_data;
    g_mutex_lock(&g_redraw_lock);
    if (!g_redraw_pending) {
        g_redraw_pending = TRUE;
        g_idle_add(overlay_idle_redraw, NULL);
    }
    g_mutex_unlock(&g_redraw_lock);
}

static gboolean fade_in_cb(gpointer data) {
    GtkWidget *window = GTK_WIDGET(data);
    if (!GTK_IS_WIDGET(window)) {
        g_fade_source_id = 0;
        return FALSE;
    }
    double op = gtk_widget_get_opacity(window);
    const OverlayConfig *cfg = config_get();
    double step = (cfg && cfg->fade_step > 0.0) ? cfg->fade_step : 0.08;
    op += step;
    if (op >= 1.0) {
        gtk_widget_set_opacity(window, 1.0);
        g_fade_source_id = 0;
        return FALSE;
    }
    gtk_widget_set_opacity(window, op);
    return TRUE;
}

static GtkWidget *build_overlay_window(void) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    configure_layer_shell(GTK_WINDOW(window));
    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK | GDK_BUTTON_PRESS_MASK);
    gtk_widget_set_can_focus(window, TRUE);
    gtk_widget_grab_focus(window);
    g_signal_connect(window, "key-press-event", G_CALLBACK(desperateOverview_ui_on_key), NULL);
    g_signal_connect(window, "button-press-event",
                     G_CALLBACK(on_overlay_window_button_press), NULL);

    gtk_widget_set_size_request(window, -1,
                                g_mon_height > 0 ? g_mon_height : 600);

    GtkWidget *root_overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(window), root_overlay);
    g_root_overlay = root_overlay;

    GtkWidget *bg = gtk_drawing_area_new();
    gtk_widget_set_hexpand(bg, TRUE);
    gtk_widget_set_vexpand(bg, TRUE);
    g_signal_connect(bg, "draw", G_CALLBACK(desperateOverview_ui_draw_background), NULL);
    gtk_container_add(GTK_CONTAINER(root_overlay), bg);

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_overlay_add_overlay(GTK_OVERLAY(root_overlay), root_box);
    g_root_box = root_box;

    desperateOverview_ui_build_overlay_content(root_box);

    gtk_widget_set_opacity(window, 0.0);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    g_signal_connect(window, "destroy", G_CALLBACK(on_overlay_destroy), NULL);
    if (g_fade_source_id) {
        g_source_remove(g_fade_source_id);
        g_fade_source_id = 0;
    }
    g_fade_source_id = g_timeout_add(16, fade_in_cb, window);
    g_built_active_count = g_active_count;
    memcpy(g_built_active_list, g_active_list, sizeof(g_active_list));
    for (int i = 0; i < g_active_count && i < MAX_WS; ++i) {
        int wsid = g_active_list[i];
        const char *name = desperateOverview_ui_workspace_display_name(wsid);
        g_strlcpy(g_built_active_names[i], name ? name : "", CORE_WS_NAME_LEN);
    }
    return window;
}

void close_overlay(void) {
    if (!g_overlay_window)
        return;

    desperateOverview_core_set_thumbnail_capture_enabled(false);
    desperateOverview_ui_cancel_drag_hold_timer();
    reset_interaction_state();
    g_overlay_content = NULL;
    g_current_preview = NULL;
    g_status_label = NULL;
    g_root_box = NULL;
    g_root_overlay = NULL;
    g_new_ws_target = NULL;

    if (g_fade_source_id) {
        g_source_remove(g_fade_source_id);
        g_fade_source_id = 0;
    }

    GtkWidget *window = g_overlay_window;
    g_overlay_window = NULL;
    g_overlay_visible = FALSE;
    gtk_widget_destroy(window);
    if (g_exit_on_hide)
        g_idle_add(gtk_quit_idle, NULL);
}

static void on_overlay_destroy(GtkWidget *widget, gpointer data) {
    (void)data;
    if (g_overlay_window == widget)
        g_overlay_window = NULL;
    g_overlay_visible = FALSE;
    g_status_label = NULL;
    g_root_box = NULL;
    g_root_overlay = NULL;
}

static GdkMonitor *match_monitor_for_window(GtkWidget *window) {
    GdkDisplay *display = gtk_widget_get_display(window);
    if (!display)
        return NULL;

    int eff_w = desperateOverview_ui_get_effective_mon_width();
    int eff_h = desperateOverview_ui_get_effective_mon_height();
    int monitors = gdk_display_get_n_monitors(display);
    for (int i = 0; i < monitors; ++i) {
        GdkMonitor *monitor = gdk_display_get_monitor(display, i);
        if (!monitor)
            continue;
        GdkRectangle rect;
        gdk_monitor_get_geometry(monitor, &rect);
        if (rect.x == g_mon_off_x &&
            rect.y == g_mon_off_y &&
            rect.width == eff_w &&
            rect.height == eff_h) {
            return monitor;
        }
    }

    return gdk_display_get_primary_monitor(display);
}

static void configure_layer_shell(GtkWindow *window) {
    gtk_layer_init_for_window(window);
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_namespace(window, G_LAYER_NAMESPACE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_exclusive_zone(window, 0);
    gtk_layer_set_keyboard_interactivity(window, TRUE);

    GdkMonitor *monitor = match_monitor_for_window(GTK_WIDGET(window));
    if (monitor)
        gtk_layer_set_monitor(window, monitor);
}

void desperateOverview_ui_init(const char *config_path) {
    g_mutex_init(&g_redraw_lock);
    ui_drag_init(&g_drag);
    desperateOverview_ui_live_init(handle_live_preview_ready, NULL);
    clear_ui_state();
    reset_interaction_state();
    config_init(config_path);
    desperateOverview_css_init();
    desperateOverview_thumb_cache_init();
    desperateOverview_core_set_thumbnail_capture_enabled(false);
}

void desperateOverview_ui_shutdown(void) {
    close_overlay();
    clear_ui_state();
    config_shutdown();
    desperateOverview_css_shutdown();
    desperateOverview_thumb_cache_shutdown();
    g_mutex_clear(&g_redraw_lock);
}

void desperateOverview_ui_sync_with_core(void) {
    copy_core_state_to_ui();
}

void desperateOverview_ui_request_show(void) {
    g_idle_add(show_overlay_idle, NULL);
}

void desperateOverview_ui_request_hide(void) {
    g_idle_add(hide_overlay_idle, NULL);
}

void desperateOverview_ui_request_quit(void) {
    g_idle_add(gtk_quit_idle, NULL);
}

void desperateOverview_ui_set_exit_on_hide(bool enabled) {
    g_exit_on_hide = enabled ? TRUE : FALSE;
}

bool desperateOverview_ui_is_visible(void) {
    return g_overlay_visible;
}


