#ifndef DESPERATEOVERVIEW_UI_STATE_H
#define DESPERATEOVERVIEW_UI_STATE_H

#include <gtk/gtk.h>
#include <stddef.h>

#include "desperateOverview_core.h"
#include "desperateOverview_types.h"
#include "desperateOverview_ui_drag.h"

extern int    g_mon_id;
extern int    g_mon_width;
extern int    g_mon_height;
extern int    g_mon_off_x;
extern int    g_mon_off_y;
extern int    g_mon_transform;

extern int    g_active_workspace;
extern WorkspaceWindows g_ws[MAX_WS];
extern int    g_active_list[MAX_WS];
extern int    g_active_count;

extern GtkWidget *g_cells[MAX_WS];
extern GtkWidget *g_overlay_window;
extern GtkWidget *g_root_overlay;
extern GtkWidget *g_root_box;
extern GtkWidget *g_overlay_content;
extern gboolean g_overlay_visible;
extern guint g_fade_source_id;

extern const gchar g_drag_target_name[];
extern const GtkTargetEntry g_drag_targets[];
extern const size_t g_drag_targets_count;

void desperateOverview_ui_queue_cells_redraw(void);
void desperateOverview_ui_refresh_active_workspace_view(int wsid);
const char *desperateOverview_ui_workspace_display_name(int wsid);
int desperateOverview_ui_get_effective_mon_width(void);
int desperateOverview_ui_get_effective_mon_height(void);
extern DragState g_drag;

extern GMutex g_redraw_lock;
extern gboolean g_redraw_pending;

extern WindowInfo *g_hover_window;

extern const char  *G_LAYER_NAMESPACE;

#endif /* DESPERATEOVERVIEW_UI_STATE_H */

