#ifndef DESPERATEOVERVIEW_CONFIG_H
#define DESPERATEOVERVIEW_CONFIG_H

#include <gtk/gtk.h>

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
    guint   thumbnail_thread_count;
    gboolean follow_drop;
    double  fade_step;
} OverlayConfig;

void config_init(const char *override_path);
void config_shutdown(void);
const OverlayConfig *config_get(void);

#endif /* DESPERATEOVERVIEW_CONFIG_H */

