#ifndef OVERVIEW_GEOMETRY_H
#define OVERVIEW_GEOMETRY_H

#include <stddef.h>

typedef struct {
    double x;
    double y;
    double w;
    double h;
} OverviewRect;

typedef struct {
    double scale;
    double view_w;
    double view_h;
    double offset_x;
    double offset_y;
} OverviewPreviewTransform;

void overview_geometry_compute_preview_transform(int mon_width,
                                                 int mon_height,
                                                 double canvas_w,
                                                 double canvas_h,
                                                 OverviewPreviewTransform *out);

void overview_geometry_window_to_normalized(int mon_width,
                                            int mon_height,
                                            int mon_off_x,
                                            int mon_off_y,
                                            int mon_transform,
                                            int win_x,
                                            int win_y,
                                            int win_w,
                                            int win_h,
                                            OverviewRect *out);

double overview_geometry_clamp(double value,
                               double min_value,
                               double max_value);

int overview_geometry_clamp_int(int value,
                                int min_value,
                                int max_value);

#endif /* OVERVIEW_GEOMETRY_H */


