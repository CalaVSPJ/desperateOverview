#define _GNU_SOURCE

#include "desperateOverview_ui_live.h"

#include <glib.h>
#include <string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "desperateOverview_core.h"
#include "desperateOverview_thumbnail_capture.h"

typedef struct {
    WindowInfo *win;
    GdkPixbuf  *pixbuf;
    char        addr_snapshot[64];
    guint64     cookie_snapshot;
} LivePreviewTask;

static DesperateOverviewLiveApply g_live_apply_cb = NULL;
static gpointer                   g_live_apply_data = NULL;
static guint64                    g_live_cookie_counter = 1;

void desperateOverview_live_cancel_tasks(WindowInfo *win) {
    if (!win)
        return;
    win->live_cookie = g_live_cookie_counter++;
}

static gboolean dispatch_live_preview(gpointer data) {
    LivePreviewTask *task = data;
    if (task->win && task->addr_snapshot[0] &&
        task->win->addr[0] &&
        strcmp(task->win->addr, task->addr_snapshot) == 0 &&
        task->cookie_snapshot == task->win->live_cookie &&
        g_live_apply_cb) {
        g_live_apply_cb(task->win, task->pixbuf, g_live_apply_data);
    } else if (task->pixbuf) {
        g_object_unref(task->pixbuf);
    }
    g_free(task);
    return G_SOURCE_REMOVE;
}

void desperateOverview_ui_live_init(DesperateOverviewLiveApply cb, gpointer user_data) {
    g_live_apply_cb = cb;
    g_live_apply_data = user_data;
}

static void desperateOverview_ui_capture_live_preview(WindowInfo *win) {
    if (!win || !win->addr[0])
        return;

    guint64 cookie = win->live_cookie;

    char *b64 = desperateOverview_core_capture_window_raw(win->addr);
    if (!b64) {
        g_warning("[desperateOverview] live preview capture failed for %s (no data)", win->addr);
        return;
    }

    gsize raw_len = 0;
    guchar *raw = g_base64_decode(b64, &raw_len);
    free(b64);
    if (!raw || raw_len == 0) {
        g_warning("[desperateOverview] live preview capture failed for %s (empty payload)", win->addr);
        if (raw)
            g_free(raw);
        return;
    }

    if (cookie != win->live_cookie) {
        g_free(raw);
        return;
    }

    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    gboolean ok = gdk_pixbuf_loader_write(loader, raw, raw_len, NULL);
    if (ok)
        gdk_pixbuf_loader_close(loader, NULL);

    GdkPixbuf *result = NULL;
    if (ok) {
        GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(loader);
        if (pb)
            result = g_object_ref(pb);
        else
            g_warning("[desperateOverview] live preview decode produced NULL pixbuf for %s", win->addr);
    } else {
        g_warning("[desperateOverview] live preview decode failed for %s", win->addr);
        gdk_pixbuf_loader_close(loader, NULL);
    }

    g_object_unref(loader);
    g_free(raw);

    if (cookie != win->live_cookie) {
        if (result)
            g_object_unref(result);
        return;
    }

    if (g_live_apply_cb) {
        LivePreviewTask *task = g_new0(LivePreviewTask, 1);
        task->win = win;
        task->pixbuf = result;
        g_strlcpy(task->addr_snapshot, win->addr, sizeof(task->addr_snapshot));
        task->cookie_snapshot = cookie;
        g_idle_add(dispatch_live_preview, task);
    } else if (result) {
        g_object_unref(result);
    }
}

void desperateOverview_ui_build_live_previews(int active_workspace,
                                              WorkspaceWindows workspaces[]) {
    if (!workspaces || active_workspace <= 0 || active_workspace >= MAX_WS)
        return;

    WorkspaceWindows *W = &workspaces[active_workspace];
    if (!W)
        return;

    WindowInfo *targets[MAX_WINS_PER_WS];
    int target_count = 0;
    for (int i = 0; i < W->count && target_count < MAX_WINS_PER_WS; ++i) {
        WindowInfo *win = &W->wins[i];
        if (!win || !win->addr[0])
            continue;
        win->live_cookie = g_live_cookie_counter++;
        targets[target_count++] = win;
    }

    if (target_count <= 0)
        return;

    if (target_count == 1) {
        desperateOverview_ui_capture_live_preview(targets[0]);
        return;
    }

    capture_windows_parallel(targets, target_count, desperateOverview_ui_capture_live_preview);
}

