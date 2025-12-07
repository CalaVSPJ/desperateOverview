#define _GNU_SOURCE

#include "desperateOverview_core_state_internal.h"

#include "desperateOverview_core.h"
#include "desperateOverview_types.h"
#include "desperateOverview_thumbnail_capture.h"
#include "desperateOverview_core_json.h"
#include "desperateOverview_core_utils.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include "yyjson.h"

static int  g_mon_id        = 0;
static int  g_mon_w         = 1920;
static int  g_mon_h         = 1080;
static int  g_mon_x         = 0;
static int  g_mon_y         = 0;
static int  g_mon_transform = 0;
static int  g_active_ws     = 1;

static WorkspaceWindows g_ws[MAX_WS];
static int              g_active_list[MAX_WS];
static int              g_active_count = 0;

static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_capture_enabled = true;

static void free_window(WindowInfo *win) {
    if (!win)
        return;
    free(win->thumb_b64);
    win->thumb_b64 = NULL;
    free(win->class_name);
    win->class_name = NULL;
    free(win->initial_class);
    win->initial_class = NULL;
    free(win->title);
    win->title = NULL;
    win->addr[0] = '\0';
    win->x = win->y = win->w = win->h = 0;
}

static void clear_all_windows(void) {
    for (int wsid = 0; wsid < MAX_WS; ++wsid) {
        WorkspaceWindows *W = &g_ws[wsid];
        for (int i = 0; i < W->count; ++i)
            free_window(&W->wins[i]);
        W->count = 0;
        W->name[0] = '\0';
    }
    memset(g_active_list, 0, sizeof(g_active_list));
    g_active_count = 0;
}

static void ensure_workspace_name(int wsid) {
    if (wsid <= 0 || wsid >= MAX_WS)
        return;
    if (!g_ws[wsid].name[0])
        snprintf(g_ws[wsid].name, CORE_WS_NAME_LEN, "%d", wsid);
}

static void update_workspace_names(void) {
    yyjson_doc *doc = desperateOverview_read_json_from_cmd("hyprctl -j workspaces 2>/dev/null");
    if (!doc)
        return;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root)) {
        yyjson_doc_free(doc);
        return;
    }

    yyjson_val *entry;
    size_t idx, max;
    yyjson_arr_foreach(root, idx, max, entry) {
        int id = desperateOverview_json_get_int(yyjson_obj_get(entry, "id"), -1);
        if (id <= 0 || id >= MAX_WS)
            continue;
        yyjson_val *name_val = yyjson_obj_get(entry, "name");
        const char *name = (name_val && yyjson_is_str(name_val)) ? yyjson_get_str(name_val) : NULL;
        if (name && *name)
            snprintf(g_ws[id].name, CORE_WS_NAME_LEN, "%s", name);
        else
            snprintf(g_ws[id].name, CORE_WS_NAME_LEN, "%d", id);
    }

    yyjson_doc_free(doc);
    ensure_workspace_name(g_active_ws);
}

static void update_monitor_geometry(void) {
    yyjson_doc *doc = desperateOverview_read_json_from_cmd("hyprctl -j monitors 2>/dev/null");
    if (!doc)
        return;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root)) {
        yyjson_doc_free(doc);
        return;
    }

    yyjson_val *entry;
    size_t idx, max;
    bool found = false;
    yyjson_arr_foreach(root, idx, max, entry) {
        if (!desperateOverview_json_is_true(yyjson_obj_get(entry, "focused")))
            continue;

        int w = desperateOverview_json_get_int(yyjson_obj_get(entry, "width"), g_mon_w);
        int h = desperateOverview_json_get_int(yyjson_obj_get(entry, "height"), g_mon_h);
        if (w > 0 && h > 0) {
            g_mon_id = desperateOverview_json_get_int(yyjson_obj_get(entry, "id"), g_mon_id);
            g_mon_w  = w;
            g_mon_h  = h;
            g_mon_x  = desperateOverview_json_get_int(yyjson_obj_get(entry, "x"), g_mon_x);
            g_mon_y  = desperateOverview_json_get_int(yyjson_obj_get(entry, "y"), g_mon_y);
            g_mon_transform = desperateOverview_json_get_int(yyjson_obj_get(entry, "transform"), 0);
            found = true;
            break;
        }
    }

    if (!found)
        g_warning("desperateOverview: focused monitor not found in hyprctl output");

    yyjson_doc_free(doc);
}

static void update_active_workspace(void) {
    yyjson_doc *doc = desperateOverview_read_json_from_cmd("hyprctl -j activeworkspace 2>/dev/null");
    if (!doc)
        return;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return;
    }

    int id = desperateOverview_json_get_int(yyjson_obj_get(root, "id"), -1);
    if (id >= 1 && id < MAX_WS)
        g_active_ws = id;

    yyjson_doc_free(doc);
}

static void update_workspace_windows(void) {
    clear_all_windows();

    int used_ws[MAX_WS] = {0};
    WindowInfo *capture_targets[MAX_WS * MAX_WINS_PER_WS];
    int capture_count = 0;
    bool need_workspace_names = false;

    yyjson_doc *doc = desperateOverview_read_json_from_cmd("hyprctl -j clients 2>/dev/null");
    if (!doc)
        return;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root)) {
        yyjson_doc_free(doc);
        return;
    }

    yyjson_val *entry;
    size_t idx, max;
    yyjson_arr_foreach(root, idx, max, entry) {
        if (!desperateOverview_json_is_true(yyjson_obj_get(entry, "mapped")))
            continue;
        if (desperateOverview_json_is_true(yyjson_obj_get(entry, "hidden")))
            continue;

        yyjson_val *ws_obj = yyjson_obj_get(entry, "workspace");
        int wsid = desperateOverview_json_get_int(ws_obj ? yyjson_obj_get(ws_obj, "id") : NULL, -1);
        if (wsid <= 0 || wsid >= MAX_WS)
            continue;

        int mon = desperateOverview_json_get_int(yyjson_obj_get(entry, "monitor"), -1);
        if (mon != g_mon_id)
            continue;

        WorkspaceWindows *W = &g_ws[wsid];
        if (W->count >= MAX_WINS_PER_WS)
            continue;

        if (ws_obj) {
            yyjson_val *ws_name_val = yyjson_obj_get(ws_obj, "name");
            const char *ws_name = (ws_name_val && yyjson_is_str(ws_name_val)) ? yyjson_get_str(ws_name_val) : NULL;
            if (ws_name && *ws_name)
                g_strlcpy(W->name, ws_name, CORE_WS_NAME_LEN);
            else if (!W->name[0])
                need_workspace_names = true;
        } else if (!W->name[0]) {
            need_workspace_names = true;
        }

        yyjson_val *addr_val = yyjson_obj_get(entry, "address");
        const char *addr_raw = (addr_val && yyjson_is_str(addr_val)) ? yyjson_get_str(addr_val) : NULL;
        if (!addr_raw || !*addr_raw)
            continue;

        WindowInfo *win = &W->wins[W->count];
        snprintf(win->addr, sizeof(win->addr), "%s", addr_raw);
        desperateOverview_core_sanitize_addr(win->addr);
        if (win->addr[0] == '\0' || strcmp(win->addr, "0x0") == 0)
            continue;

        if (!desperateOverview_json_get_vec2(yyjson_obj_get(entry, "at"), &win->x, &win->y))
            continue;
        if (!desperateOverview_json_get_vec2(yyjson_obj_get(entry, "size"), &win->w, &win->h))
            continue;

        win->class_name = desperateOverview_json_dup_str(yyjson_obj_get(entry, "class"));
        win->initial_class = desperateOverview_json_dup_str(yyjson_obj_get(entry, "initialClass"));
        win->title = desperateOverview_json_dup_str(yyjson_obj_get(entry, "title"));
        win->thumb_b64 = NULL;

        if (capture_count < (int)(MAX_WS * MAX_WINS_PER_WS))
            capture_targets[capture_count++] = win;

        W->count++;
        used_ws[wsid] = 1;
    }

    yyjson_doc_free(doc);

    if (g_capture_enabled && capture_count > 0)
        capture_thumbnails_parallel(capture_targets, capture_count);

    if (g_active_ws > 0 && g_active_ws < MAX_WS)
        used_ws[g_active_ws] = 1;

    if (!g_ws[g_active_ws].name[0])
        need_workspace_names = true;
    if (need_workspace_names)
        update_workspace_names();

    g_active_count = 0;
    for (int wsid = 1; wsid < MAX_WS; ++wsid) {
        if (!used_ws[wsid])
            continue;

        if (wsid != g_active_ws) {
            WorkspaceWindows *W = &g_ws[wsid];
            if (!W || W->count <= 0)
                continue;
        }

        ensure_workspace_name(wsid);
        g_active_list[g_active_count++] = wsid;
    }

    if (g_active_ws > 0 && g_active_ws < MAX_WS) {
        bool present = false;
        for (int i = 0; i < g_active_count; ++i) {
            if (g_active_list[i] == g_active_ws) {
                present = true;
                break;
            }
        }
        if (!present) {
            ensure_workspace_name(g_active_ws);
            g_active_list[g_active_count++] = g_active_ws;
        }
    } else if (g_active_count == 0) {
        ensure_workspace_name(1);
        g_active_list[0] = 1;
        g_active_count = 1;
    }
}

void desperateOverview_core_state_refresh_full(void) {
    pthread_mutex_lock(&g_state_lock);
    update_active_workspace();
    update_monitor_geometry();
    update_workspace_windows();
    pthread_mutex_unlock(&g_state_lock);
}

void desperateOverview_core_state_init(void) {
    pthread_mutex_lock(&g_state_lock);
    clear_all_windows();
    pthread_mutex_unlock(&g_state_lock);
}

void desperateOverview_core_state_shutdown(void) {
    pthread_mutex_lock(&g_state_lock);
    clear_all_windows();
    pthread_mutex_unlock(&g_state_lock);
}

static void copy_window_to_core(const WindowInfo *src, CoreWindow *dst) {
    dst->x = src->x;
    dst->y = src->y;
    dst->w = src->w;
    dst->h = src->h;
    strncpy(dst->addr, src->addr, sizeof(dst->addr));
    dst->addr[sizeof(dst->addr) - 1] = '\0';
    dst->thumb_b64 = src->thumb_b64 ? strdup(src->thumb_b64) : NULL;
    dst->class_name = src->class_name ? strdup(src->class_name) : NULL;
    dst->initial_class = src->initial_class ? strdup(src->initial_class) : NULL;
    dst->title = src->title ? strdup(src->title) : NULL;
}

void desperateOverview_core_copy_state(CoreState *out_state) {
    if (!out_state)
        return;

    desperateOverview_core_free_state(out_state);
    memset(out_state, 0, sizeof(*out_state));

    pthread_mutex_lock(&g_state_lock);

    out_state->mon_id = g_mon_id;
    out_state->mon_width = g_mon_w;
    out_state->mon_height = g_mon_h;
    out_state->mon_off_x = g_mon_x;
    out_state->mon_off_y = g_mon_y;
    out_state->mon_transform = g_mon_transform;
    out_state->active_workspace = g_active_ws;
    out_state->active_count = g_active_count;
    memcpy(out_state->active_list, g_active_list, sizeof(g_active_list));

    for (int wsid = 0; wsid < MAX_WS; ++wsid) {
        CoreWorkspace *dst_ws = &out_state->workspaces[wsid];
        WorkspaceWindows *src_ws = &g_ws[wsid];
        dst_ws->count = src_ws->count;
        g_strlcpy(dst_ws->name, src_ws->name, CORE_WS_NAME_LEN);
        for (int j = 0; j < src_ws->count; ++j)
            copy_window_to_core(&src_ws->wins[j], &dst_ws->wins[j]);
    }

    pthread_mutex_unlock(&g_state_lock);
}

void desperateOverview_core_free_state(CoreState *state) {
    if (!state)
        return;
    for (int wsid = 0; wsid < MAX_WS; ++wsid) {
        CoreWorkspace *ws = &state->workspaces[wsid];
        for (int j = 0; j < ws->count; ++j) {
            free(ws->wins[j].thumb_b64);
            ws->wins[j].thumb_b64 = NULL;
            free(ws->wins[j].class_name);
            ws->wins[j].class_name = NULL;
            free(ws->wins[j].initial_class);
            ws->wins[j].initial_class = NULL;
            free(ws->wins[j].title);
            ws->wins[j].title = NULL;
        }
        ws->count = 0;
    }
}

void desperateOverview_core_set_thumbnail_capture_enabled(bool enabled) {
    pthread_mutex_lock(&g_state_lock);
    g_capture_enabled = enabled;
    pthread_mutex_unlock(&g_state_lock);
}

