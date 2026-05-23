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

int    g_active_workspace = 1;
WorkspaceWindows g_ws[MAX_WS];
int    g_active_list[MAX_WS];
int    g_active_count = 0;

GtkWidget *g_cells[MAX_WS];
GtkWidget *g_overlay_window = NULL;
GtkWidget *g_root_overlay = NULL;
GtkWidget *g_root_box = NULL;
GtkWidget *g_overlay_content = NULL;
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

WindowInfo *g_hover_window = NULL;
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
    for (int wsid = 1; wsid <= 9; ++wsid) {
        if (g_cells[wsid])
            gtk_widget_queue_draw(g_cells[wsid]);
    }
    if (g_overlay_window)
        gtk_widget_queue_draw(g_overlay_window);
}

const char *desperateOverview_ui_workspace_display_name(int wsid) {
    if (wsid <= 0 || wsid >= MAX_WS)
        return "";
    WorkspaceWindows *ws = &g_ws[wsid];
    if (!ws->name[0])
        g_snprintf(ws->name, CORE_WS_NAME_LEN, "%d", wsid);
    return ws->name;
}
