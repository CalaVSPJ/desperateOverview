#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "overview_core.h"
#include "overview_geometry.h"
#include "overview_types.h"
#include "overview_ui.h"

static int    g_mon_id         = 0;
static int    g_mon_width      = 1920;
static int    g_mon_height     = 1080;
static int    g_mon_off_x      = 0;
static int    g_mon_off_y      = 0;
static int    g_mon_transform  = 0;
static double g_aspect_ratio = 1920.0 / 1080.0;

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

static int    g_active_workspace = 1;

static WorkspaceWindows g_ws[MAX_WS];
static int              g_active_list[MAX_WS];
static int              g_active_count = 0;

static GtkWidget *g_cells[MAX_WS];
static GtkWidget *g_overlay_window = NULL;
static GtkWidget *g_root_overlay = NULL;
static GtkWidget *g_root_box = NULL;
static GtkWidget *g_overlay_content = NULL;
static GtkWidget *g_current_preview = NULL;
static struct {
    double x, y, w, h;
    gboolean valid;
} g_current_preview_rect = {0, 0, 0, 0, FALSE};
static int g_built_active_count = 0;
static int g_built_active_list[MAX_WS];
static char g_built_active_names[MAX_WS][CORE_WS_NAME_LEN];
static gboolean g_overlay_visible = FALSE;
static guint g_fade_source_id = 0;
static char g_active_names[MAX_WS][CORE_WS_NAME_LEN];

static gchar g_drag_target_name[] = "application/x-despoverview-window";
static GtkTargetEntry g_drag_targets[] = {
    { g_drag_target_name, GTK_TARGET_SAME_APP, 0 },
};

static WindowInfo *g_drag_active_window = NULL;
static int         g_drag_source_ws     = -1;
static gboolean    g_drag_in_progress   = FALSE;
static gboolean    g_pending_ws_click   = FALSE;
static gboolean    g_pending_window_click = FALSE;
static int         g_pending_ws_id      = -1;

static GMutex g_redraw_lock;
static gboolean g_redraw_pending = FALSE;

static const double G_OVERLAY_FRACTION = 0.33;
static const double G_PREVIEW_FRACTION = 0.85;
static const double G_GAP_PX           = 20.0;
static const char  *G_LAYER_NAMESPACE  = "despoverlay";
static const double G_WINDOW_BORDER_WIDTH = 2.0;
static const int    G_LIVE_PREVIEW_THREADS = 4;
static const double G_DRAG_HOLD_MOVE_THRESHOLD = 3.0;

typedef struct {
    GdkRGBA inactive_ws_border;
    GdkRGBA active_ws_border;
    GdkRGBA window_border;
    GdkRGBA inactive_ws_bg;
    GdkRGBA active_ws_bg;
    GdkRGBA overlay_bg;
    GdkRGBA drag_highlight;
    GdkRGBA new_ws_border;
    GdkRGBA new_ws_background;
    GdkRGBA new_ws_background_hover;
    double  workspace_corner_radius;
    double  window_corner_radius;
    guint   drag_hold_delay_ms;
} OverlayConfig;

static OverlayConfig g_config;
static gchar *g_config_override_path = NULL;

typedef struct {
    WindowInfo **wins;
    int count;
    int next_index;
    pthread_mutex_t lock;
} LiveCaptureQueue;

static guint g_drag_hold_source = 0;
static GtkWidget *g_drag_hold_widget = NULL;
static GdkEvent *g_drag_hold_event_copy = NULL;
static int g_drag_hold_wsid = -1;
static double g_drag_hold_start_x = 0.0;
static double g_drag_hold_start_y = 0.0;
static WindowInfo *g_hover_window = NULL;
static gboolean    g_hover_window_bottom = FALSE;
static GtkWidget  *g_status_label = NULL;
static GtkWidget  *g_new_ws_target = NULL;
static gboolean    g_new_ws_target_hover = FALSE;

static void overlay_config_reload(void);
static void overlay_config_set_defaults(OverlayConfig *cfg);
static void cairo_add_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double radius);
static void cairo_set_source_rgba_color(cairo_t *cr, const GdkRGBA *color);
static GdkRGBA lighten_color(const GdkRGBA *base, double delta);
static gboolean parse_color_string(const char *value, GdkRGBA *out);
static GdkPixbuf *orient_pixbuf_for_monitor(GdkPixbuf *src);
static void reset_interaction_state(void);
static void build_active_workspace_live_previews(void);
static gboolean on_overlay_window_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
static gboolean draw_current_workspace(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean point_inside_widget(GtkWidget *target, GtkWidget *relative_to, double px, double py);
static GtkWidget *build_overlay_window(void);
static void close_overlay(void);
static void on_overlay_destroy(GtkWidget *widget, gpointer data);
static gboolean show_overlay_idle(gpointer data);
static gboolean hide_overlay_idle(gpointer data);
static gboolean gtk_quit_idle(gpointer data);
static void queue_all_cells_redraw(void);
static GdkMonitor *match_monitor_for_window(GtkWidget *window);
static void configure_layer_shell(GtkWindow *window);
static void cache_window_preview(WindowInfo *win, double rx, double ry, double rw, double rh, gboolean bottom_view);
static WindowInfo *hit_test_window_view(int wsid, double px, double py, gboolean bottom_view);
static int resolve_workspace_id(gpointer data);
static gchar *build_window_hover_text(const WindowInfo *win);
static void draw_window_border(cairo_t *cr, double rx, double ry, double rw, double rh);
static void draw_window_placeholder(cairo_t *cr, double rx, double ry, double rw, double rh);
static void capture_live_preview_for_window(WindowInfo *win);
static void *live_capture_worker(void *data);
static gboolean on_cell_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data);
static void start_drag_hold_timer(GtkWidget *widget, GdkEventButton *event, int wsid);
static void cancel_drag_hold_timer(void);
static gboolean drag_hold_timeout_cb(gpointer data);
static gboolean on_cell_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data);
static void set_hover_window(WindowInfo *win, gboolean bottom_view);
static void build_overlay_content(GtkWidget *root_box);
static void rebuild_overlay_content(void);
static void refresh_active_workspace_view(int wsid);
static int find_first_free_workspace(void);
static gboolean draw_new_workspace_target(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean on_new_ws_drag_motion(GtkWidget *widget, GdkDragContext *context,
                                      gint x, gint y, guint time, gpointer data);
static void on_new_ws_drag_leave(GtkWidget *widget, GdkDragContext *context, guint time, gpointer data);
static gboolean on_new_ws_drag_drop(GtkWidget *widget, GdkDragContext *context,
                                    gint x, gint y, guint time, gpointer data);
static void prune_empty_workspaces(void);
static gboolean draw_cell(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean on_cell_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
static gboolean on_cell_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data);
static void on_cell_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer data);
static void on_cell_drag_end(GtkWidget *widget, GdkDragContext *context, gpointer data);
static void on_cell_drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data,
                                  guint info, guint time, gpointer data);
static void on_cell_drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                                       GtkSelectionData *selection_data, guint info, guint time, gpointer data);
static gboolean on_cell_drag_drop(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer data);
static void build_overlay_content(GtkWidget *root_box);
static void rebuild_overlay_content(void);

static gboolean parse_hex_byte_pair(const char *s, guint8 *out) {
    if (!g_ascii_isxdigit(s[0]) || !g_ascii_isxdigit(s[1]))
        return FALSE;
    int hi = g_ascii_xdigit_value(s[0]);
    int lo = g_ascii_xdigit_value(s[1]);
    if (hi < 0 || lo < 0)
        return FALSE;
    *out = (guint8)((hi << 4) | lo);
    return TRUE;
}

static gboolean parse_color_string(const char *value, GdkRGBA *out) {
    if (!value || !out)
        return FALSE;

    size_t len = strlen(value);
    if (value[0] == '#' && len == 7) { /* #RRGGBB */
        guint8 r, g, b;
        if (!parse_hex_byte_pair(value + 1, &r) ||
            !parse_hex_byte_pair(value + 3, &g) ||
            !parse_hex_byte_pair(value + 5, &b))
            return FALSE;
        out->red = r / 255.0;
        out->green = g / 255.0;
        out->blue = b / 255.0;
        out->alpha = 1.0;
        return TRUE;
    }

    if (value[0] == '#' && len == 9) { /* #RRGGBBAA */
        guint8 r, g, b, a;
        if (!parse_hex_byte_pair(value + 1, &r) ||
            !parse_hex_byte_pair(value + 3, &g) ||
            !parse_hex_byte_pair(value + 5, &b) ||
            !parse_hex_byte_pair(value + 7, &a))
            return FALSE;
        out->red = r / 255.0;
        out->green = g / 255.0;
        out->blue = b / 255.0;
        out->alpha = a / 255.0;
        return TRUE;
    }

    return gdk_rgba_parse(out, value);
}

static void cairo_add_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double radius) {
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

static void cairo_set_source_rgba_color(cairo_t *cr, const GdkRGBA *color) {
    cairo_set_source_rgba(cr, color->red, color->green, color->blue, color->alpha);
}

static GdkRGBA lighten_color(const GdkRGBA *base, double delta) {
    GdkRGBA out = *base;
    out.red = fmin(1.0, fmax(0.0, out.red + delta));
    out.green = fmin(1.0, fmax(0.0, out.green + delta));
    out.blue = fmin(1.0, fmax(0.0, out.blue + delta));
    out.alpha = fmin(1.0, fmax(0.0, out.alpha + delta * 0.5));
    return out;
}

static gboolean config_try_color(GKeyFile *kf, const char *section, const char *key, GdkRGBA *out) {
    g_autofree gchar *value = g_key_file_get_string(kf, section, key, NULL);
    if (!value)
        return FALSE;
    GdkRGBA tmp;
    if (!parse_color_string(value, &tmp))
        return FALSE;
    *out = tmp;
    return TRUE;
}

static void overlay_config_set_defaults(OverlayConfig *cfg) {
    gdk_rgba_parse(&cfg->inactive_ws_border, "#144344");
    cfg->inactive_ws_border.alpha = 1.0;
    gdk_rgba_parse(&cfg->active_ws_border, "#f2f2f9");
    cfg->active_ws_border.alpha = 0.95;
    gdk_rgba_parse(&cfg->window_border, "#144344");
    cfg->window_border.alpha = 0.85;
    gdk_rgba_parse(&cfg->inactive_ws_bg, "#1a1a1f");
    cfg->inactive_ws_bg.alpha = 0.95;
    gdk_rgba_parse(&cfg->active_ws_bg, "#282831");
    cfg->active_ws_bg.alpha = 0.95;
    gdk_rgba_parse(&cfg->overlay_bg, "#09090d");
    cfg->overlay_bg.alpha = 0.40;
    gdk_rgba_parse(&cfg->drag_highlight, "#ffcc33");
    cfg->drag_highlight.alpha = 0.9;
    gdk_rgba_parse(&cfg->new_ws_border, "#9ad0ff");
    cfg->new_ws_border.alpha = 0.9;
    gdk_rgba_parse(&cfg->new_ws_background, "#4d7399");
    cfg->new_ws_background.alpha = 0.85;
    cfg->new_ws_background_hover = lighten_color(&cfg->new_ws_background, 0.15);
    cfg->workspace_corner_radius = 10.0;
    cfg->window_corner_radius = 4.0;
    cfg->drag_hold_delay_ms = 150;
}

static gchar *overlay_config_default_path(void) {
    const char *config_dir = g_get_user_config_dir();
    if (!config_dir)
        return NULL;
    return g_build_filename(config_dir, "overviewApp", "config.ini", NULL);
}

static void overlay_config_load_from_file(const char *path, OverlayConfig *cfg) {
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
        g_warning("overview_ui: failed to load config '%s': %s", path, err ? err->message : "unknown error");
        if (err)
            g_error_free(err);
        g_key_file_unref(kf);
        return;
    }

    config_try_color(kf, "colors", "inactive_workspace_border", &cfg->inactive_ws_border);
    config_try_color(kf, "colors", "active_workspace_border", &cfg->active_ws_border);
    config_try_color(kf, "colors", "window_border", &cfg->window_border);
    config_try_color(kf, "colors", "inactive_workspace_background", &cfg->inactive_ws_bg);
    config_try_color(kf, "colors", "active_workspace_background", &cfg->active_ws_bg);
    config_try_color(kf, "colors", "overlay_background", &cfg->overlay_bg);
    if (config_try_color(kf, "colors", "drag_highlight", &cfg->drag_highlight)) {
        if (cfg->drag_highlight.alpha <= 0.0)
            cfg->drag_highlight.alpha = 0.9;
    }
    config_try_color(kf, "colors", "new_workspace_border", &cfg->new_ws_border);
    if (config_try_color(kf, "colors", "new_workspace_background", &cfg->new_ws_background)) {
        cfg->new_ws_background_hover = lighten_color(&cfg->new_ws_background, 0.15);
    }

    GError *local_err = NULL;
    double workspace_radius = g_key_file_get_double(kf, "layout", "workspace_corner_radius", &local_err);
    if (!local_err && workspace_radius >= 0.0)
        cfg->workspace_corner_radius = workspace_radius;
    if (local_err)
        g_clear_error(&local_err);

    double window_radius = g_key_file_get_double(kf, "layout", "window_corner_radius", &local_err);
    if (!local_err && window_radius >= 0.0)
        cfg->window_corner_radius = window_radius;
    if (local_err)
        g_clear_error(&local_err);

    gint drag_delay = g_key_file_get_integer(kf, "behavior", "drag_hold_delay_ms", &local_err);
    if (!local_err && drag_delay > 0)
        cfg->drag_hold_delay_ms = (guint)drag_delay;
    if (local_err)
        g_clear_error(&local_err);

    g_key_file_unref(kf);
}

static void overlay_config_reload(void) {
    OverlayConfig cfg;
    overlay_config_set_defaults(&cfg);

    gchar *path = NULL;
    if (g_config_override_path && *g_config_override_path) {
        path = g_strdup(g_config_override_path);
    } else {
        path = overlay_config_default_path();
        if (path && !g_file_test(path, G_FILE_TEST_EXISTS)) {
            g_free(path);
            path = NULL;
        }
    }

    if (path) {
        overlay_config_load_from_file(path, &cfg);
        g_free(path);
    }

    g_config = cfg;
}

static void clear_window_resources(WindowInfo *win) {
    if (!win)
        return;
    if (win->label) {
        g_free(win->label);
        win->label = NULL;
    }
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
    win->top_preview_valid = FALSE;
    win->bottom_preview_valid = FALSE;
}

static GdkPixbuf *orient_pixbuf_for_monitor(GdkPixbuf *src) {
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

static void reset_interaction_state(void) {
    g_drag_active_window = NULL;
    g_drag_source_ws = -1;
    g_drag_in_progress = FALSE;
    g_pending_ws_click = FALSE;
    g_pending_window_click = FALSE;
    g_pending_ws_id = -1;
    g_current_preview_rect.valid = FALSE;
    g_hover_window = NULL;
    g_hover_window_bottom = FALSE;
    if (g_status_label)
        gtk_label_set_text(GTK_LABEL(g_status_label), "");
    g_new_ws_target_hover = FALSE;
}

static void capture_live_preview_for_window(WindowInfo *win) {
    if (!win || !win->addr[0])
        return;

    char *b64 = core_capture_window_raw(win->addr);
    if (!b64) {
        g_warning("[overview] live preview capture failed for %s (no data)", win->addr);
        return;
    }

    gsize raw_len = 0;
    guchar *raw = g_base64_decode(b64, &raw_len);
    free(b64);
    if (!raw || raw_len == 0) {
        g_warning("[overview] live preview capture failed for %s (empty payload)", win->addr);
        if (raw)
            g_free(raw);
        return;
    }

    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    gboolean ok = gdk_pixbuf_loader_write(loader, raw, raw_len, NULL);
    if (ok)
        gdk_pixbuf_loader_close(loader, NULL);

    if (ok) {
        GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(loader);
        if (pb) {
            if (win->live_pixbuf)
                g_object_unref(win->live_pixbuf);
            win->live_pixbuf = g_object_ref(pb);
        } else {
            g_warning("[overview] live preview decode produced NULL pixbuf for %s", win->addr);
        }
    } else {
        g_warning("[overview] live preview decode failed for %s", win->addr);
        gdk_pixbuf_loader_close(loader, NULL);
    }

    g_object_unref(loader);
    g_free(raw);
}

static void cancel_drag_hold_timer(void) {
    if (g_drag_hold_source) {
        g_source_remove(g_drag_hold_source);
        g_drag_hold_source = 0;
    }
    if (g_drag_hold_event_copy) {
        gdk_event_free(g_drag_hold_event_copy);
        g_drag_hold_event_copy = NULL;
    }
    if (g_drag_hold_widget) {
        g_object_unref(g_drag_hold_widget);
        g_drag_hold_widget = NULL;
    }
    g_drag_hold_wsid = -1;
}

static gboolean drag_hold_timeout_cb(gpointer data) {
    (void)data;
    g_drag_hold_source = 0;
    if (!g_drag_hold_widget || !GTK_IS_WIDGET(g_drag_hold_widget) ||
        !g_drag_hold_event_copy || !g_drag_active_window) {
        cancel_drag_hold_timer();
        return G_SOURCE_REMOVE;
    }

    GdkEventButton *btn = (GdkEventButton *)g_drag_hold_event_copy;
    GtkTargetList *list = gtk_target_list_new(g_drag_targets,
                                              G_N_ELEMENTS(g_drag_targets));
    gtk_drag_begin_with_coordinates(
        g_drag_hold_widget,
        list,
        GDK_ACTION_MOVE,
        btn->button,
        g_drag_hold_event_copy,
        (int)btn->x,
        (int)btn->y
    );
    gtk_target_list_unref(list);

    queue_all_cells_redraw();
    cancel_drag_hold_timer();
    return G_SOURCE_REMOVE;
}

static void start_drag_hold_timer(GtkWidget *widget, GdkEventButton *event, int wsid) {
    cancel_drag_hold_timer();
    if (!widget || !event)
        return;

    g_drag_hold_widget = g_object_ref(widget);
    g_drag_hold_event_copy = gdk_event_copy((GdkEvent *)event);
    g_drag_hold_start_x = event->x;
    g_drag_hold_start_y = event->y;
    g_drag_hold_wsid = wsid;
    guint delay = g_config.drag_hold_delay_ms > 0 ? g_config.drag_hold_delay_ms : 150;
    g_drag_hold_source = g_timeout_add(delay, drag_hold_timeout_cb, NULL);
}

static void set_hover_window(WindowInfo *win, gboolean bottom_view) {
    if (!g_overlay_visible)
        return;
    if (win == g_hover_window && (win ? bottom_view : FALSE) == g_hover_window_bottom)
        return;
    g_hover_window = win;
    g_hover_window_bottom = win ? bottom_view : FALSE;
    if (g_status_label && GTK_IS_LABEL(g_status_label)) {
        gchar *text = win ? build_window_hover_text(win) : NULL;
        gtk_label_set_text(GTK_LABEL(g_status_label), text ? text : "");
        g_free(text);
    } else {
        g_status_label = NULL;
    }
}

static void clear_root_box_children(void) {
    if (!g_root_box)
        return;
    GList *children = gtk_container_get_children(GTK_CONTAINER(g_root_box));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
    g_overlay_content = NULL;
    g_current_preview = NULL;
    g_status_label = NULL;
    g_new_ws_target = NULL;
    for (int i = 0; i < MAX_WS; ++i)
        g_cells[i] = NULL;
}

static void build_overlay_content(GtkWidget *root_box) {
    if (!root_box)
        return;

    int base_w_px = get_effective_mon_width();
    int base_h_px = get_effective_mon_height();
    if (base_w_px <= 0) base_w_px = (g_mon_width > 0) ? g_mon_width : 1920;
    if (base_h_px <= 0) base_h_px = (g_mon_height > 0) ? g_mon_height : 1080;

    double overlay_h = (double)base_h_px * G_OVERLAY_FRACTION;
    double base_h    = overlay_h * G_PREVIEW_FRACTION;
    double base_w    = base_h * g_aspect_ratio;
    int count = g_active_count > 0 ? g_active_count : 1;
    double gap = G_GAP_PX;
    double avail_for_cells = (double)base_w_px - gap * (count - 1);
    if (avail_for_cells < 10.0) avail_for_cells = 10.0;
    const double ghost_ratio = 0.35;
    double total_cells_width = base_w * (count + ghost_ratio);
    double scale = 1.0;
    if (total_cells_width > avail_for_cells)
        scale = avail_for_cells / total_cells_width;
    double cell_w = base_w * scale;
    double cell_h = base_h * scale;

    GtkWidget *frame = gtk_event_box_new();
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_widget_set_vexpand(frame, FALSE);
    gtk_widget_set_size_request(frame, -1, (int)(overlay_h));
    gtk_box_pack_start(GTK_BOX(root_box), frame, FALSE, FALSE, 0);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, (int)gap);
    g_overlay_content = hbox;
    gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(hbox, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(frame), hbox);

    GtkWidget *current_preview = gtk_drawing_area_new();
    g_current_preview = current_preview;
    gtk_widget_set_hexpand(current_preview, TRUE);
    gtk_widget_set_vexpand(current_preview, TRUE);
    gtk_widget_set_halign(current_preview, GTK_ALIGN_FILL);
    gtk_widget_set_valign(current_preview, GTK_ALIGN_FILL);
    g_signal_connect(current_preview, "draw",
                     G_CALLBACK(draw_current_workspace), NULL);
    gtk_widget_add_events(current_preview,
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_BUTTON_MOTION_MASK |
                          GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(current_preview, "motion-notify-event",
                     G_CALLBACK(on_cell_motion), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "leave-notify-event",
                     G_CALLBACK(on_cell_leave), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "button-press-event",
                     G_CALLBACK(on_cell_button_press), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "button-release-event",
                     G_CALLBACK(on_cell_button_release), GINT_TO_POINTER(0));
    gtk_drag_source_set(current_preview,
                        GDK_BUTTON1_MASK,
                        g_drag_targets,
                        G_N_ELEMENTS(g_drag_targets),
                        GDK_ACTION_MOVE);
    g_signal_connect(current_preview, "drag-begin",
                     G_CALLBACK(on_cell_drag_begin), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "drag-end",
                     G_CALLBACK(on_cell_drag_end), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "drag-data-get",
                     G_CALLBACK(on_cell_drag_data_get), GINT_TO_POINTER(0));
    gtk_drag_dest_set(current_preview,
                      GTK_DEST_DEFAULT_MOTION |
                      GTK_DEST_DEFAULT_HIGHLIGHT |
                      GTK_DEST_DEFAULT_DROP,
                      g_drag_targets,
                      G_N_ELEMENTS(g_drag_targets),
                      GDK_ACTION_MOVE);
    g_signal_connect(current_preview, "drag-drop",
                     G_CALLBACK(on_cell_drag_drop), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "drag-data-received",
                     G_CALLBACK(on_cell_drag_data_received), GINT_TO_POINTER(0));
    gtk_box_pack_start(GTK_BOX(root_box), current_preview, TRUE, TRUE, 0);

    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(status_box, TRUE);
    gtk_widget_set_margin_bottom(status_box, 6);
    gtk_box_pack_start(GTK_BOX(root_box), status_box, FALSE, FALSE, 0);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(g_status_label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(g_status_label), "overview-status");
    gtk_box_pack_start(GTK_BOX(status_box), g_status_label, TRUE, TRUE, 0);

    for (int i = 0; i < g_active_count; ++i) {
        int wsid = g_active_list[i];
        GtkWidget *cell = gtk_drawing_area_new();
        g_cells[i] = cell;
        gtk_widget_set_size_request(cell,
                                    (int)(cell_w + 0.5),
                                    (int)(cell_h + 0.5));
        g_signal_connect(cell, "draw", G_CALLBACK(draw_cell),
                         GINT_TO_POINTER(wsid));
        gtk_widget_add_events(cell,
                              GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_BUTTON_MOTION_MASK |
                              GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect(cell, "motion-notify-event",
                         G_CALLBACK(on_cell_motion), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "leave-notify-event",
                         G_CALLBACK(on_cell_leave), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "button-press-event",
                         G_CALLBACK(on_cell_button_press), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "button-release-event",
                         G_CALLBACK(on_cell_button_release), GINT_TO_POINTER(wsid));
        gtk_drag_source_set(cell,
                            GDK_BUTTON1_MASK,
                            g_drag_targets,
                            G_N_ELEMENTS(g_drag_targets),
                            GDK_ACTION_MOVE);
        g_signal_connect(cell, "drag-begin",
                         G_CALLBACK(on_cell_drag_begin), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "drag-end",
                         G_CALLBACK(on_cell_drag_end), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "drag-data-get",
                         G_CALLBACK(on_cell_drag_data_get), GINT_TO_POINTER(wsid));
        gtk_drag_dest_set(cell,
                          GTK_DEST_DEFAULT_MOTION |
                          GTK_DEST_DEFAULT_HIGHLIGHT |
                          GTK_DEST_DEFAULT_DROP,
                          g_drag_targets,
                          G_N_ELEMENTS(g_drag_targets),
                          GDK_ACTION_MOVE);
        g_signal_connect(cell, "drag-drop",
                         G_CALLBACK(on_cell_drag_drop), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "drag-data-received",
                         G_CALLBACK(on_cell_drag_data_received), GINT_TO_POINTER(wsid));
        gtk_box_pack_start(GTK_BOX(hbox), cell, FALSE, FALSE, 0);
    }

    GtkWidget *ghost = gtk_drawing_area_new();
    g_new_ws_target = ghost;
    int ghost_width = (int)(cell_w * ghost_ratio + 0.5);
    gtk_widget_set_size_request(ghost, ghost_width, (int)(cell_h));
    gtk_widget_set_hexpand(ghost, FALSE);
    gtk_widget_set_margin_start(ghost, (int)(gap * 0.5));
    gtk_widget_add_events(ghost, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(ghost, "draw", G_CALLBACK(draw_new_workspace_target), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), ghost, FALSE, FALSE, 0);
    gtk_drag_dest_set(ghost,
                      GTK_DEST_DEFAULT_MOTION |
                      GTK_DEST_DEFAULT_HIGHLIGHT |
                      GTK_DEST_DEFAULT_DROP,
                      g_drag_targets,
                      G_N_ELEMENTS(g_drag_targets),
                      GDK_ACTION_MOVE);
    g_signal_connect(ghost, "drag-motion",
                     G_CALLBACK(on_new_ws_drag_motion), NULL);
    g_signal_connect(ghost, "drag-leave",
                     G_CALLBACK(on_new_ws_drag_leave), NULL);
    g_signal_connect(ghost, "drag-drop",
                     G_CALLBACK(on_new_ws_drag_drop), NULL);
    gtk_widget_queue_draw(ghost);

    for (int j = g_active_count; j < MAX_WS; ++j)
        g_cells[j] = NULL;
}

static void rebuild_overlay_content(void) {
    if (!g_overlay_window || !g_root_box)
        return;
    clear_root_box_children();
    build_overlay_content(g_root_box);
    gtk_widget_show_all(g_overlay_window);
    g_built_active_count = g_active_count;
    memcpy(g_built_active_list, g_active_list, sizeof(g_active_list));
    memcpy(g_built_active_names, g_active_names, sizeof(g_active_names));
}

static void refresh_active_workspace_view(int wsid) {
    if (wsid <= 0 || wsid >= MAX_WS)
        return;
    if (g_active_workspace != wsid)
        g_active_workspace = wsid;
    build_active_workspace_live_previews();
    set_hover_window(NULL, FALSE);
    queue_all_cells_redraw();
}

static int find_first_free_workspace(void) {
    for (int wsid = 1; wsid < MAX_WS; ++wsid) {
        gboolean used = FALSE;
        for (int i = 0; i < g_active_count; ++i) {
            if (g_active_list[i] == wsid) {
                used = TRUE;
                break;
            }
        }
        if (!used)
            return wsid;
    }
    return -1;
}

static void prune_empty_workspaces(void) {
    int write = 0;
    for (int read = 0; read < g_active_count; ++read) {
        int wsid = g_active_list[read];
        WorkspaceWindows *W = &g_ws[wsid];
        if (wsid == g_active_workspace || (W && W->count > 0)) {
            if (write != read) {
                g_active_list[write] = g_active_list[read];
                memcpy(g_active_names[write], g_active_names[read], CORE_WS_NAME_LEN);
            }
            ++write;
        }
    }
    if (write <= 0 && g_active_workspace > 0 && g_active_workspace < MAX_WS) {
        g_active_list[0] = g_active_workspace;
        snprintf(g_active_names[0], CORE_WS_NAME_LEN, "%d", g_active_workspace);
        write = 1;
    }
    g_active_count = write;
}

static gboolean draw_new_workspace_target(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    double x = 2.0;
    double y = 2.0;
    double w = alloc.width - 4.0;
    double h = alloc.height - 4.0;

    cairo_save(cr);
    cairo_add_rounded_rect(cr, x, y, w, h, g_config.workspace_corner_radius);

    if (g_new_ws_target_hover)
        cairo_set_source_rgba_color(cr, &g_config.new_ws_background_hover);
    else
        cairo_set_source_rgba_color(cr, &g_config.new_ws_background);
    cairo_fill_preserve(cr);

    cairo_set_line_width(cr, 2.5);
    cairo_set_source_rgba_color(cr, &g_config.new_ws_border);
    cairo_stroke(cr);

    cairo_set_line_width(cr, 4.0);
    cairo_set_source_rgba_color(cr, &g_config.new_ws_border);
    cairo_move_to(cr, x + w / 2.0, y + h * 0.25);
    cairo_line_to(cr, x + w / 2.0, y + h * 0.75);
    cairo_move_to(cr, x + w * 0.25, y + h / 2.0);
    cairo_line_to(cr, x + w * 0.75, y + h / 2.0);
    cairo_stroke(cr);

    cairo_restore(cr);
    return FALSE;
}

static gboolean on_new_ws_drag_motion(GtkWidget *widget, GdkDragContext *context,
                                      gint x, gint y, guint time, gpointer data) {
    (void)context; (void)x; (void)y; (void)time; (void)data;
    if (!g_new_ws_target_hover) {
        g_new_ws_target_hover = TRUE;
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

static void on_new_ws_drag_leave(GtkWidget *widget, GdkDragContext *context, guint time, gpointer data) {
    (void)context; (void)time; (void)data;
    if (g_new_ws_target_hover) {
        g_new_ws_target_hover = FALSE;
        gtk_widget_queue_draw(widget);
    }
}

static gboolean on_new_ws_drag_drop(GtkWidget *widget, GdkDragContext *context,
                                    gint x, gint y, guint time, gpointer data) {
    (void)x; (void)y; (void)data;
    g_new_ws_target_hover = FALSE;
    gtk_widget_queue_draw(widget);

    if (!g_drag_active_window || !g_drag_active_window->addr[0]) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return TRUE;
    }

    int free_ws = find_first_free_workspace();
    if (free_ws <= 0) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return TRUE;
    }

    gtk_drag_finish(context, TRUE, FALSE, time);
    core_move_window(g_drag_active_window->addr, free_ws);
    queue_all_cells_redraw();
    return TRUE;
}

static gboolean on_cell_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    gboolean bottom_view = (widget == g_current_preview);
    int wsid = resolve_workspace_id(data);
    WindowInfo *hit = hit_test_window_view(wsid, event->x, event->y, bottom_view);
    set_hover_window(hit, bottom_view);

    if (g_drag_hold_source && widget == g_drag_hold_widget) {
        double dx = fabs(event->x - g_drag_hold_start_x);
        double dy = fabs(event->y - g_drag_hold_start_y);
        if (dx > G_DRAG_HOLD_MOVE_THRESHOLD || dy > G_DRAG_HOLD_MOVE_THRESHOLD)
            cancel_drag_hold_timer();
    }
    return FALSE;
}

static gboolean on_cell_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
    (void)widget;
    (void)event;
    (void)data;
    if (!g_drag_in_progress)
        set_hover_window(NULL, FALSE);
    return FALSE;
}

static void *live_capture_worker(void *data) {
    LiveCaptureQueue *queue = (LiveCaptureQueue *)data;
    while (true) {
        pthread_mutex_lock(&queue->lock);
        int idx = queue->next_index++;
        pthread_mutex_unlock(&queue->lock);

        if (idx >= queue->count)
            break;

        WindowInfo *win = queue->wins[idx];
        capture_live_preview_for_window(win);
    }
    return NULL;
}

static void build_active_workspace_live_previews(void) {
    if (g_active_workspace <= 0 || g_active_workspace >= MAX_WS)
        return;
    WorkspaceWindows *W = &g_ws[g_active_workspace];
    if (!W)
        return;

    WindowInfo *targets[MAX_WINS_PER_WS];
    int target_count = 0;
    for (int i = 0; i < W->count && target_count < MAX_WINS_PER_WS; ++i) {
        WindowInfo *win = &W->wins[i];
        if (!win || !win->addr[0])
            continue;
        targets[target_count++] = win;
    }

    if (target_count <= 0)
        return;

    if (target_count == 1) {
        capture_live_preview_for_window(targets[0]);
        return;
    }

    LiveCaptureQueue queue = {
        .wins = targets,
        .count = target_count,
        .next_index = 0,
    };
    pthread_mutex_init(&queue.lock, NULL);

    int thread_count = target_count < G_LIVE_PREVIEW_THREADS
                       ? target_count
                       : G_LIVE_PREVIEW_THREADS;
    pthread_t threads[G_LIVE_PREVIEW_THREADS];
    memset(threads, 0, sizeof(threads));

    for (int i = 0; i < thread_count; ++i) {
        if (pthread_create(&threads[i], NULL, live_capture_worker, &queue) != 0) {
            threads[i] = 0;
            capture_live_preview_for_window(targets[queue.next_index++]);
        }
    }

    for (int i = 0; i < thread_count; ++i) {
        if (threads[i])
            pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&queue.lock);
}

static void clear_ui_state(void) {
    for (int wsid = 0; wsid < MAX_WS; ++wsid) {
        WorkspaceWindows *W = &g_ws[wsid];
        for (int i = 0; i < W->count; ++i) {
            clear_window_resources(&W->wins[i]);
        }
        W->count = 0;
    }
    g_active_count = 0;
    memset(g_active_list, 0, sizeof(g_active_list));
    memset(g_active_names, 0, sizeof(g_active_names));
}

static void copy_core_state_to_ui(void) {
    CoreState snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    core_copy_state(&snapshot);

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
    memcpy(g_active_names, snapshot.active_names, sizeof(g_active_names));
    g_current_preview_rect.valid = FALSE;
    int eff_w = get_effective_mon_width();
    int eff_h = get_effective_mon_height();
    g_aspect_ratio = (eff_w > 0 && eff_h > 0)
                     ? (double)eff_w / (double)eff_h
                     : 16.0 / 9.0;

    for (int wsid = 0; wsid < MAX_WS; ++wsid) {
        CoreWorkspace *src_ws = &snapshot.workspaces[wsid];
        WorkspaceWindows *dst_ws = &g_ws[wsid];
        dst_ws->count = src_ws->count;
        for (int j = 0; j < src_ws->count; ++j) {
            CoreWindow *src = &src_ws->wins[j];
            WindowInfo *dst = &dst_ws->wins[j];
            dst->x = src->x;
            dst->y = src->y;
            dst->w = src->w;
            dst->h = src->h;
            strncpy(dst->addr, src->addr, sizeof(dst->addr));
            dst->addr[sizeof(dst->addr) - 1] = '\0';
            dst->label = src->label ? g_strdup(src->label) : NULL;
            dst->thumb_pixbuf = NULL;
            dst->live_pixbuf = NULL;
            dst->thumb_b64 = src->thumb_b64 ? g_strdup(src->thumb_b64) : NULL;
            dst->class_name = src->class_name ? g_strdup(src->class_name) : NULL;
            dst->initial_class = src->initial_class ? g_strdup(src->initial_class) : NULL;
            dst->title = src->title ? g_strdup(src->title) : NULL;
            dst->top_preview_valid = FALSE;
            dst->bottom_preview_valid = FALSE;

            if (src->thumb_b64 && src->thumb_b64[0]) {
                gsize raw_len = 0;
                guchar *raw = g_base64_decode(src->thumb_b64, &raw_len);
                if (raw && raw_len > 0) {
                    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
                    if (gdk_pixbuf_loader_write(loader, raw, raw_len, NULL)) {
                        gdk_pixbuf_loader_close(loader, NULL);
                        GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(loader);
                        if (pb)
                            dst->thumb_pixbuf = g_object_ref(pb);
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

    build_active_workspace_live_previews();

    core_free_state(&snapshot);
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

static gboolean draw_current_workspace(GtkWidget *widget, cairo_t *cr, gpointer data) {
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
    overview_geometry_compute_preview_transform(
        eff_w, eff_h, width, height, &transform);

    cairo_save(cr);
    cairo_add_rounded_rect(cr,
                           transform.offset_x,
                           transform.offset_y,
                           transform.view_w,
                           transform.view_h,
                           g_config.workspace_corner_radius);
    cairo_set_source_rgba_color(cr, &g_config.active_ws_bg);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 3.0);
    cairo_set_source_rgba_color(cr, &g_config.active_ws_border);
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
        overview_geometry_window_to_normalized(
            g_mon_width, g_mon_height, g_mon_off_x, g_mon_off_y, g_mon_transform,
            win->x, win->y, win->w, win->h, &norm);

        double rx = transform.offset_x + norm.x * transform.view_w;
        double ry = transform.offset_y + norm.y * transform.view_h;
        double rw = norm.w * transform.view_w;
        double rh = norm.h * transform.view_h;

        rw = overview_geometry_clamp(rw, 2.0, transform.view_w);
        rh = overview_geometry_clamp(rh, 2.0, transform.view_h);

        cache_window_preview(win, rx, ry, rw, rh, TRUE);

        cairo_save(cr);
        cairo_add_rounded_rect(cr, rx, ry, rw, rh, g_config.window_corner_radius);
        cairo_clip(cr);

        GdkPixbuf *preview = win->live_pixbuf ? win->live_pixbuf : win->thumb_pixbuf;
        GdkPixbuf *oriented = orient_pixbuf_for_monitor(preview);
        int target_w = (int)ceil(rw);
        int target_h = (int)ceil(rh);
        if (target_w <= 0) target_w = 1;
        if (target_h <= 0) target_h = 1;

        gboolean drew_texture = FALSE;
        if (oriented) {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
                oriented,
                target_w,
                target_h,
                GDK_INTERP_BILINEAR
            );
            if (scaled) {
                cairo_save(cr);
                cairo_rectangle(cr, rx, ry, rw, rh);
                cairo_clip(cr);
                gdk_cairo_set_source_pixbuf(cr, scaled, rx, ry);
                cairo_paint(cr);
                cairo_restore(cr);
                g_object_unref(scaled);
                drew_texture = TRUE;
            }
            g_object_unref(oriented);
        }

        if (!drew_texture) {
            cairo_save(cr);
            cairo_add_rounded_rect(cr, rx, ry, rw, rh, g_config.window_corner_radius);
            cairo_clip(cr);
            draw_window_placeholder(cr, rx, ry, rw, rh);
            cairo_restore(cr);
        }

        if (g_drag_in_progress && g_drag_active_window == win) {
            cairo_save(cr);
            cairo_add_rounded_rect(cr, rx, ry, rw, rh, g_config.window_corner_radius);
            double fill_alpha = fmin(1.0, g_config.drag_highlight.alpha * 0.35);
            cairo_set_source_rgba(cr,
                                  g_config.drag_highlight.red,
                                  g_config.drag_highlight.green,
                                  g_config.drag_highlight.blue,
                                  fill_alpha);
            cairo_fill_preserve(cr);
            double dash[] = {6.0, 4.0};
            cairo_set_dash(cr, dash, 2, 0);
            cairo_set_line_width(cr, 3.0);
            cairo_set_source_rgba_color(cr, &g_config.drag_highlight);
            cairo_stroke(cr);
            cairo_restore(cr);
        }
        draw_window_border(cr, rx, ry, rw, rh);
        cairo_restore(cr);
    }

    cairo_restore(cr);
    return TRUE;
}

static void queue_all_cells_redraw(void) {
    for (int i = 0; i < g_active_count; ++i) {
        if (g_cells[i])
            gtk_widget_queue_draw(g_cells[i]);
    }
    if (g_overlay_window)
        gtk_widget_queue_draw(g_overlay_window);
    if (g_current_preview)
        gtk_widget_queue_draw(g_current_preview);
}

static gboolean show_overlay_idle(gpointer data) {
    (void)data;
    if (g_overlay_visible)
        return G_SOURCE_REMOVE;
    copy_core_state_to_ui();
    prune_empty_workspaces();
    g_overlay_window = build_overlay_window();
    g_overlay_visible = TRUE;
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

static int find_active_index(int wsid) {
    for (int i = 0; i < g_active_count; ++i) {
        if (g_active_list[i] == wsid)
            return i;
    }
    return -1;
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
        if (strncmp(g_built_active_names[i], g_active_names[i],
                    CORE_WS_NAME_LEN) != 0)
            return TRUE;
    }
    int active_idx = find_active_index(g_active_workspace);
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
        rebuild_overlay_content();
    } else {
        queue_all_cells_redraw();
    }
    return G_SOURCE_REMOVE;
}

void overview_ui_core_redraw_callback(void *user_data) {
    (void)user_data;
    g_mutex_lock(&g_redraw_lock);
    if (!g_redraw_pending) {
        g_redraw_pending = TRUE;
        g_idle_add(overlay_idle_redraw, NULL);
    }
    g_mutex_unlock(&g_redraw_lock);
}

static void set_drag_icon_from_window(GdkDragContext *context, WindowInfo *win) {
    if (!context || !win)
        return;

    GdkPixbuf *icon = NULL;

    if (win->thumb_pixbuf) {
        GdkPixbuf *source = orient_pixbuf_for_monitor(win->thumb_pixbuf);
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

            target_w = overview_geometry_clamp_int(target_w, min_dim, max_dim);
            target_h = overview_geometry_clamp_int(target_h, min_dim, max_dim);

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
        }
        if (source)
            g_object_unref(source);
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

static gboolean draw_background(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    int eff_w = get_effective_mon_width();
    int eff_h = get_effective_mon_height();
    double width = alloc.width > 0 ? alloc.width : eff_w;
    double height = alloc.height > 0 ? alloc.height : eff_h;
    cairo_set_source_rgba_color(cr, &g_config.overlay_bg);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
    return TRUE;
}

static gboolean draw_cell(GtkWidget *widget, cairo_t *cr, gpointer data) {
    int wsid = GPOINTER_TO_INT(data);

    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    double W = a.width;
    double H = a.height;

    cairo_save(cr);
    cairo_add_rounded_rect(cr, 2.0, 2.0, W - 4.0, H - 4.0, g_config.workspace_corner_radius);
    if (wsid == g_active_workspace)
        cairo_set_source_rgba_color(cr, &g_config.active_ws_bg);
    else
        cairo_set_source_rgba_color(cr, &g_config.inactive_ws_bg);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 3.0);
    if (wsid == g_active_workspace)
        cairo_set_source_rgba_color(cr, &g_config.active_ws_border);
    else
        cairo_set_source_rgba_color(cr, &g_config.inactive_ws_border);
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
        overview_geometry_window_to_normalized(
            g_mon_width, g_mon_height, g_mon_off_x, g_mon_off_y, g_mon_transform,
            win->x, win->y, win->w, win->h, &norm);

        double rx = ix + norm.x * inner_w;
        double ry = iy + norm.y * inner_h;
        double rw = norm.w * inner_w;
        double rh = norm.h * inner_h;

        rw = overview_geometry_clamp(rw, 5.0, inner_w);
        rh = overview_geometry_clamp(rh, 5.0, inner_h);

        if (rx < ix) rx = ix;
        if (ry < iy) ry = iy;
        if (rx + rw > ix + inner_w) rw = (ix + inner_w) - rx;
        if (ry + rh > iy + inner_h) rh = (iy + inner_h) - ry;

        if (rw <= 0 || rh <= 0)
            continue;

        cache_window_preview(win, rx, ry, rw, rh, FALSE);

        gboolean drew_texture = FALSE;
        if (win->thumb_pixbuf) {
            GdkPixbuf *oriented = orient_pixbuf_for_monitor(win->thumb_pixbuf);
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
                    cairo_add_rounded_rect(cr, rx, ry, rw, rh, g_config.window_corner_radius);
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
            cairo_add_rounded_rect(cr, rx, ry, rw, rh, g_config.window_corner_radius);
            cairo_clip(cr);
            draw_window_placeholder(cr, rx, ry, rw, rh);
            cairo_restore(cr);
        }
        if (g_drag_in_progress && g_drag_active_window == win) {
            cairo_save(cr);
            cairo_add_rounded_rect(cr, rx, ry, rw, rh, g_config.window_corner_radius);
            double fill_alpha = fmin(1.0, g_config.drag_highlight.alpha * 0.35);
            cairo_set_source_rgba(cr,
                                  g_config.drag_highlight.red,
                                  g_config.drag_highlight.green,
                                  g_config.drag_highlight.blue,
                                  fill_alpha);
            cairo_fill_preserve(cr);
            double dash[] = {6.0, 4.0};
            cairo_set_dash(cr, dash, 2, 0);
            cairo_set_line_width(cr, 3.0);
            cairo_set_source_rgba_color(cr, &g_config.drag_highlight);
            cairo_stroke(cr);
            cairo_restore(cr);
        }
        draw_window_border(cr, rx, ry, rw, rh);
    }

out_restore_cell:
    cairo_restore(cr);
    return TRUE;
}

static gboolean on_cell_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button != GDK_BUTTON_PRIMARY)
        return FALSE;

    cancel_drag_hold_timer();
    int wsid = resolve_workspace_id(data);
    gboolean bottom_view = (widget == g_current_preview);
    WindowInfo *hit = hit_test_window_view(wsid, event->x, event->y, bottom_view);

    if (hit) {
        g_drag_active_window = hit;
        g_drag_source_ws = wsid;
        g_pending_window_click = TRUE;
        g_pending_ws_click = FALSE;
        g_pending_ws_id = wsid;
        start_drag_hold_timer(widget, event, wsid);
    } else {
        g_drag_active_window = NULL;
        g_drag_source_ws = -1;
        g_pending_ws_click = TRUE;
        g_pending_window_click = FALSE;
        g_pending_ws_id = wsid;
    }

    return TRUE;
}

static gboolean on_cell_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    if (event->button != GDK_BUTTON_PRIMARY)
        return FALSE;

    cancel_drag_hold_timer();

    int wsid = resolve_workspace_id(data);
    gboolean bottom_view = (widget == g_current_preview);

    if (!g_drag_in_progress &&
        g_pending_window_click &&
        wsid == g_pending_ws_id &&
        !bottom_view) {
        g_pending_window_click = FALSE;
        g_pending_ws_click = FALSE;
        g_pending_ws_id = -1;
        int idx = find_active_index(wsid);
        refresh_active_workspace_view(wsid);
        if (idx >= 0)
            core_switch_workspace(g_active_names[idx], wsid);
        return TRUE;
    }

    if (!g_drag_in_progress &&
        g_pending_ws_click &&
        wsid == g_pending_ws_id &&
        !bottom_view) {
        g_pending_ws_click = FALSE;
        g_pending_window_click = FALSE;
        g_pending_ws_id = -1;
        int idx = find_active_index(wsid);
        refresh_active_workspace_view(wsid);
        if (idx >= 0)
            core_switch_workspace(g_active_names[idx], wsid);
        return TRUE;
    }

    if (!g_drag_in_progress && bottom_view) {
        g_pending_ws_click = FALSE;
        g_pending_window_click = FALSE;
        g_pending_ws_id = -1;
        int idx = find_active_index(wsid);
        close_overlay();
        if (idx >= 0)
            core_switch_workspace(g_active_names[idx], wsid);
        return TRUE;
    }

    if (!g_drag_in_progress) {
        g_drag_active_window = NULL;
        g_drag_source_ws = -1;
    }

    g_pending_ws_click = FALSE;
    g_pending_ws_id = -1;
    g_pending_window_click = FALSE;
    return TRUE;
}

static void on_cell_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer data) {
    (void)widget;
    int wsid = resolve_workspace_id(data);

    cancel_drag_hold_timer();

    if (!g_drag_active_window || wsid != g_drag_source_ws) {
        gdk_drag_abort(context, GDK_CURRENT_TIME);
        return;
    }

    g_drag_in_progress = TRUE;
    g_pending_ws_click = FALSE;
    g_pending_window_click = FALSE;
    g_pending_ws_id = -1;
    set_drag_icon_from_window(context, g_drag_active_window);
    queue_all_cells_redraw();
}

static void on_cell_drag_end(GtkWidget *widget, GdkDragContext *context, gpointer data) {
    (void)widget; (void)context; (void)data;
    cancel_drag_hold_timer();
    g_drag_in_progress = FALSE;
    g_drag_active_window = NULL;
    g_drag_source_ws = -1;
    g_pending_ws_click = FALSE;
    g_pending_window_click = FALSE;
    g_pending_ws_id = -1;
    queue_all_cells_redraw();
}

static void on_cell_drag_data_get(GtkWidget *widget,
                                  GdkDragContext *context,
                                  GtkSelectionData *selection_data,
                                  guint info,
                                  guint time,
                                  gpointer data) {
    (void)widget; (void)context; (void)info; (void)time; (void)data;

    if (!g_drag_active_window || !g_drag_active_window->addr[0])
        return;

    gtk_selection_data_set(
        selection_data,
        gdk_atom_intern_static_string(g_drag_target_name),
        8,
        (const guchar *)g_drag_active_window->addr,
        (gint)(strlen(g_drag_active_window->addr) + 1)
    );
}

static void on_cell_drag_data_received(GtkWidget *widget,
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
                core_move_window(addr, target_ws);
                success = TRUE;
            }
        }
    }

    gtk_drag_finish(context, success, FALSE, time);

    if (success)
        queue_all_cells_redraw();
}

static gboolean on_cell_drag_drop(GtkWidget *widget,
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

static gboolean on_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget; (void)data;
    if (event->keyval == GDK_KEY_Escape) {
        close_overlay();
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_KP_Left ||
        event->keyval == GDK_KEY_Right || event->keyval == GDK_KEY_KP_Right) {
        if (g_active_count <= 0)
            return TRUE;

        int current_idx = find_active_index(g_active_workspace);
        if (current_idx < 0)
            current_idx = 0;

        int target_idx = current_idx;
        if (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_KP_Left) {
            target_idx = (current_idx - 1 + g_active_count) % g_active_count;
        } else {
            target_idx = (current_idx + 1) % g_active_count;
        }

        int target_ws = g_active_list[target_idx];
        refresh_active_workspace_view(target_ws);
        core_switch_workspace(g_active_names[target_idx], target_ws);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        if (g_active_workspace > 0) {
            int idx = find_active_index(g_active_workspace);
            close_overlay();
            if (idx >= 0)
                core_switch_workspace(g_active_names[idx], g_active_workspace);
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean fade_in_cb(gpointer data) {
    GtkWidget *window = GTK_WIDGET(data);
    if (!GTK_IS_WIDGET(window)) {
        g_fade_source_id = 0;
        return FALSE;
    }
    double op = gtk_widget_get_opacity(window);
    op += 0.08;
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
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), NULL);
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
    g_signal_connect(bg, "draw", G_CALLBACK(draw_background), NULL);
    gtk_container_add(GTK_CONTAINER(root_overlay), bg);

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_overlay_add_overlay(GTK_OVERLAY(root_overlay), root_box);
    g_root_box = root_box;

    build_overlay_content(root_box);

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
    memcpy(g_built_active_names, g_active_names, sizeof(g_active_names));
    return window;
}

static void close_overlay(void) {
    if (!g_overlay_window)
        return;

    cancel_drag_hold_timer();
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

    int eff_w = get_effective_mon_width();
    int eff_h = get_effective_mon_height();
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

static void cache_window_preview(WindowInfo *win, double rx, double ry, double rw, double rh, gboolean bottom_view) {
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

static int resolve_workspace_id(gpointer data) {
    int wsid = GPOINTER_TO_INT(data);
    if (wsid == 0)
        wsid = g_active_workspace;
    return wsid;
}

static WindowInfo *hit_test_window_view(int wsid, double px, double py, gboolean bottom_view) {
    if (wsid <= 0 || wsid >= MAX_WS)
        return NULL;

    WorkspaceWindows *W = &g_ws[wsid];
    for (int i = 0; i < W->count; ++i) {
        WindowInfo *win = &W->wins[i];
        double x0, y0, w, h;
        gboolean valid = bottom_view ? win->bottom_preview_valid : win->top_preview_valid;
        if (!valid)
            continue;
        if (bottom_view) {
            x0 = win->bottom_preview_x;
            y0 = win->bottom_preview_y;
            w = win->bottom_preview_w;
            h = win->bottom_preview_h;
        } else {
            x0 = win->top_preview_x;
            y0 = win->top_preview_y;
            w = win->top_preview_w;
            h = win->top_preview_h;
        }
        double x1 = x0 + w;
        double y1 = y0 + h;
        if (px >= x0 && px <= x1 && py >= y0 && py <= y1)
            return win;
    }
    return NULL;
}

static gchar *build_window_hover_text(const WindowInfo *win) {
    if (!win)
        return NULL;
    const char *text = NULL;
    if (win->class_name && *win->class_name)
        text = win->class_name;
    else if (win->initial_class && *win->initial_class)
        text = win->initial_class;
    else if (win->title && *win->title)
        text = win->title;
    else if (win->label)
        text = win->label;
    if (!text || !*text)
        return NULL;
    return g_strdup(text);
}

static void draw_window_border(cairo_t *cr, double rx, double ry, double rw, double rh) {
    cairo_save(cr);
    cairo_add_rounded_rect(cr, rx + 0.5, ry + 0.5, rw - 1.0, rh - 1.0, g_config.window_corner_radius);
    cairo_set_source_rgba_color(cr, &g_config.window_border);
    cairo_set_line_width(cr, G_WINDOW_BORDER_WIDTH);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void draw_window_placeholder(cairo_t *cr, double rx, double ry, double rw, double rh) {
    cairo_save(cr);
    cairo_add_rounded_rect(cr, rx, ry, rw, rh, g_config.window_corner_radius);

    cairo_pattern_t *pattern = cairo_pattern_create_linear(rx, ry, rx + rw, ry + rh);
    cairo_pattern_add_color_stop_rgba(pattern, 0.0, 0.16, 0.24, 0.31, 0.65);
    cairo_pattern_add_color_stop_rgba(pattern, 1.0, 0.10, 0.16, 0.21, 0.65);
    cairo_set_source(cr, pattern);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(pattern);

    cairo_set_line_width(cr, 1.5);
    cairo_set_source_rgba_color(cr, &g_config.window_border);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void overview_ui_init(const char *config_path) {
    g_mutex_init(&g_redraw_lock);
    clear_ui_state();
    reset_interaction_state();
    g_free(g_config_override_path);
    g_config_override_path = config_path ? g_strdup(config_path) : NULL;
    overlay_config_reload();
}

void overview_ui_shutdown(void) {
    close_overlay();
    clear_ui_state();
    g_free(g_config_override_path);
    g_config_override_path = NULL;
    g_mutex_clear(&g_redraw_lock);
}

void overview_ui_sync_with_core(void) {
    copy_core_state_to_ui();
}

void overview_ui_request_show(void) {
    g_idle_add(show_overlay_idle, NULL);
}

void overview_ui_request_hide(void) {
    g_idle_add(hide_overlay_idle, NULL);
}

void overview_ui_request_quit(void) {
    g_idle_add(gtk_quit_idle, NULL);
}

bool overview_ui_is_visible(void) {
    return g_overlay_visible;
}


