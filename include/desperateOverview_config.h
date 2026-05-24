#ifndef DESPERATEOVERVIEW_CONFIG_H
#define DESPERATEOVERVIEW_CONFIG_H

#include <gtk/gtk.h>

#include "desperateOverview_core.h"

typedef struct {
    GdkRGBA inactive_ws_border;
    GdkRGBA active_ws_border;
    GdkRGBA window_border;
    GdkRGBA inactive_ws_bg;
    GdkRGBA active_ws_bg;
    GdkRGBA overlay_bg;
    GdkRGBA new_ws_border;
    GdkRGBA new_ws_background;
    GdkRGBA new_ws_background_hover;
    double  workspace_corner_radius;
    double  window_corner_radius;

    /* Visual constants that used to be hardcoded in the renderer and the
     * layout container. All in pixels in the overlay's coordinate space. */
    double  window_border_width;        /* default 2.0 */
    double  window_border_hover_width;  /* default 2.5; thicker when hovered */
    double  close_button_radius;        /* default 13.0; used on normal cells */
    double  close_button_radius_small;  /* default 9.0;  used on small cells (< 40px) */
    double  grid_margin;                /* default 12.0; outer padding around the workspace grid */
    double  grid_gap;                   /* default 8.0;  spacing between grid cells */

    guint   drag_hold_delay_ms;
    guint   thumbnail_thread_count;
    gboolean follow_drop;
    double  fade_step;

    /* Grid layout: workspace_ids[slot] holds the Hyprland workspace ID at
     * row-major grid slot `slot` (0-indexed). workspace_count is the number
     * of populated slots and must be <= grid_rows * grid_cols and < MAX_WS. */
    guint   grid_rows;
    guint   grid_cols;
    int     workspace_ids[MAX_WS];
    guint   workspace_count;

    /* Monitor selection. monitor_target is either:
     *   - "cursor"  (default; follow the cursor's monitor)
     *   - "focused" (always use Hyprland's focused monitor)
     *   - "<name>"  (a specific output name like "DP-1" or "eDP-1") */
    char    monitor_target[64];
} OverlayConfig;

void config_init(const char *override_path);
void config_reload(void);
void config_shutdown(void);
const OverlayConfig *config_get(void);

/* Returns the row-major grid slot for the given workspace id, or -1 if the
 * workspace is not part of the configured grid. */
int config_workspace_slot(int wsid);

/* Returns the workspace id at the given grid slot, or 0 if the slot is
 * out of range or unpopulated. */
int config_workspace_at_slot(int slot);

#endif /* DESPERATEOVERVIEW_CONFIG_H */

