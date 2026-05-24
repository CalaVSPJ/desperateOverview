#define _GNU_SOURCE

#include "desperateOverview_ui_layout.h"

#include <gtk/gtk.h>

#include "desperateOverview_config.h"
#include "desperateOverview_ui_state.h"
#include "desperateOverview_ui_render.h"
#include "desperateOverview_ui_events.h"

void desperateOverview_ui_build_overlay_content(GtkWidget *root_box) {
    if (!root_box)
        return;

    const OverlayConfig *cfg = config_get();
    int grid_cols = (int)cfg->grid_cols;
    if (grid_cols <= 0) grid_cols = 1;

    int margin = (int)cfg->grid_margin;
    int gap    = (int)cfg->grid_gap;

    GtkWidget *grid = gtk_grid_new();
    g_overlay_content = grid;
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(grid), gap);
    gtk_grid_set_column_spacing(GTK_GRID(grid), gap);
    gtk_widget_set_margin_start(grid, margin);
    gtk_widget_set_margin_end(grid, margin);
    gtk_widget_set_margin_top(grid, margin);
    gtk_widget_set_margin_bottom(grid, margin);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_widget_set_vexpand(grid, TRUE);
    gtk_box_pack_start(GTK_BOX(root_box), grid, TRUE, TRUE, 0);

    for (int i = 0; i < MAX_WS; ++i)
        g_cells[i] = NULL;

    for (guint slot = 0; slot < cfg->workspace_count; ++slot) {
        int wsid = cfg->workspace_ids[slot];
        if (wsid <= 0 || wsid >= MAX_WS)
            continue;
        int col = (int)slot % grid_cols;
        int row = (int)slot / grid_cols;

        GtkWidget *cell = gtk_drawing_area_new();
        g_cells[wsid] = cell;
        gtk_widget_set_hexpand(cell, TRUE);
        gtk_widget_set_vexpand(cell, TRUE);

        g_signal_connect(cell, "draw",
                         G_CALLBACK(desperateOverview_ui_draw_cell), GINT_TO_POINTER(wsid));
        gtk_widget_add_events(cell,
                              GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_BUTTON_MOTION_MASK |
                              GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect(cell, "motion-notify-event",
                         G_CALLBACK(desperateOverview_ui_on_cell_motion), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "leave-notify-event",
                         G_CALLBACK(desperateOverview_ui_on_cell_leave), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "button-press-event",
                         G_CALLBACK(desperateOverview_ui_on_cell_button_press), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "button-release-event",
                         G_CALLBACK(desperateOverview_ui_on_cell_button_release), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "drag-begin",
                         G_CALLBACK(desperateOverview_ui_on_cell_drag_begin), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "drag-end",
                         G_CALLBACK(desperateOverview_ui_on_cell_drag_end), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "drag-data-get",
                         G_CALLBACK(desperateOverview_ui_on_cell_drag_data_get), GINT_TO_POINTER(wsid));
        gtk_drag_dest_set(cell,
                          GTK_DEST_DEFAULT_MOTION |
                          GTK_DEST_DEFAULT_HIGHLIGHT |
                          GTK_DEST_DEFAULT_DROP,
                          g_drag_targets,
                          g_drag_targets_count,
                          GDK_ACTION_MOVE);
        g_signal_connect(cell, "drag-drop",
                         G_CALLBACK(desperateOverview_ui_on_cell_drag_drop), GINT_TO_POINTER(wsid));
        g_signal_connect(cell, "drag-data-received",
                         G_CALLBACK(desperateOverview_ui_on_cell_drag_data_received),
                         GINT_TO_POINTER(wsid));

        gtk_grid_attach(GTK_GRID(grid), cell, col, row, 1, 1);
    }
}
