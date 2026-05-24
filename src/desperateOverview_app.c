#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pthread.h>

#include "desperateOverview_config.h"
#include "desperateOverview_core.h"
#include "desperateOverview_ui.h"

static pthread_t g_control_thread;
static int g_control_sock = -1;
static bool g_control_thread_running = false;
static char g_control_sock_path[256];
static char g_cli_config_path[PATH_MAX];
static bool g_cli_config_path_set = false;

#define CONTROL_SOCKET_PATH_FMT "/run/user/%d/desp_overview.sock"

#ifndef DESPERATEOVERVIEW_VERSION
#define DESPERATEOVERVIEW_VERSION "0.0.0-dev"
#endif

static void print_usage(FILE *out) {
    fprintf(out,
        "Usage: desperateOverview [options]\n"
        "\n"
        "Hyprland workspace overlay with live window thumbnails.\n"
        "Each invocation either shows a one-shot overlay or talks to one\n"
        "that is already showing; no long-running daemon is required.\n"
        "\n"
        "Modes:\n"
        "  --show              Show the overlay (default if no mode flag is given).\n"
        "                      If another instance is already showing, exits silently.\n"
        "  --toggle            If an instance is showing, dismiss it. Otherwise show one.\n"
        "  --hide, --quit      Dismiss the running overlay; no-op if none.\n"
        "\n"
        "Options:\n"
        "  --config <path>     Use the given INI config file.\n"
        "                      Default: $XDG_CONFIG_HOME/desperateOverview/config.ini\n"
        "  -h, --help          Show this help and exit.\n"
        "  -V, --version       Print version and exit.\n"
        "\n"
        "See docs/config.example.ini for the full set of config keys.\n");
}

static int start_control_server(void);
static void stop_control_server(void);
static void *control_server_thread(void *data);
static void handle_control_command(const char *cmd);
static bool send_command(const char *cmd);

/* The control socket only carries one real command in the no-daemon model:
 * "QUIT" (also accepted as "HIDE", since dismissing the one-shot overlay
 * means the process exits). Empty payloads are tolerated so that callers
 * can use the socket purely as a presence probe. */
static void handle_control_command(const char *cmd) {
    if (!cmd || !*cmd)
        return;

    if (g_ascii_strcasecmp(cmd, "QUIT") == 0 ||
        g_ascii_strcasecmp(cmd, "HIDE") == 0) {
        desperateOverview_ui_request_hide();
        desperateOverview_ui_request_quit();
    }
}

static void *control_server_thread(void *data) {
    (void)data;
    while (g_control_thread_running) {
        int cfd = accept(g_control_sock, NULL, NULL);
        if (cfd < 0) {
            if (!g_control_thread_running)
                break;
            if (errno == EINTR)
                continue;
            break;
        }

        char buf[128];
        ssize_t r = read(cfd, buf, sizeof(buf) - 1);
        if (r > 0) {
            buf[r] = 0;
            buf[strcspn(buf, "\r\n")] = 0;
            handle_control_command(buf);
        }
        close(cfd);
    }
    return NULL;
}

static int start_control_server(void) {
    snprintf(g_control_sock_path, sizeof(g_control_sock_path),
             CONTROL_SOCKET_PATH_FMT, getuid());
    unlink(g_control_sock_path);

    g_control_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_control_sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path),
             "%.*s", (int)sizeof(addr.sun_path) - 1, g_control_sock_path);

    if (bind(g_control_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(g_control_sock);
        g_control_sock = -1;
        return -1;
    }

    if (listen(g_control_sock, 5) < 0) {
        perror("listen");
        close(g_control_sock);
        g_control_sock = -1;
        return -1;
    }

    g_control_thread_running = true;
    if (pthread_create(&g_control_thread, NULL, control_server_thread, NULL) != 0) {
        fprintf(stderr, "failed to start control server thread\n");
        g_control_thread_running = false;
        close(g_control_sock);
        g_control_sock = -1;
        return -1;
    }

    return 0;
}

static void stop_control_server(void) {
    if (g_control_thread_running) {
        g_control_thread_running = false;
        if (g_control_sock >= 0) {
            shutdown(g_control_sock, SHUT_RDWR);
            close(g_control_sock);
            g_control_sock = -1;
        }
        pthread_join(g_control_thread, NULL);
    } else if (g_control_sock >= 0) {
        close(g_control_sock);
        g_control_sock = -1;
    }

    if (g_control_sock_path[0])
        unlink(g_control_sock_path);
}

static bool send_command(const char *cmd) {
    char path[256];
    snprintf(path, sizeof(path), CONTROL_SOCKET_PATH_FMT, getuid());

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path),
             "%.*s", (int)sizeof(addr.sun_path) - 1, path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    dprintf(fd, "%s\n", cmd);
    close(fd);
    return true;
}

int main(int argc, char **argv) {
    enum { MODE_SHOW, MODE_TOGGLE, MODE_HIDE } mode = MODE_SHOW;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("desperateOverview %s\n", DESPERATEOVERVIEW_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "desperateOverview: --config requires a file path\n");
                return 1;
            }
            snprintf(g_cli_config_path, sizeof(g_cli_config_path), "%s", argv[i + 1]);
            g_cli_config_path_set = true;
            ++i;
        } else if (strcmp(argv[i], "--show") == 0) {
            mode = MODE_SHOW;
        } else if (strcmp(argv[i], "--toggle") == 0) {
            mode = MODE_TOGGLE;
        } else if (strcmp(argv[i], "--hide") == 0 || strcmp(argv[i], "--quit") == 0) {
            mode = MODE_HIDE;
        } else {
            fprintf(stderr, "desperateOverview: unknown option '%s'. Try --help.\n",
                    argv[i]);
            return 1;
        }
    }

    /* No daemon: each invocation either starts a one-shot overlay or
     * talks to one that's already showing via its control socket. */
    switch (mode) {
        case MODE_HIDE:
            /* No-op if nothing's running; the call's failure is silent. */
            send_command("QUIT");
            return 0;
        case MODE_TOGGLE:
            /* If an instance is showing, dismiss it and exit. Otherwise
             * fall through and start a fresh one. */
            if (send_command("QUIT"))
                return 0;
            break;
        case MODE_SHOW:
            /* If an instance is already showing, don't disturb it. Empty
             * payload is a no-op on the receiver but still proves liveness. */
            if (send_command(""))
                return 0;
            break;
    }

    gtk_init(&argc, &argv);
    const char *config_path = g_cli_config_path_set ? g_cli_config_path : NULL;
    desperateOverview_ui_init(config_path);

    {
        /* desperateOverview_ui_init() has already invoked config_init(),
         * so config_get() is valid here. Push the monitor selection into
         * the core layer before its first refresh. */
        const OverlayConfig *cfg = config_get();
        const char *t = cfg->monitor_target;
        if (!t || !*t || g_ascii_strcasecmp(t, "cursor") == 0) {
            desperateOverview_core_set_monitor_target(CORE_MONITOR_TARGET_CURSOR, NULL);
        } else if (g_ascii_strcasecmp(t, "focused") == 0) {
            desperateOverview_core_set_monitor_target(CORE_MONITOR_TARGET_FOCUSED, NULL);
        } else {
            desperateOverview_core_set_monitor_target(CORE_MONITOR_TARGET_NAMED, t);
        }
    }

    /* The overlay always exits when it's hidden; there's no daemon to fall
     * back to. */
    desperateOverview_ui_set_exit_on_hide(true);

    if (desperateOverview_core_init(desperateOverview_ui_core_redraw_callback, NULL) != 0) {
        desperateOverview_ui_shutdown();
        return 1;
    }
    desperateOverview_ui_sync_with_core();

    /* Socket failure is non-fatal: the overlay still works, you just can't
     * tell it to quit from another invocation (Escape / outside-click /
     * SIGTERM still dismiss it). */
    bool control_server_started = (start_control_server() == 0);
    if (!control_server_started)
        fprintf(stderr, "desperateOverview: control socket unavailable; "
                        "--toggle and --hide from other invocations will not work.\n");

    desperateOverview_ui_request_show();

    gtk_main();

    if (control_server_started)
        stop_control_server();
    desperateOverview_core_shutdown();
    desperateOverview_ui_shutdown();
    return 0;
}


