#include "overview_geometry.h"

#include <math.h>

static double clamp_double(double v, double min_v, double max_v) {
    if (v < min_v)
        return min_v;
    if (v > max_v)
        return max_v;
    return v;
}

void overview_geometry_compute_preview_transform(int mon_width,
                                                 int mon_height,
                                                 double canvas_w,
                                                 double canvas_h,
                                                 OverviewPreviewTransform *out) {
    if (!out) return;

    out->scale = 1.0;
    out->view_w = canvas_w;
    out->view_h = canvas_h;
    out->offset_x = 0.0;
    out->offset_y = 0.0;

    if (mon_width <= 0 || mon_height <= 0 ||
        canvas_w <= 0.0 || canvas_h <= 0.0) {
        return;
    }

    double scale_x = canvas_w / (double)mon_width;
    double scale_y = canvas_h / (double)mon_height;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale <= 0.0)
        scale = 1.0;

    out->scale = scale;
    out->view_w = mon_width * scale;
    out->view_h = mon_height * scale;
    out->offset_x = (canvas_w - out->view_w) / 2.0;
    out->offset_y = (canvas_h - out->view_h) / 2.0;
}

void overview_geometry_window_to_normalized(int mon_width,
                                            int mon_height,
                                            int mon_off_x,
                                            int mon_off_y,
                                            int mon_transform,
                                            int win_x,
                                            int win_y,
                                            int win_w,
                                            int win_h,
                                            OverviewRect *out) {
    if (!out) return;

    out->x = 0.0;
    out->y = 0.0;
    out->w = 0.0;
    out->h = 0.0;

    if (mon_width <= 0 || mon_height <= 0)
        return;

    double local_x = (double)(win_x - mon_off_x);
    double local_y = (double)(win_y - mon_off_y);
    double local_w = (double)win_w;
    double local_h = (double)win_h;

    double base_w = (double)mon_width;
    double base_h = (double)mon_height;
    int t = mon_transform % 4;
    if (t < 0)
        t += 4;

    if (t == 1 || t == 3) {
        base_w = (double)mon_height;
        base_h = (double)mon_width;
    }

    if (base_w <= 0.0)
        base_w = 1.0;
    if (base_h <= 0.0)
        base_h = 1.0;

    out->x = clamp_double(local_x / base_w, 0.0, 1.0);
    out->y = clamp_double(local_y / base_h, 0.0, 1.0);
    out->w = clamp_double(local_w / base_w, 0.0, 1.0);
    out->h = clamp_double(local_h / base_h, 0.0, 1.0);
}

double overview_geometry_clamp(double value,
                               double min_value,
                               double max_value) {
    return clamp_double(value, min_value, max_value);
}

int overview_geometry_clamp_int(int value,
                                int min_value,
                                int max_value) {
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}


