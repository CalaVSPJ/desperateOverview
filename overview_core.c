#define _GNU_SOURCE

#include "overview_core.h"
#include "overview_types.h"
#include "thumbnail_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>

static int g_mon_id        = 0;
static int g_mon_w         = 1920;
static int g_mon_h         = 1080;
static int g_mon_x         = 0;
static int g_mon_y         = 0;
static int g_mon_transform = 0;
static int g_active_ws = 1;

static WorkspaceWindows g_ws[MAX_WS];
static int              g_active_list[MAX_WS];
static char             g_active_names[MAX_WS][CORE_WS_NAME_LEN];
static int              g_active_count = 0;
static char             g_workspace_names[MAX_WS][CORE_WS_NAME_LEN];

static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;

static char g_hypr_sock_cmd[PATH_MAX];
static char g_hypr_sock_evt[PATH_MAX];

static pthread_t g_event_thread;
static bool g_event_thread_running = false;
static bool g_event_thread_spawned = false;

static CoreRedrawCallback g_redraw_cb = NULL;
static void *g_redraw_user = NULL;

static void core_request_redraw(void) {
    if (g_redraw_cb)
        g_redraw_cb(g_redraw_user);
}

static void log_line(const char *channel, const char *direction, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "[%s] %s: ", channel, direction);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    fflush(stdout);
    va_end(ap);
}

static void log_bytes(const char *channel,
                      const char *direction,
                      const char *label,
                      const unsigned char *buf,
                      size_t len) {
    char hex[512];
    char ascii[256];
    size_t hpos = 0, apos = 0;

    for (size_t i = 0; i < len; ++i) {
        if (hpos + 4 >= sizeof(hex))
            break;
        hpos += snprintf(hex + hpos, sizeof(hex) - hpos, "%02X ", buf[i]);

        if (apos + 2 >= sizeof(ascii))
            break;
        ascii[apos++] = (buf[i] >= 32 && buf[i] < 127) ? buf[i] : '.';
    }

    if (hpos > 0 && hex[hpos - 1] == ' ')
        hex[hpos - 1] = '\0';
    ascii[apos] = '\0';

    log_line(channel, direction, "%s HEX: %s", label, hex);
    log_line(channel, direction, "%s ASCII: %s", label, ascii);
}

static const char *normalize_token(const char *tok) {
    if (!tok || !*tok)
        return NULL;
    if (strcmp(tok, "null") == 0)
        return NULL;
    return tok;
}

static char *run_cmd(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    static char buf[8192];
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[len] = 0;
    pclose(fp);
    return buf;
}

static void free_window(WindowInfo *win) {
    if (!win)
        return;
    if (win->label) {
        free(win->label);
        win->label = NULL;
    }
    if (win->thumb_b64) {
        free(win->thumb_b64);
        win->thumb_b64 = NULL;
    }
    if (win->class_name) {
        free(win->class_name);
        win->class_name = NULL;
    }
    if (win->initial_class) {
        free(win->initial_class);
        win->initial_class = NULL;
    }
    if (win->title) {
        free(win->title);
        win->title = NULL;
    }
    win->addr[0] = '\0';
    win->x = win->y = win->w = win->h = 0;
}

static void clear_all_windows(void) {
    for (int wsid = 0; wsid < MAX_WS; ++wsid) {
        WorkspaceWindows *W = &g_ws[wsid];
        for (int i = 0; i < W->count; ++i) {
            free_window(&W->wins[i]);
        }
        W->count = 0;
        g_workspace_names[wsid][0] = '\0';
    }
    memset(g_active_list, 0, sizeof(g_active_list));
    memset(g_active_names, 0, sizeof(g_active_names));
    g_active_count = 0;
}

static void ensure_workspace_name(int wsid) {
    if (wsid <= 0 || wsid >= MAX_WS)
        return;
    if (!g_workspace_names[wsid][0])
        snprintf(g_workspace_names[wsid], CORE_WS_NAME_LEN, "%d", wsid);
}

static void update_workspace_names(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "hyprctl -j workspaces 2>/dev/null | "
             "jq -r '.[] | \"\\(.id)|\\(.name)\"'");

    FILE *fp = popen(cmd, "r");
    if (!fp)
        return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!*line)
            continue;
        char *sep = strchr(line, '|');
        if (!sep)
            continue;
        *sep = 0;
        int id = atoi(line);
        if (id <= 0 || id >= MAX_WS)
            continue;
        const char *name = sep + 1;
        if (!name || !*name)
            snprintf(g_workspace_names[id], CORE_WS_NAME_LEN, "%d", id);
        else
            snprintf(g_workspace_names[id], CORE_WS_NAME_LEN, "%s", name);
    }

    pclose(fp);
    ensure_workspace_name(g_active_ws);
}

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

static int hypr_send_command(const char *command) {
    if (!command || !*command)
        return -1;
    if (!g_hypr_sock_cmd[0] && init_hypr_paths() < 0)
        return -1;

    int fd = connect_unix_socket(g_hypr_sock_cmd);
    if (fd < 0) {
        log_line("HyprlandSock", "ERROR", "connect(%s) failed: %s",
                 g_hypr_sock_cmd, strerror(errno));
        return -1;
    }

    char payload[512];
    int len = snprintf(payload, sizeof(payload), "%s", command);
    if (len < 0 || len >= (int)sizeof(payload)) {
        close(fd);
        return -1;
    }
    payload[len] = '\0';

    char printable[512];
    snprintf(printable, sizeof(printable), "%.*s",
             (int)(len > 0 && payload[len - 1] == '\n' ? len - 1 : len),
             payload);
    log_line("HyprlandSock", "OUTBOUND", "%s", printable);
    log_bytes("HyprlandSock", "OUTBOUND", "payload", (unsigned char *)payload, (size_t)len);

    ssize_t w = write(fd, payload, (size_t)len);
    if (w < 0 || (size_t)w != (size_t)len) {
        log_line("HyprlandSock", "ERROR", "write failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    char buf[256];
    ssize_t rr;
    while ((rr = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[rr] = 0;
        log_line("HyprlandSock", "INBOUND", "%s", buf);
    }

    close(fd);
    return 0;
}

static void sanitize_addr(char *s) {
    char out[64];
    int j = 0;
    for (int i = 0; s[i] && j < 63; i++) {
        char c = s[i];
        if ((c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F') ||
            c == 'x' || c == 'X') {
            out[j++] = c;
        } else {
            break;
        }
    }
    out[j] = 0;
    strcpy(s, out);
}

static void update_monitor_geometry(void) {
    FILE *fp = popen(
        "hyprctl -j monitors 2>/dev/null | "
        "jq -r '.[] | select(.focused == true) "
        "| \"\\(.id) \\(.width) \\(.height) \\(.x) \\(.y) \\(.transform // 0)\"'",
        "r"
    );
    if (!fp)
        return;

    int id, w, h, x, y, transform;
    if (fscanf(fp, "%d %d %d %d %d %d", &id, &w, &h, &x, &y, &transform) == 6) {
        if (w > 0 && h > 0) {
            g_mon_id = id;
            g_mon_w  = w;
            g_mon_h  = h;
            g_mon_x  = x;
            g_mon_y  = y;
            g_mon_transform = transform;
        }
    }

    pclose(fp);
}

static void update_active_workspace(void) {
    char *json = run_cmd("hyprctl -j activeworkspace 2>/dev/null");
    if (!json)
        return;

    char *p = strstr(json, "\"id\":");
    if (!p)
        return;

    int id = atoi(p + 5);
    if (id >= 1 && id < MAX_WS)
        g_active_ws = id;
}

static void update_workspace_windows(void) {
    clear_all_windows();

    int used_ws[MAX_WS] = {0};
    WindowInfo *capture_targets[MAX_WS * MAX_WINS_PER_WS];
    int capture_count = 0;

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "hyprctl -j clients 2>/dev/null | "
             "jq -r '.[] "
             "| select(.mapped == true and .hidden == false "
             "and .workspace.id != null) "
             "| \"\\(.address)|\\(.workspace.id)|\\(.monitor)|"
             "\\(.at[0])|\\(.at[1])|\\(.size[0])|\\(.size[1])|"
             "\\(.class)|\\(.initialClass)|\\(.title)\"'");

    FILE *fp = popen(cmd, "r");
    if (!fp)
        return;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!*line)
            continue;

        char *save = NULL;
        char *tok  = strtok_r(line, "|", &save);
        if (!tok)
            continue;

        char addr_clean[64];
        snprintf(addr_clean, sizeof(addr_clean), "%s", tok);
        sanitize_addr(addr_clean);

        tok = strtok_r(NULL, "|", &save);
        if (!tok)
            continue;
        int wsid = atoi(tok);
        if (wsid <= 0 || wsid >= MAX_WS)
            continue;

        tok = strtok_r(NULL, "|", &save);
        if (!tok)
            continue;
        int mon = atoi(tok);
        if (mon != g_mon_id)
            continue;

        WorkspaceWindows *W = &g_ws[wsid];
        if (W->count >= MAX_WINS_PER_WS)
            continue;

        WindowInfo *win = &W->wins[W->count];

        tok = strtok_r(NULL, "|", &save);
        if (!tok)
            continue;
        win->x = atoi(tok);

        tok = strtok_r(NULL, "|", &save);
        if (!tok)
            continue;
        win->y = atoi(tok);

        tok = strtok_r(NULL, "|", &save);
        if (!tok)
            continue;
        win->w = atoi(tok);

        tok = strtok_r(NULL, "|", &save);
        if (!tok)
            continue;
        win->h = atoi(tok);

        char *classTok = strtok_r(NULL, "|", &save);
        char *initialClassTok = strtok_r(NULL, "|", &save);
        char *titleTok = strtok_r(NULL, "|", &save);
        const char *classSrc = normalize_token(classTok);
        const char *initialSrc = normalize_token(initialClassTok);
        const char *titleSrc = normalize_token(titleTok);

        if (classSrc)
            win->class_name = strdup(classSrc);
        else
            win->class_name = NULL;

        if (initialSrc)
            win->initial_class = strdup(initialSrc);
        else
            win->initial_class = NULL;

        if (titleSrc)
            win->title = strdup(titleSrc);
        else
            win->title = NULL;

        const char *labelSrc = classSrc ? classSrc : (initialSrc ? initialSrc : titleSrc);
        win->label = labelSrc ? strdup(labelSrc) : NULL;

        snprintf(win->addr, sizeof(win->addr), "%s", addr_clean);
        win->thumb_b64 = NULL;

        if (win->addr[0] != '\0' && strcmp(win->addr, "0x0") != 0 &&
            capture_count < (int)(MAX_WS * MAX_WINS_PER_WS)) {
            capture_targets[capture_count++] = win;
        }

        W->count++;
        used_ws[wsid] = 1;
    }

    pclose(fp);

    capture_thumbnails_parallel(capture_targets, capture_count);

    if (g_active_ws > 0 && g_active_ws < MAX_WS)
        used_ws[g_active_ws] = 1;

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

        g_active_list[g_active_count] = wsid;
        ensure_workspace_name(wsid);
        snprintf(g_active_names[g_active_count], CORE_WS_NAME_LEN, "%s",
                 g_workspace_names[wsid]);
        g_active_count++;
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
            g_active_list[g_active_count] = g_active_ws;
            ensure_workspace_name(g_active_ws);
            snprintf(g_active_names[g_active_count], CORE_WS_NAME_LEN, "%s",
                     g_workspace_names[g_active_ws]);
            g_active_count++;
        }
    } else if (g_active_count == 0) {
        g_active_list[0] = 1;
        ensure_workspace_name(1);
        snprintf(g_active_names[0], CORE_WS_NAME_LEN, "%s",
                 g_workspace_names[1]);
        g_active_count = 1;
    }
}

static void refresh_full_state(void) {
    pthread_mutex_lock(&g_state_lock);
    update_active_workspace();
    update_monitor_geometry();
    update_workspace_windows();
    pthread_mutex_unlock(&g_state_lock);
    core_request_redraw();
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

static void *hypr_event_thread(void *data) {
    (void)data;

    while (g_event_thread_running) {
        if (!g_hypr_sock_evt[0] && init_hypr_paths() < 0) {
            sleep(1);
            continue;
        }

        int fd = connect_unix_socket(g_hypr_sock_evt);
        if (fd < 0) {
            log_line("HyprlandSock", "ERROR", "connect(%s) failed: %s",
                     g_hypr_sock_evt, strerror(errno));
            sleep(1);
            continue;
        }

        FILE *fp = fdopen(fd, "r");
        if (!fp) {
            close(fd);
            sleep(1);
            continue;
        }

        char line[512];
        while (g_event_thread_running && fgets(line, sizeof(line), fp)) {
            char *nl = strchr(line, '\n');
            if (nl)
                *nl = 0;

            char *sep = strstr(line, ">>");
            if (!sep)
                continue;
            *sep = 0;
            const char *payload = sep + 2;

            log_line("HyprlandSock", "INBOUND", "%s %s", line, payload);

            if (event_requires_refresh(line)) {
                log_line("HyprlandSock", "INFO", "Refreshing state due to %s", line);
                refresh_full_state();
            }
        }

        fclose(fp);
        sleep(1);
    }

    return NULL;
}

static void copy_window_to_core(const WindowInfo *src, CoreWindow *dst) {
    dst->x = src->x;
    dst->y = src->y;
    dst->w = src->w;
    dst->h = src->h;
    strncpy(dst->addr, src->addr, sizeof(dst->addr));
    dst->addr[sizeof(dst->addr) - 1] = '\0';
    dst->label = src->label ? strdup(src->label) : NULL;
    dst->thumb_b64 = src->thumb_b64 ? strdup(src->thumb_b64) : NULL;
    dst->class_name = src->class_name ? strdup(src->class_name) : NULL;
    dst->initial_class = src->initial_class ? strdup(src->initial_class) : NULL;
    dst->title = src->title ? strdup(src->title) : NULL;
}

int core_init(CoreRedrawCallback cb, void *user_data) {
    g_redraw_cb = cb;
    g_redraw_user = user_data;

    if (init_hypr_paths() < 0) {
        fprintf(stderr, "[core] failed to resolve Hyprland IPC paths\n");
        return -1;
    }

    g_event_thread_running = true;
    if (pthread_create(&g_event_thread, NULL, hypr_event_thread, NULL) != 0) {
        fprintf(stderr, "[core] failed to start event listener thread\n");
        g_event_thread_running = false;
        return -1;
    }
    g_event_thread_spawned = true;

    refresh_full_state();
    return 0;
}

void core_shutdown(void) {
    g_event_thread_running = false;
    if (g_event_thread_spawned) {
        pthread_join(g_event_thread, NULL);
        g_event_thread_spawned = false;
    }

    pthread_mutex_lock(&g_state_lock);
    clear_all_windows();
    pthread_mutex_unlock(&g_state_lock);
}

void core_copy_state(CoreState *out_state) {
    if (!out_state)
        return;

    core_free_state(out_state);
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
    memcpy(out_state->active_names, g_active_names, sizeof(g_active_names));

    for (int wsid = 0; wsid < MAX_WS; ++wsid) {
        CoreWorkspace *dst_ws = &out_state->workspaces[wsid];
        WorkspaceWindows *src_ws = &g_ws[wsid];
        dst_ws->count = src_ws->count;
        for (int j = 0; j < src_ws->count; ++j) {
            copy_window_to_core(&src_ws->wins[j], &dst_ws->wins[j]);
        }
    }

    pthread_mutex_unlock(&g_state_lock);
}

void core_free_state(CoreState *state) {
    if (!state)
        return;
    for (int wsid = 0; wsid < MAX_WS; ++wsid) {
        CoreWorkspace *ws = &state->workspaces[wsid];
        for (int j = 0; j < ws->count; ++j) {
            free(ws->wins[j].label);
            ws->wins[j].label = NULL;
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

void core_move_window(const char *addr, int wsid) {
    if (!addr || !addr[0] || wsid <= 0)
        return;

    char addr_clean[64];
    snprintf(addr_clean, sizeof(addr_clean), "%s", addr);
    sanitize_addr(addr_clean);

    char ws[16];
    snprintf(ws, sizeof(ws), "%d", wsid);

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "dispatch movetoworkspacesilent %s,address:%s",
             ws, addr_clean);
    log_line("OverviewCore", "workspace_request", "%s", cmd);
    hypr_send_command(cmd);
}

void core_switch_workspace(const char *name, int wsid) {
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

    hypr_send_command(cmd);
    refresh_full_state();
}

void core_focus_window(const char *addr) {
    if (!addr || !addr[0])
        return;

    char addr_clean[64];
    snprintf(addr_clean, sizeof(addr_clean), "%s", addr);
    sanitize_addr(addr_clean);

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "dispatch focuswindow address:%s",
             addr_clean);
    hypr_send_command(cmd);
}

char *core_capture_window_raw(const char *addr) {
    if (!addr || !addr[0])
        return NULL;
    char addr_clean[64];
    snprintf(addr_clean, sizeof(addr_clean), "%s", addr);
    sanitize_addr(addr_clean);
    return capture_window_ppm_base64_with_limit(addr_clean, 0);
}


