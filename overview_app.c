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

#include "overview_core.h"
#include "overview_ui.h"

static pthread_t g_control_thread;
static int g_control_sock = -1;
static bool g_control_thread_running = false;
static char g_control_sock_path[256];
static char g_cli_config_path[PATH_MAX];
static bool g_cli_config_path_set = false;

#define CONTROL_SOCKET_PATH_FMT "/run/user/%d/desp_overview.sock"

static int start_control_server(void);
static void stop_control_server(void);
static void *control_server_thread(void *data);
static void handle_control_command(const char *cmd);
static int send_control_command(const char *cmd);
static bool notify_existing_instance(const char *cmd);

static void handle_control_command(const char *cmd) {
    if (!cmd || !*cmd)
        return;

    if (g_ascii_strcasecmp(cmd, "SHOW") == 0) {
        overview_ui_request_show();
    } else if (g_ascii_strcasecmp(cmd, "HIDE") == 0) {
        overview_ui_request_hide();
    } else if (g_ascii_strcasecmp(cmd, "TOGGLE") == 0) {
        if (overview_ui_is_visible())
            overview_ui_request_hide();
        else
            overview_ui_request_show();
    } else if (g_ascii_strcasecmp(cmd, "QUIT") == 0) {
        overview_ui_request_hide();
        overview_ui_request_quit();
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

static int send_control_command(const char *cmd) {
    char path[256];
    snprintf(path, sizeof(path), CONTROL_SOCKET_PATH_FMT, getuid());

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path),
             "%.*s", (int)sizeof(addr.sun_path) - 1, path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    dprintf(fd, "%s\n", cmd);
    close(fd);
    return 0;
}

static bool notify_existing_instance(const char *cmd) {
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
    bool force_show_on_start = false;
    bool skip_notify = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "overviewApp: --config requires a file path\n");
                return 1;
            }
            snprintf(g_cli_config_path, sizeof(g_cli_config_path), "%s", argv[i + 1]);
            g_cli_config_path_set = true;
            ++i;
        } else if (strcmp(argv[i], "--toggle") == 0) {
            if (send_control_command("TOGGLE") == 0)
                return 0;
            fprintf(stderr, "overviewApp: no running instance, starting new overlay.\n");
            force_show_on_start = true;
            skip_notify = true;
        } else if (strcmp(argv[i], "--show") == 0) {
            return send_control_command("SHOW");
        } else if (strcmp(argv[i], "--hide") == 0) {
            return send_control_command("HIDE");
        } else if (strcmp(argv[i], "--quit") == 0) {
            return send_control_command("QUIT");
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!skip_notify && notify_existing_instance("SHOW"))
        return 0;

    gtk_init(&argc, &argv);
    const char *config_path = g_cli_config_path_set ? g_cli_config_path : NULL;
    overview_ui_init(config_path);

    if (core_init(overview_ui_core_redraw_callback, NULL) != 0) {
        overview_ui_shutdown();
        return 1;
    }
    overview_ui_sync_with_core();

    if (start_control_server() != 0) {
        core_shutdown();
        overview_ui_shutdown();
        return 1;
    }

    if (force_show_on_start)
        overview_ui_request_show();

    gtk_main();

    stop_control_server();
    core_shutdown();
    overview_ui_shutdown();
    return 0;
}


