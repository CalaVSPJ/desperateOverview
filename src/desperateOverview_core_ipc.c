#define _GNU_SOURCE

#include "desperateOverview_core_ipc.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <glib.h>

static char g_hypr_sock_cmd[PATH_MAX];
static char g_hypr_sock_evt[PATH_MAX];

static pthread_t g_event_thread;
static bool g_event_thread_running = false;
static bool g_event_thread_spawned = false;

static DesperateOverviewCoreRefreshHook g_refresh_hook = NULL;
static void *g_refresh_user = NULL;

static int init_hypr_paths(void) {
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    const char *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg || !his)
        return -1;

    if (snprintf(g_hypr_sock_cmd, sizeof(g_hypr_sock_cmd),
                 "%s/hypr/%s/.socket.sock", xdg, his) >= (int)sizeof(g_hypr_sock_cmd))
        return -1;
    if (snprintf(g_hypr_sock_evt, sizeof(g_hypr_sock_evt),
                 "%s/hypr/%s/.socket2.sock", xdg, his) >= (int)sizeof(g_hypr_sock_evt))
        return -1;
    return 0;
}

static int connect_unix_socket(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool event_requires_refresh(const char *event_name) {
    if (!event_name || !*event_name)
        return false;

    static const char *kEvents[] = {
        "openwindow",
        "closewindow",
        "movewindowv2",
        "moveworkspacev2",
        "createworkspacev2",
        "destroyworkspacev2",
        "workspacev2",
        "changefloatingmode",
        "activewindowv2",
    };

    for (size_t i = 0; i < sizeof(kEvents) / sizeof(kEvents[0]); ++i) {
        if (strcmp(event_name, kEvents[i]) == 0)
            return true;
    }

    return false;
}

static void trigger_refresh(void) {
    if (g_refresh_hook)
        g_refresh_hook(g_refresh_user);
}

static void *hypr_event_thread(void *data) {
    (void)data;
    const guint BACKOFF_MIN_MS = 100;
    const guint BACKOFF_MAX_MS = 1000;
    guint backoff_ms = BACKOFF_MIN_MS;

    while (g_event_thread_running) {
        if (!g_hypr_sock_evt[0] && init_hypr_paths() < 0) {
            g_usleep((gulong)backoff_ms * 1000);
            backoff_ms = MIN(backoff_ms * 2, BACKOFF_MAX_MS);
            continue;
        }

        int fd = connect_unix_socket(g_hypr_sock_evt);
        if (fd < 0) {
            g_warning("desperateOverview: event socket connect(%s) failed: %s",
                      g_hypr_sock_evt, strerror(errno));
            g_usleep((gulong)backoff_ms * 1000);
            backoff_ms = MIN(backoff_ms * 2, BACKOFF_MAX_MS);
            continue;
        }

        FILE *fp = fdopen(fd, "r");
        if (!fp) {
            close(fd);
            g_usleep((gulong)backoff_ms * 1000);
            backoff_ms = MIN(backoff_ms * 2, BACKOFF_MAX_MS);
            continue;
        }

        backoff_ms = BACKOFF_MIN_MS;
        char line[512];
        while (g_event_thread_running && fgets(line, sizeof(line), fp)) {
            char *nl = strchr(line, '\n');
            if (nl)
                *nl = 0;

            char *sep = strstr(line, ">>");
            if (!sep)
                continue;
            *sep = 0;

            if (event_requires_refresh(line))
                trigger_refresh();
        }

        fclose(fp);
        g_usleep((gulong)backoff_ms * 1000);
        backoff_ms = MIN(backoff_ms * 2, BACKOFF_MAX_MS);
    }

    return NULL;
}

int desperateOverview_core_ipc_init(void) {
    return init_hypr_paths();
}

void desperateOverview_core_ipc_shutdown(void) {
    g_hypr_sock_cmd[0] = '\0';
    g_hypr_sock_evt[0] = '\0';
}

int desperateOverview_core_ipc_send_command(const char *command) {
    if (!command || !*command)
        return -1;
    if (!g_hypr_sock_cmd[0] && init_hypr_paths() < 0)
        return -1;

    int fd = connect_unix_socket(g_hypr_sock_cmd);
    if (fd < 0) {
        g_warning("desperateOverview: connect(%s) failed: %s",
                  g_hypr_sock_cmd, strerror(errno));
        return -1;
    }

    char payload[512];
    int len = snprintf(payload, sizeof(payload), "%s", command);
    if (len < 0 || len >= (int)sizeof(payload)) {
        close(fd);
        return -1;
    }

    ssize_t w = write(fd, payload, (size_t)len);
    if (w < 0 || (size_t)w != (size_t)len) {
        g_warning("desperateOverview: Hyprland command write failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    char buf[256];
    while (read(fd, buf, sizeof(buf) - 1) > 0) {
        /* discard response */
    }

    close(fd);
    return 0;
}

int desperateOverview_core_ipc_start_events(DesperateOverviewCoreRefreshHook hook,
                                            void *user_data) {
    g_refresh_hook = hook;
    g_refresh_user = user_data;

    if (!g_refresh_hook)
        return -1;

    g_event_thread_running = true;
    if (pthread_create(&g_event_thread, NULL, hypr_event_thread, NULL) != 0) {
        g_event_thread_running = false;
        g_refresh_hook = NULL;
        g_refresh_user = NULL;
        return -1;
    }

    g_event_thread_spawned = true;
    return 0;
}

void desperateOverview_core_ipc_stop_events(void) {
    g_event_thread_running = false;
    if (g_event_thread_spawned) {
        pthread_join(g_event_thread, NULL);
        g_event_thread_spawned = false;
    }
    g_refresh_hook = NULL;
    g_refresh_user = NULL;
}

