#include "desperateOverview_ui_drag.h"

static gboolean ui_drag_hold_dispatch(gpointer data) {
    DragState *drag = (DragState *)data;
    drag->hold_source_id = 0;
    if (!drag->hold_widget || !GTK_IS_WIDGET(drag->hold_widget) ||
        !drag->hold_event_copy || !drag->hold_cb)
        return G_SOURCE_REMOVE;
    return drag->hold_cb(drag, drag->hold_cb_data);
}

void ui_drag_init(DragState *drag) {
    if (!drag)
        return;
    *drag = (DragState){0};
}

void ui_drag_reset(DragState *drag) {
    if (!drag)
        return;
    ui_drag_cancel_hold(drag);
    drag->active_window = NULL;
    drag->source_workspace = -1;
    drag->in_progress = FALSE;
    drag->pending_ws_click = FALSE;
    drag->pending_window_click = FALSE;
    drag->pending_ws_id = -1;
}

void ui_drag_cancel_hold(DragState *drag) {
    if (!drag)
        return;
    if (drag->hold_source_id) {
        g_source_remove(drag->hold_source_id);
        drag->hold_source_id = 0;
    }
    if (drag->hold_event_copy) {
        gdk_event_free(drag->hold_event_copy);
        drag->hold_event_copy = NULL;
    }
    if (drag->hold_widget) {
        g_object_unref(drag->hold_widget);
        drag->hold_widget = NULL;
    }
    drag->hold_cb = NULL;
    drag->hold_cb_data = NULL;
}

void ui_drag_start_hold(DragState *drag,
                        GtkWidget *widget,
                        GdkEventButton *event,
                        guint delay_ms,
                        UiDragHoldCallback cb,
                        gpointer cb_data) {
    if (!drag || !widget || !event)
        return;

    ui_drag_cancel_hold(drag);
    drag->hold_widget = g_object_ref(widget);
    drag->hold_event_copy = gdk_event_copy((GdkEvent *)event);
    drag->hold_start_x = event->x;
    drag->hold_start_y = event->y;
    drag->hold_cb = cb;
    drag->hold_cb_data = cb_data;
    drag->hold_source_id = g_timeout_add(delay_ms > 0 ? delay_ms : 1,
                                         ui_drag_hold_dispatch,
                                         drag);
}

