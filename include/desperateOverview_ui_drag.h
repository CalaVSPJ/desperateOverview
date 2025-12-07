#ifndef DESPERATEOVERVIEW_UI_DRAG_H
#define DESPERATEOVERVIEW_UI_DRAG_H

#include <gtk/gtk.h>
#include "desperateOverview_types.h"

typedef struct DragState DragState;

typedef gboolean (*UiDragHoldCallback)(DragState *drag, gpointer user_data);

struct DragState {
    WindowInfo *active_window;
    int source_workspace;
    gboolean in_progress;
    gboolean pending_ws_click;
    gboolean pending_window_click;
    int pending_ws_id;
    guint hold_source_id;
    GtkWidget *hold_widget;
    GdkEvent *hold_event_copy;
    double hold_start_x;
    double hold_start_y;
    UiDragHoldCallback hold_cb;
    gpointer hold_cb_data;
};

void ui_drag_init(DragState *drag);
void ui_drag_reset(DragState *drag);
void ui_drag_cancel_hold(DragState *drag);
void ui_drag_start_hold(DragState *drag,
                        GtkWidget *widget,
                        GdkEventButton *event,
                        guint delay_ms,
                        UiDragHoldCallback cb,
                        gpointer cb_data);

#endif /* DESPERATEOVERVIEW_UI_DRAG_H */

