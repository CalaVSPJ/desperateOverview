#define _GNU_SOURCE

#include "desperateOverview_ui_layout.h"

#include <gtk/gtk.h>

#include "desperateOverview_config.h"
#include "desperateOverview_ui_state.h"
#include "desperateOverview_ui_render.h"
#include "desperateOverview_ui_drawing.h"
#include "desperateOverview_ui_events.h"
#include "desperateOverview_ui_drag.h"

static const double G_OVERLAY_FRACTION = 0.33;
static const double G_PREVIEW_FRACTION = 0.85;
static const double G_GAP_PX           = 20.0;

static void clear_root_box_children(void);
static gboolean draw_new_workspace_target(GtkWidget *widget, cairo_t *cr, gpointer data);

void desperateOverview_ui_build_overlay_content(GtkWidget *root_box) {
    if (!root_box)
        return;

    int base_w_px = desperateOverview_ui_get_effective_mon_width();
    int base_h_px = desperateOverview_ui_get_effective_mon_height();
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
                     G_CALLBACK(desperateOverview_ui_draw_current_workspace), NULL);
    gtk_widget_add_events(current_preview,
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_BUTTON_MOTION_MASK |
                          GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(current_preview, "motion-notify-event",
                     G_CALLBACK(desperateOverview_ui_on_cell_motion), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "leave-notify-event",
                     G_CALLBACK(desperateOverview_ui_on_cell_leave), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "button-press-event",
                     G_CALLBACK(desperateOverview_ui_on_cell_button_press), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "button-release-event",
                     G_CALLBACK(desperateOverview_ui_on_cell_button_release), GINT_TO_POINTER(0));
    gtk_drag_source_set(current_preview,
                        GDK_BUTTON1_MASK,
                        g_drag_targets,
                        g_drag_targets_count,
                        GDK_ACTION_MOVE);
    g_signal_connect(current_preview, "drag-begin",
                     G_CALLBACK(desperateOverview_ui_on_cell_drag_begin), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "drag-end",
                     G_CALLBACK(desperateOverview_ui_on_cell_drag_end), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "drag-data-get",
                     G_CALLBACK(desperateOverview_ui_on_cell_drag_data_get), GINT_TO_POINTER(0));
    gtk_drag_dest_set(current_preview,
                      GTK_DEST_DEFAULT_MOTION |
                      GTK_DEST_DEFAULT_HIGHLIGHT |
                      GTK_DEST_DEFAULT_DROP,
                      g_drag_targets,
                      g_drag_targets_count,
                      GDK_ACTION_MOVE);
    g_signal_connect(current_preview, "drag-drop",
                     G_CALLBACK(desperateOverview_ui_on_cell_drag_drop), GINT_TO_POINTER(0));
    g_signal_connect(current_preview, "drag-data-received",
                     G_CALLBACK(desperateOverview_ui_on_cell_drag_data_received), GINT_TO_POINTER(0));
    gtk_box_pack_start(GTK_BOX(root_box), current_preview, TRUE, TRUE, 0);

    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(status_box, TRUE);
    gtk_widget_set_margin_bottom(status_box, 6);
    gtk_box_pack_start(GTK_BOX(root_box), status_box, FALSE, FALSE, 0);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(g_status_label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(g_status_label), "desperateOverview-status");
    gtk_box_pack_start(GTK_BOX(status_box), g_status_label, TRUE, TRUE, 0);

    for (int i = 0; i < g_active_count; ++i) {
        int wsid = g_active_list[i];
        GtkWidget *cell = gtk_drawing_area_new();
        g_cells[i] = cell;
        gtk_widget_set_size_request(cell,
                                    (int)(cell_w + 0.5),
                                    (int)(cell_h + 0.5));
        g_signal_connect(cell, "draw", G_CALLBACK(desperateOverview_ui_draw_cell),
                         GINT_TO_POINTER(wsid));
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
        gtk_drag_source_set(cell,
                            GDK_BUTTON1_MASK,
                            g_drag_targets,
                            g_drag_targets_count,
                            GDK_ACTION_MOVE);
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
                         G_CALLBACK(desperateOverview_ui_on_cell_drag_data_received), GINT_TO_POINTER(wsid));
        gtk_box_pack_start(GTK_BOX(hbox), cell, FALSE, FALSE, 0);
    }

    GtkWidget *ghost = gtk_drawing_area_new();
    g_new_ws_target = ghost;
    int ghost_width = (int)(cell_w * ghost_ratio + 0.5);
    gtk_widget_set_size_request(ghost, ghost_width, (int)(cell_h));
    gtk_widget_set_hexpand(ghost, FALSE);
    gtk_widget_set_margin_start(ghost, (int)(gap * 0.5));
    gtk_style_context_add_class(gtk_widget_get_style_context(ghost), "desperateOverview-ghost");
    gtk_widget_add_events(ghost, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(ghost, "draw", G_CALLBACK(draw_new_workspace_target), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), ghost, FALSE, FALSE, 0);
    gtk_drag_dest_set(ghost,
                      GTK_DEST_DEFAULT_MOTION |
                      GTK_DEST_DEFAULT_HIGHLIGHT |
                      GTK_DEST_DEFAULT_DROP,
                      g_drag_targets,
                      g_drag_targets_count,
                      GDK_ACTION_MOVE);
    g_signal_connect(ghost, "drag-motion",
                     G_CALLBACK(desperateOverview_ui_on_new_ws_drag_motion), NULL);
    g_signal_connect(ghost, "drag-leave",
                     G_CALLBACK(desperateOverview_ui_on_new_ws_drag_leave), NULL);
    g_signal_connect(ghost, "drag-drop",
                     G_CALLBACK(desperateOverview_ui_on_new_ws_drag_drop), NULL);
    gtk_widget_queue_draw(ghost);

    for (int j = g_active_count; j < MAX_WS; ++j)
        g_cells[j] = NULL;
}

void desperateOverview_ui_rebuild_overlay_content(void) {
    if (!g_overlay_window || !g_root_box)
        return;
    clear_root_box_children();
    desperateOverview_ui_build_overlay_content(g_root_box);
    gtk_widget_show_all(g_overlay_window);
    g_built_active_count = g_active_count;
    memcpy(g_built_active_list, g_active_list, sizeof(g_active_list));
    for (int i = 0; i < g_active_count && i < MAX_WS; ++i) {
        int wsid = g_active_list[i];
        const char *name = desperateOverview_ui_workspace_display_name(wsid);
        g_strlcpy(g_built_active_names[i], name ? name : "", CORE_WS_NAME_LEN);
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

static gboolean draw_new_workspace_target(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    double x = 2.0;
    double y = 2.0;
    double w = alloc.width - 4.0;
    double h = alloc.height - 4.0;

    cairo_save(cr);
    const OverlayConfig *cfg = config_get();
    cairo_add_rounded_rect(cr, x, y, w, h, cfg->workspace_corner_radius);

    if (g_new_ws_target_hover)
        cairo_set_source_rgba_color(cr, &cfg->new_ws_background_hover);
    else
        cairo_set_source_rgba_color(cr, &cfg->new_ws_background);
    cairo_fill_preserve(cr);

    cairo_set_line_width(cr, 2.5);
    cairo_set_source_rgba_color(cr, &cfg->new_ws_border);
    cairo_stroke(cr);

    cairo_set_line_width(cr, 4.0);
    cairo_set_source_rgba_color(cr, &cfg->new_ws_border);
    cairo_move_to(cr, x + w / 2.0, y + h * 0.25);
    cairo_line_to(cr, x + w / 2.0, y + h * 0.75);
    cairo_move_to(cr, x + w * 0.25, y + h / 2.0);
    cairo_line_to(cr, x + w * 0.75, y + h / 2.0);
    cairo_stroke(cr);

    cairo_restore(cr);
    return FALSE;
}

