#ifndef DESPERATEOVERVIEW_UI_EVENTS_H
#define DESPERATEOVERVIEW_UI_EVENTS_H

#include <gtk/gtk.h>

#include "desperateOverview_types.h"

void desperateOverview_ui_start_drag_hold_timer(GtkWidget *widget, GdkEventButton *event);
void desperateOverview_ui_cancel_drag_hold_timer(void);
void desperateOverview_ui_set_hover_window(WindowInfo *win, gboolean bottom_view);

gboolean desperateOverview_ui_on_cell_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data);
gboolean desperateOverview_ui_on_cell_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data);
gboolean desperateOverview_ui_on_cell_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
gboolean desperateOverview_ui_on_cell_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data);
void     desperateOverview_ui_on_cell_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer data);
void     desperateOverview_ui_on_cell_drag_end(GtkWidget *widget, GdkDragContext *context, gpointer data);
void     desperateOverview_ui_on_cell_drag_data_get(GtkWidget *widget,
                                                    GdkDragContext *context,
                                                    GtkSelectionData *selection_data,
                                                    guint info,
                                                    guint time,
                                                    gpointer data);
void     desperateOverview_ui_on_cell_drag_data_received(GtkWidget *widget,
                                                         GdkDragContext *context,
                                                         gint x,
                                                         gint y,
                                                         GtkSelectionData *selection_data,
                                                         guint info,
                                                         guint time,
                                                         gpointer data);
gboolean desperateOverview_ui_on_cell_drag_drop(GtkWidget *widget,
                                                GdkDragContext *context,
                                                gint x,
                                                gint y,
                                                guint time,
                                                gpointer data);
gboolean desperateOverview_ui_on_new_ws_drag_motion(GtkWidget *widget, GdkDragContext *context,
                                                    gint x, gint y, guint time, gpointer data);
void     desperateOverview_ui_on_new_ws_drag_leave(GtkWidget *widget, GdkDragContext *context,
                                                   guint time, gpointer data);
gboolean desperateOverview_ui_on_new_ws_drag_drop(GtkWidget *widget, GdkDragContext *context,
                                                  gint x, gint y, guint time, gpointer data);
gboolean desperateOverview_ui_on_key(GtkWidget *widget, GdkEventKey *event, gpointer data);

#endif /* DESPERATEOVERVIEW_UI_EVENTS_H */

