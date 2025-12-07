#define _GNU_SOURCE

#include "desperateOverview_ui_state.h"

#include <gtk/gtk.h>
#include <string.h>

#include "desperateOverview_config.h"

int    g_mon_id         = 0;
int    g_mon_width      = 1920;
int    g_mon_height     = 1080;
int    g_mon_off_x      = 0;
int    g_mon_off_y      = 0;
int    g_mon_transform  = 0;
double g_aspect_ratio   = 1920.0 / 1080.0;

int    g_active_workspace = 1;
WorkspaceWindows g_ws[MAX_WS];
int    g_active_list[MAX_WS];
int    g_active_count = 0;

GtkWidget *g_cells[MAX_WS];
GtkWidget *g_overlay_window = NULL;
GtkWidget *g_root_overlay = NULL;
GtkWidget *g_root_box = NULL;
GtkWidget *g_overlay_content = NULL;
GtkWidget *g_current_preview = NULL;
DesperateOverviewPreviewRect g_current_preview_rect = {0, 0, 0, 0, FALSE};
int g_built_active_count = 0;
int g_built_active_list[MAX_WS];
char g_built_active_names[MAX_WS][CORE_WS_NAME_LEN];
gboolean g_overlay_visible = FALSE;
guint g_fade_source_id = 0;

const gchar g_drag_target_name[] = "application/x-desperateoverview-window";
const GtkTargetEntry g_drag_targets[] = {
    { (gchar *)g_drag_target_name, GTK_TARGET_SAME_APP, 0 },
};
const size_t g_drag_targets_count = G_N_ELEMENTS(g_drag_targets);

DragState g_drag;

GMutex g_redraw_lock;
gboolean g_redraw_pending = FALSE;

gboolean g_force_decode_thumbs = FALSE;
gboolean g_force_live_previews = FALSE;
WindowInfo *g_hover_window = NULL;
gboolean    g_hover_window_bottom = FALSE;
GtkWidget  *g_status_label = NULL;
GtkWidget  *g_new_ws_target = NULL;
gboolean    g_new_ws_target_hover = FALSE;
const char *G_LAYER_NAMESPACE = "despoverlay";

static gboolean monitor_transform_is_rotated(void) {
    return (g_mon_transform % 2) != 0;
}

int desperateOverview_ui_get_effective_mon_width(void) {
    if (monitor_transform_is_rotated() && g_mon_height > 0)
        return g_mon_height;
    return g_mon_width;
}

int desperateOverview_ui_get_effective_mon_height(void) {
    if (monitor_transform_is_rotated() && g_mon_width > 0)
        return g_mon_width;
    return g_mon_height;
}

void desperateOverview_ui_queue_cells_redraw(void) {
    for (int i = 0; i < g_active_count; ++i) {
        if (g_cells[i])
            gtk_widget_queue_draw(g_cells[i]);
    }
    if (g_overlay_window)
        gtk_widget_queue_draw(g_overlay_window);
    if (g_current_preview)
        gtk_widget_queue_draw(g_current_preview);
}

int desperateOverview_ui_find_active_index(int wsid) {
    for (int i = 0; i < g_active_count; ++i) {
        if (g_active_list[i] == wsid)
            return i;
    }
    return -1;
}

const char *desperateOverview_ui_workspace_display_name(int wsid) {
    if (wsid <= 0 || wsid >= MAX_WS)
        return "";
    WorkspaceWindows *ws = &g_ws[wsid];
    if (!ws->name[0])
        g_snprintf(ws->name, CORE_WS_NAME_LEN, "%d", wsid);
    return ws->name;
}


