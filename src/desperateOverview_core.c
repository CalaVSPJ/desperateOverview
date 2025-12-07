#define _GNU_SOURCE

#include "desperateOverview_core.h"
#include "desperateOverview_thumbnail_capture.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desperateOverview_core_ipc.h"
#include "desperateOverview_core_state_internal.h"
#include "desperateOverview_core_utils.h"

static CoreRedrawCallback g_redraw_cb = NULL;
static void              *g_redraw_user = NULL;

static void core_request_redraw(void) {
    if (g_redraw_cb)
        g_redraw_cb(g_redraw_user);
}

static void core_on_ipc_refresh(void *user_data) {
    (void)user_data;
    desperateOverview_core_state_refresh_full();
    core_request_redraw();
}

int desperateOverview_core_init(CoreRedrawCallback cb, void *user_data) {
    g_redraw_cb = cb;
    g_redraw_user = user_data;

    desperateOverview_core_state_init();

    if (desperateOverview_core_ipc_init() != 0) {
        fprintf(stderr, "desperateOverview: failed to resolve Hyprland IPC paths\n");
        desperateOverview_core_state_shutdown();
        return -1;
    }

    if (desperateOverview_core_ipc_start_events(core_on_ipc_refresh, NULL) != 0) {
        fprintf(stderr, "desperateOverview: failed to start event listener thread\n");
        desperateOverview_core_ipc_shutdown();
        desperateOverview_core_state_shutdown();
        return -1;
    }

    desperateOverview_core_state_refresh_full();
    core_request_redraw();
    return 0;
}

void desperateOverview_core_shutdown(void) {
    desperateOverview_core_ipc_stop_events();
    desperateOverview_core_ipc_shutdown();
    desperateOverview_core_state_shutdown();
    g_redraw_cb = NULL;
    g_redraw_user = NULL;
}

void desperateOverview_core_move_window(const char *addr, int wsid) {
    if (!addr || !addr[0] || wsid <= 0)
        return;

    char addr_clean[64];
    snprintf(addr_clean, sizeof(addr_clean), "%s", addr);
    desperateOverview_core_sanitize_addr(addr_clean);

    char ws[16];
    snprintf(ws, sizeof(ws), "%d", wsid);

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "dispatch movetoworkspacesilent %s,address:%s",
             ws, addr_clean);
    desperateOverview_core_ipc_send_command(cmd);
}

void desperateOverview_core_switch_workspace(const char *name, int wsid) {
    char cmd[200];

    if (name && strncmp(name, "special:", 8) == 0) {
        snprintf(cmd, sizeof(cmd), "dispatch workspace %s", name);
    } else if (wsid != 0) {
        snprintf(cmd, sizeof(cmd), "dispatch workspace %d", wsid);
    } else if (name && *name) {
        int id = atoi(name);
        if (id != 0)
            snprintf(cmd, sizeof(cmd), "dispatch workspace %d", id);
        else
            snprintf(cmd, sizeof(cmd), "dispatch workspace %s", name);
    } else {
        return;
    }

    desperateOverview_core_ipc_send_command(cmd);
}

char *desperateOverview_core_capture_window_raw(const char *addr) {
    if (!addr || !addr[0])
        return NULL;
    char addr_clean[64];
    snprintf(addr_clean, sizeof(addr_clean), "%s", addr);
    desperateOverview_core_sanitize_addr(addr_clean);
    return capture_window_ppm_base64_with_limit(addr_clean, 0);
}

void desperateOverview_core_request_full_refresh(void) {
    desperateOverview_core_state_refresh_full();
    core_request_redraw();
}
