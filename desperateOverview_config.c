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

static void set_defaults(OverlayConfig *cfg) {
    gdk_rgba_parse(&cfg->inactive_ws_border, "#144344");
    cfg->inactive_ws_border.alpha = 1.0;
    gdk_rgba_parse(&cfg->active_ws_border, "#f2f2f9");
    cfg->active_ws_border.alpha = 0.95;
    gdk_rgba_parse(&cfg->window_border, "#144344");
    cfg->window_border.alpha = 0.85;
    gdk_rgba_parse(&cfg->inactive_ws_bg, "#1a1a1f");
    cfg->inactive_ws_bg.alpha = 0.95;
    gdk_rgba_parse(&cfg->active_ws_bg, "#282831");
    cfg->active_ws_bg.alpha = 0.95;
    gdk_rgba_parse(&cfg->overlay_bg, "#09090d");
    cfg->overlay_bg.alpha = 0.40;
    gdk_rgba_parse(&cfg->drag_highlight, "#ffcc33");
    cfg->drag_highlight.alpha = 0.9;
    gdk_rgba_parse(&cfg->new_ws_border, "#9ad0ff");
    cfg->new_ws_border.alpha = 0.9;
    gdk_rgba_parse(&cfg->new_ws_background, "#4d7399");
    cfg->new_ws_background.alpha = 0.85;
    cfg->new_ws_background_hover = lighten_color(&cfg->new_ws_background, 0.15);
    cfg->workspace_corner_radius = 10.0;
    cfg->window_corner_radius = 4.0;
    cfg->drag_hold_delay_ms = 150;
    cfg->thumbnail_thread_count = 4;
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
    if (config_try_color(kf, "drag_highlight", &cfg->drag_highlight)) {
        if (cfg->drag_highlight.alpha <= 0.0)
            cfg->drag_highlight.alpha = 0.9;
    }
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

    g_key_file_unref(kf);
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

void config_shutdown(void) {
    g_free(g_config_override_path);
    g_config_override_path = NULL;
}

const OverlayConfig *config_get(void) {
    return &g_config;
}

