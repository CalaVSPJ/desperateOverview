#define _GNU_SOURCE
#include "desperateOverview_config.h"

#include <glib/gstdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static OverlayConfig g_config;
static gchar *g_config_override_path = NULL;

static gboolean parse_hex_pair(const char *s, guint8 *out) {
    if (!g_ascii_isxdigit(s[0]) || !g_ascii_isxdigit(s[1]))
        return FALSE;
    int hi = g_ascii_xdigit_value(s[0]);
    int lo = g_ascii_xdigit_value(s[1]);
    if (hi < 0 || lo < 0)
        return FALSE;
    *out = (guint8)((hi << 4) | lo);
    return TRUE;
}

static gboolean parse_color_string(const char *value, GdkRGBA *out) {
    if (!value || !out)
        return FALSE;

    size_t len = strlen(value);
    if (value[0] == '#' && len == 7) {
        guint8 r, g, b;
        if (!parse_hex_pair(value + 1, &r) ||
            !parse_hex_pair(value + 3, &g) ||
            !parse_hex_pair(value + 5, &b))
            return FALSE;
        out->red = r / 255.0;
        out->green = g / 255.0;
        out->blue = b / 255.0;
        out->alpha = 1.0;
        return TRUE;
    }

    if (value[0] == '#' && len == 9) {
        guint8 r, g, b, a;
        if (!parse_hex_pair(value + 1, &r) ||
            !parse_hex_pair(value + 3, &g) ||
            !parse_hex_pair(value + 5, &b) ||
            !parse_hex_pair(value + 7, &a))
            return FALSE;
        out->red = r / 255.0;
        out->green = g / 255.0;
        out->blue = b / 255.0;
        out->alpha = a / 255.0;
        return TRUE;
    }

    return gdk_rgba_parse(out, value);
}

static GdkRGBA lighten_color(const GdkRGBA *base, double delta) {
    GdkRGBA out = *base;
    out.red = fmin(1.0, fmax(0.0, out.red + delta));
    out.green = fmin(1.0, fmax(0.0, out.green + delta));
    out.blue = fmin(1.0, fmax(0.0, out.blue + delta));
    out.alpha = fmin(1.0, fmax(0.0, out.alpha + delta * 0.5));
    return out;
}

static gboolean config_try_color(GKeyFile *kf, const char *key, GdkRGBA *out) {
    g_autofree gchar *value = g_key_file_get_string(kf, "colors", key, NULL);
    if (!value)
        return FALSE;
    GdkRGBA tmp;
    if (!parse_color_string(value, &tmp))
        return FALSE;
    *out = tmp;
    return TRUE;
}

#define DEFAULT_GRID_ROWS 3
#define DEFAULT_GRID_COLS 3

static void apply_default_workspaces(OverlayConfig *cfg) {
    guint cap = cfg->grid_rows * cfg->grid_cols;
    if (cap == 0)
        cap = DEFAULT_GRID_ROWS * DEFAULT_GRID_COLS;
    if (cap >= MAX_WS)
        cap = MAX_WS - 1;
    for (guint i = 0; i < cap; ++i)
        cfg->workspace_ids[i] = (int)(i + 1);
    cfg->workspace_count = cap;
}

static void set_defaults(OverlayConfig *cfg) {
    gdk_rgba_parse(&cfg->inactive_ws_border, "#144344");
    cfg->inactive_ws_border.alpha = 1.0;
    gdk_rgba_parse(&cfg->active_ws_border, "#f2f2f9");
    cfg->active_ws_border.alpha = 0.95;
    gdk_rgba_parse(&cfg->window_border, "#144344");
    cfg->window_border.alpha = 0.85;
    gdk_rgba_parse(&cfg->inactive_ws_bg, "#1a1a1f");
    cfg->inactive_ws_bg.alpha = 0.50;
    gdk_rgba_parse(&cfg->active_ws_bg, "#282831");
    cfg->active_ws_bg.alpha = 0.50;
    gdk_rgba_parse(&cfg->overlay_bg, "#09090d");
    cfg->overlay_bg.alpha = 0.40;
    gdk_rgba_parse(&cfg->new_ws_border, "#9ad0ff");
    cfg->new_ws_border.alpha = 0.9;
    gdk_rgba_parse(&cfg->new_ws_background, "#4d7399");
    cfg->new_ws_background.alpha = 0.85;
    cfg->new_ws_background_hover = lighten_color(&cfg->new_ws_background, 0.15);
    cfg->workspace_corner_radius = 10.0;
    cfg->window_corner_radius = 4.0;
    cfg->window_border_width = 2.0;
    cfg->window_border_hover_width = 2.5;
    cfg->close_button_radius = 13.0;
    cfg->close_button_radius_small = 9.0;
    cfg->grid_margin = 12.0;
    cfg->grid_gap = 8.0;
    cfg->drag_hold_delay_ms = 150;
    cfg->thumbnail_thread_count = 4;
    cfg->fade_step = 0.15;
    cfg->follow_drop = FALSE;

    cfg->grid_rows = DEFAULT_GRID_ROWS;
    cfg->grid_cols = DEFAULT_GRID_COLS;
    for (int i = 0; i < MAX_WS; ++i)
        cfg->workspace_ids[i] = 0;
    apply_default_workspaces(cfg);

    g_strlcpy(cfg->monitor_target, "cursor", sizeof(cfg->monitor_target));
}

/* Parse a comma- or space-separated list of workspace IDs. Returns the
 * number of valid IDs stored (deduped, in source order). Caller-provided
 * capacity bounds the output and prevents overflow. */
static guint parse_workspace_list(const char *raw, int *out_ids, guint capacity) {
    if (!raw || !out_ids || capacity == 0)
        return 0;
    guint count = 0;
    const char *p = raw;
    while (*p && count < capacity) {
        while (*p && (*p == ',' || *p == ' ' || *p == '\t'))
            p++;
        if (!*p)
            break;
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p) {
            p++;
            continue;
        }
        p = end;
        if (v <= 0 || v >= MAX_WS)
            continue;
        gboolean dup = FALSE;
        for (guint i = 0; i < count; ++i) {
            if (out_ids[i] == (int)v) {
                dup = TRUE;
                break;
            }
        }
        if (!dup)
            out_ids[count++] = (int)v;
    }
    return count;
}

static gchar *default_config_path(void) {
    const char *config_dir = g_get_user_config_dir();
    if (!config_dir)
        return NULL;
    return g_build_filename(config_dir, "desperateOverview", "config.ini", NULL);
}

static void load_from_file(const char *path, OverlayConfig *cfg) {
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
        g_warning("desperateOverview config: failed to load '%s': %s", path, err ? err->message : "unknown error");
        if (err)
            g_error_free(err);
        g_key_file_unref(kf);
        return;
    }

    config_try_color(kf, "inactive_workspace_border", &cfg->inactive_ws_border);
    config_try_color(kf, "active_workspace_border", &cfg->active_ws_border);
    config_try_color(kf, "window_border", &cfg->window_border);
    config_try_color(kf, "inactive_workspace_background", &cfg->inactive_ws_bg);
    config_try_color(kf, "active_workspace_background", &cfg->active_ws_bg);
    config_try_color(kf, "overlay_background", &cfg->overlay_bg);
    config_try_color(kf, "new_workspace_border", &cfg->new_ws_border);
    if (config_try_color(kf, "new_workspace_background", &cfg->new_ws_background))
        cfg->new_ws_background_hover = lighten_color(&cfg->new_ws_background, 0.15);
    config_try_color(kf, "new_workspace_background_hover", &cfg->new_ws_background_hover);

    GError *local_err = NULL;
    double workspace_radius = g_key_file_get_double(kf, "layout", "workspace_corner_radius", &local_err);
    if (!local_err && workspace_radius >= 0.0)
        cfg->workspace_corner_radius = workspace_radius;
    if (local_err)
        g_clear_error(&local_err);

    double window_radius = g_key_file_get_double(kf, "layout", "window_corner_radius", &local_err);
    if (!local_err && window_radius >= 0.0)
        cfg->window_corner_radius = window_radius;
    if (local_err)
        g_clear_error(&local_err);

    struct { const char *key; double *out; double min; } visual_doubles[] = {
        { "window_border_width",       &cfg->window_border_width,       0.0 },
        { "window_border_hover_width", &cfg->window_border_hover_width, 0.0 },
        { "close_button_radius",       &cfg->close_button_radius,       1.0 },
        { "close_button_radius_small", &cfg->close_button_radius_small, 1.0 },
        { "grid_margin",               &cfg->grid_margin,               0.0 },
        { "grid_gap",                  &cfg->grid_gap,                  0.0 },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(visual_doubles); ++i) {
        double v = g_key_file_get_double(kf, "layout", visual_doubles[i].key, &local_err);
        if (!local_err && v >= visual_doubles[i].min)
            *visual_doubles[i].out = v;
        if (local_err)
            g_clear_error(&local_err);
    }

    gint drag_delay = g_key_file_get_integer(kf, "behavior", "drag_hold_delay_ms", &local_err);
    if (!local_err && drag_delay > 0)
        cfg->drag_hold_delay_ms = (guint)drag_delay;
    if (local_err)
        g_clear_error(&local_err);

    gint thumb_threads = g_key_file_get_integer(kf, "behavior", "thumbnail_thread_count", &local_err);
    if (!local_err && thumb_threads > 0)
        cfg->thumbnail_thread_count = (guint)thumb_threads;
    if (local_err)
        g_clear_error(&local_err);

    double fade_step = g_key_file_get_double(kf, "behavior", "fade_step", &local_err);
    if (!local_err && fade_step > 0.0)
        cfg->fade_step = fade_step;
    if (local_err)
        g_clear_error(&local_err);

    gboolean follow_drop = g_key_file_get_boolean(kf, "behavior", "follow_drop", &local_err);
    if (!local_err)
        cfg->follow_drop = follow_drop;
    if (local_err)
        g_clear_error(&local_err);

    /* Grid dimensions. Order matters: read rows/cols first so the workspace
     * defaults (used if 'workspaces=' is absent) reflect the requested grid. */
    gboolean grid_overridden = FALSE;
    gint grid_rows = g_key_file_get_integer(kf, "layout", "rows", &local_err);
    if (!local_err && grid_rows > 0) {
        cfg->grid_rows = (guint)grid_rows;
        grid_overridden = TRUE;
    }
    if (local_err)
        g_clear_error(&local_err);

    gint grid_cols = g_key_file_get_integer(kf, "layout", "cols", &local_err);
    if (!local_err && grid_cols > 0) {
        cfg->grid_cols = (guint)grid_cols;
        grid_overridden = TRUE;
    }
    if (local_err)
        g_clear_error(&local_err);

    /* If rows/cols were touched but no explicit workspace list follows,
     * re-derive 1..N for the new grid capacity. */
    if (grid_overridden)
        apply_default_workspaces(cfg);

    g_autofree gchar *monitor_target = g_key_file_get_string(kf, "behavior", "monitor", NULL);
    if (monitor_target && *monitor_target) {
        /* Trim leading/trailing whitespace; the key file parser already
         * strips most of it but defensive trim costs nothing. */
        char *start = monitor_target;
        while (*start == ' ' || *start == '\t') start++;
        size_t len = strlen(start);
        while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t' ||
                           start[len-1] == '\n' || start[len-1] == '\r'))
            start[--len] = '\0';
        if (*start)
            g_strlcpy(cfg->monitor_target, start, sizeof(cfg->monitor_target));
    }

    g_autofree gchar *ws_list = g_key_file_get_string(kf, "layout", "workspaces", NULL);
    if (ws_list && *ws_list) {
        int parsed[MAX_WS];
        for (int i = 0; i < MAX_WS; ++i)
            parsed[i] = 0;
        guint capacity = cfg->grid_rows * cfg->grid_cols;
        if (capacity == 0 || capacity >= MAX_WS)
            capacity = MAX_WS - 1;
        guint n = parse_workspace_list(ws_list, parsed, capacity);
        if (n > 0) {
            for (int i = 0; i < MAX_WS; ++i)
                cfg->workspace_ids[i] = 0;
            for (guint i = 0; i < n; ++i)
                cfg->workspace_ids[i] = parsed[i];
            cfg->workspace_count = n;
        }
    }

    g_key_file_unref(kf);
}

int config_workspace_slot(int wsid) {
    if (wsid <= 0)
        return -1;
    for (guint i = 0; i < g_config.workspace_count; ++i) {
        if (g_config.workspace_ids[i] == wsid)
            return (int)i;
    }
    return -1;
}

int config_workspace_at_slot(int slot) {
    if (slot < 0 || (guint)slot >= g_config.workspace_count)
        return 0;
    return g_config.workspace_ids[slot];
}

void config_init(const char *override_path) {
    g_free(g_config_override_path);
    g_config_override_path = override_path ? g_strdup(override_path) : NULL;

    OverlayConfig cfg;
    set_defaults(&cfg);

    gchar *path = NULL;
    if (g_config_override_path && *g_config_override_path) {
        path = g_strdup(g_config_override_path);
    } else {
        path = default_config_path();
        if (path && !g_file_test(path, G_FILE_TEST_EXISTS)) {
            g_free(path);
            path = NULL;
        }
    }

    if (path) {
        load_from_file(path, &cfg);
        g_free(path);
    }

    g_config = cfg;
}

void config_reload(void) {
    OverlayConfig cfg;
    set_defaults(&cfg);

    gchar *path = NULL;
    if (g_config_override_path && *g_config_override_path) {
        path = g_strdup(g_config_override_path);
    } else {
        path = default_config_path();
        if (path && !g_file_test(path, G_FILE_TEST_EXISTS)) {
            g_free(path);
            path = NULL;
        }
    }

    if (path) {
        load_from_file(path, &cfg);
        g_free(path);
    }

    g_config = cfg;
}

void config_shutdown(void) {
    g_free(g_config_override_path);
    g_config_override_path = NULL;
}

const OverlayConfig *config_get(void) {
    return &g_config;
}

