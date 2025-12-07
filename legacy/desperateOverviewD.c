// desperateOverviewD.c
// Daemon that exposes Hyprland state over a UNIX socket and
// also captures per-window thumbnails via Hyprland's toplevel export.
//
// Protocol to UI:
//   MON|mon_id|w|h|x|y|active_ws
//   WIN|addr|wsid|mon|x|y|w|h|label|B64:<base64-ppm-data>
//   END
//
// Thumbnail details:
//   - Captured via Wayland hyprland_toplevel_export_v1
//   - Downscaled to max width THUMB_MAX_W (keeps aspect ratio)
//   - Stored as binary PPM (P6), then base64-encoded inline.
//   - No tmp files, no external capture binary needed.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <pthread.h>
#include <limits.h>
#include <stdarg.h>

// Wayland / Hyprland capture
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include "hyprland-toplevel-export-v1-client-protocol.h"

#define MAX_WS          32
#define MAX_WINS_PER_WS 32
// Maximum thumbnail width to keep WIN lines under ~64k buffer
// 160px on a 16:9 window => ~57kB base64 PPM, safe for the 64k UI buffer.
#define THUMB_MAX_W 512

/* ---------- Global monitor / workspace state ---------- */

static int g_mon_id    = 0;
static int g_mon_w     = 1920;
static int g_mon_h     = 1080;
static int g_mon_x     = 0;
static int g_mon_y     = 0;
static int g_active_ws = 1;

typedef struct {
    int  x, y, w, h;
    char *label;        /* UTF-8, may be NULL */
    char  addr[64];     /* Hyprland window address, for focus/move */
    char *thumb_b64;    /* Base64 encoded thumbnail data */
} WindowInfo;

typedef struct {
    int count;
    WindowInfo wins[MAX_WINS_PER_WS];
} WorkspaceWindows;

static WorkspaceWindows g_ws[MAX_WS];
static int              g_active_list[MAX_WS];
static int              g_active_count = 0;

static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_hypr_sock_cmd[PATH_MAX];
static char g_hypr_sock_evt[PATH_MAX];
static pthread_t g_event_thread;
static bool g_event_thread_running = false;
static bool g_event_thread_spawned = false;

static void log_line(const char *channel, const char *direction, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void log_line(const char *channel, const char *direction, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "[%s] %s: ", channel, direction);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    fflush(stdout);
    va_end(ap);
}
/* ---------- Helpers ---------- */

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
    }
    memset(g_active_list, 0, sizeof(g_active_list));
    g_active_count = 0;
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
    if (len == 0 || payload[len - 1] != '\n') {
        if (len + 1 >= (int)sizeof(payload)) {
            close(fd);
            return -1;
        }
        payload[len++] = '\n';
        payload[len] = 0;
    }

    char printable[512];
    snprintf(printable, sizeof(printable), "%.*s",
             (int)(len > 0 && payload[len - 1] == '\n' ? len - 1 : len),
             payload);
    log_line("HyprlandSock", "OUTBOUND", "%s", printable);

    ssize_t w = write(fd, payload, (size_t)len);
    if (w < 0 || (size_t)w != (size_t)len) {
        log_line("HyprlandSock", "ERROR", "write failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    /* Drain response (if any) */
    char buf[256];
    ssize_t rr;
    while ((rr = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[rr] = 0;
        log_line("HyprlandSock", "INBOUND", "%s", buf);
    }

    close(fd);
    return 0;
}

static void refresh_full_state(void);
static void *hypr_event_thread(void *data);


/* keep a hex-only window address for hyprctl dispatch */
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

/* ---------- Monitor & workspace updates ---------- */

/* focused monitor (id, width, height, x, y) */
static void update_monitor_geometry(void) {
    FILE *fp = popen(
        "hyprctl -j monitors 2>/dev/null | "
        "jq -r '.[] | select(.focused == true) "
        "| \"\\(.id) \\(.width) \\(.height) \\(.x) \\(.y)\"'",
        "r"
    );
    if (!fp)
        return;

    int id, w, h, x, y;
    if (fscanf(fp, "%d %d %d %d %d", &id, &w, &h, &x, &y) == 5) {
        if (w > 0 && h > 0) {
            g_mon_id = id;
            g_mon_w  = w;
            g_mon_h  = h;
            g_mon_x  = x;
            g_mon_y  = y;
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

/* fill g_ws[], g_active_list[], g_active_count with current windows
 * on the focused monitor, using jq (like original working version).
 */
static char *capture_window_ppm_base64(const char *addr_hex);

static void update_workspace_windows(void) {
    clear_all_windows();

    int used_ws[MAX_WS] = {0};

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "hyprctl -j clients 2>/dev/null | "
             "jq -r '.[] "
             "| select(.mapped == true and .hidden == false "
             "and .workspace.id != null) "
             "| \"\\(.address)|\\(.workspace.id)|\\(.monitor)|"
             "\\(.at[0])|\\(.at[1])|\\(.size[0])|\\(.size[1])|"
             "\\(.class)|\\(.title)\"'");

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

        /* address */
        char addr_clean[64];
        snprintf(addr_clean, sizeof(addr_clean), "%s", tok);
        sanitize_addr(addr_clean);

        /* workspace id */
        tok = strtok_r(NULL, "|", &save);
        if (!tok)
            continue;
        int wsid = atoi(tok);
        if (wsid <= 0 || wsid >= MAX_WS)
            continue;

        /* monitor id */
        tok = strtok_r(NULL, "|", &save);
        if (!tok)
            continue;
        int mon = atoi(tok);

        /* only take windows on the focused monitor */
        if (mon != g_mon_id)
            continue;

        WorkspaceWindows *W = &g_ws[wsid];
        if (W->count >= MAX_WINS_PER_WS)
            continue;

        WindowInfo *win = &W->wins[W->count];

        /* x, y, w, h */
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

        /* labels */
        char *classTok = strtok_r(NULL, "|", &save);
        char *titleTok = strtok_r(NULL, "|", &save);
        if (!classTok)
            classTok = (char *)"";
        if (!titleTok)
            titleTok = (char *)"";

        const char *src = (*classTok) ? classTok : titleTok;
        if (src && *src && *src != '\0') {
            win->label = strdup(src);
        } else {
            win->label = NULL;
        }

        snprintf(win->addr, sizeof(win->addr), "%s", addr_clean);
        win->thumb_b64 = NULL;

        if (win->addr[0] != '\0' && strcmp(win->addr, "0x0") != 0) {
            win->thumb_b64 = capture_window_ppm_base64(win->addr);
        }

        W->count++;
        used_ws[wsid] = 1;
    }

    pclose(fp);

    /* Rebuild workspace list (keep active ws even if empty) */
    if (g_active_ws > 0 && g_active_ws < MAX_WS)
        used_ws[g_active_ws] = 1;

    g_active_count = 0;
    for (int wsid = 1; wsid < MAX_WS; ++wsid) {
        if (used_ws[wsid])
            g_active_list[g_active_count++] = wsid;
    }

    if (g_active_count == 0) {
        g_active_list[0] =
            (g_active_ws > 0 && g_active_ws < MAX_WS) ? g_active_ws : 1;
        g_active_count = 1;
    }

    /* ensure active workspace is present */
    int have_active = 0;
    for (int i = 0; i < g_active_count; ++i) {
        if (g_active_list[i] == g_active_ws) {
            have_active = 1;
            break;
        }
    }
    if (!have_active && g_active_ws > 0 && g_active_ws < MAX_WS) {
        g_active_list[g_active_count++] = g_active_ws;
    }
}

static bool event_requires_refresh(const char *event_name) {
    if (!event_name || !*event_name)
        return false;

    static const char *kEvents[] = {
        "workspace",
        "workspacev2",
        "focusedmon",
        "focusedmonv2",
        "openwindow",
        "closewindow",
        "movewindow",
        "movewindowv2",
        "activewindow",
        "activewindowv2",
        "changefloatingmode",
        "urgent",
        "windowtitle",
        "windowtitlev2",
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
        /* reconnect loop */
        sleep(1);
    }

    return NULL;
}

/* ---------- Base64 encode (no newlines) ---------- */

static char *base64_encode(const unsigned char *src, size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    size_t out_len = 4 * ((len + 2) / 3);
    char *out = malloc(out_len + 1);
    if (!out)
        return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? src[i++] : 0;
        uint32_t octet_b = i < len ? src[i++] : 0;
        uint32_t octet_c = i < len ? src[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out[j++] = tbl[(triple >> 18) & 0x3F];
        out[j++] = tbl[(triple >> 12) & 0x3F];
        out[j++] = (i > len + 1) ? '=' : tbl[(triple >> 6) & 0x3F];
        out[j++] = (i > len)     ? '=' : tbl[triple & 0x3F];
    }
    out[j] = 0;
    return out;
}

/* ---------- Wayland capture plumbing (from former hypr_window_capture) ---------- */

struct CaptureState {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct hyprland_toplevel_export_manager_v1 *export_manager;
    struct hyprland_toplevel_export_frame_v1 *frame;

    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;

    uint32_t shm_format;
    uint32_t width, height, stride;
    void *shm_data;
    size_t shm_size;

    bool got_buffer_info;
    bool got_ready;
    bool got_failed;
};

static int create_shm_file(size_t size) {
    char name[] = "/hyprthumb-XXXXXX";
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        perror("shm_open");
        return -1;
    }
    shm_unlink(name);
    if (ftruncate(fd, (off_t)size) < 0) {
        perror("ftruncate");
        close(fd);
        return -1;
    }
    return fd;
}

/* registry listener */

static void reg_global(void *data, struct wl_registry *registry,
                       uint32_t name, const char *interface, uint32_t version) {
    struct CaptureState *st = data;

    if (strcmp(interface, "wl_shm") == 0) {
        st->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "hyprland_toplevel_export_manager_v1") == 0) {
        uint32_t ver = version < 2 ? version : 2;
        st->export_manager = wl_registry_bind(
            registry, name,
            &hyprland_toplevel_export_manager_v1_interface,
            ver
        );
    }
}

static void reg_global_remove(void *data, struct wl_registry *registry,
                              uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener reg_listener = {
    .global        = reg_global,
    .global_remove = reg_global_remove,
};

/* frame listener */

static void frame_handle_buffer(void *data,
                                struct hyprland_toplevel_export_frame_v1 *frame,
                                uint32_t format,
                                uint32_t width,
                                uint32_t height,
                                uint32_t stride) {
    struct CaptureState *st = data;
    (void)frame;

    st->shm_format = format;
    st->width      = width;
    st->height     = height;
    st->stride     = stride;
    st->shm_size   = (size_t)stride * height;

    int fd = create_shm_file(st->shm_size);
    if (fd < 0) {
        fprintf(stderr, "capture: failed to create shm file\n");
        return;
    }

    st->shm_data = mmap(NULL, st->shm_size,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    if (st->shm_data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        st->shm_data = NULL;
        return;
    }

    st->pool = wl_shm_create_pool(st->shm, fd, (int)st->shm_size);
    close(fd);

    st->buffer = wl_shm_pool_create_buffer(
        st->pool,
        0,
        (int)width,
        (int)height,
        (int)stride,
        st->shm_format
    );

    st->got_buffer_info = true;
}

static void frame_handle_linux_dmabuf(void *data,
                                      struct hyprland_toplevel_export_frame_v1 *frame,
                                      uint32_t format,
                                      uint32_t width,
                                      uint32_t height) {
    (void)data;
    (void)frame;
    (void)format;
    (void)width;
    (void)height;
    /* ignore dmabuf, we only handle wl_shm */
}

static void frame_handle_buffer_done(void *data,
                                     struct hyprland_toplevel_export_frame_v1 *frame) {
    struct CaptureState *st = data;
    (void)frame;

    if (!st->got_buffer_info) {
        fprintf(stderr, "capture: buffer_done but no buffer info\n");
        return;
    }

    /* ignore_damage = 1 => copy entire buffer right now */
    hyprland_toplevel_export_frame_v1_copy(st->frame, st->buffer, 1);
}

static void frame_handle_damage(void *data,
                                struct hyprland_toplevel_export_frame_v1 *frame,
                                uint32_t x, uint32_t y,
                                uint32_t width, uint32_t height) {
    (void)data;
    (void)frame;
    (void)x; (void)y; (void)width; (void)height;
}

static void frame_handle_flags(void *data,
                               struct hyprland_toplevel_export_frame_v1 *frame,
                               uint32_t flags) {
    (void)data;
    (void)frame;
    (void)flags;
}

static void frame_handle_ready(void *data,
                               struct hyprland_toplevel_export_frame_v1 *frame,
                               uint32_t tv_sec_hi,
                               uint32_t tv_sec_lo,
                               uint32_t tv_nsec) {
    struct CaptureState *st = data;
    (void)frame;
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;

    st->got_ready = true;
}

static void frame_handle_failed(void *data,
                                struct hyprland_toplevel_export_frame_v1 *frame) {
    struct CaptureState *st = data;
    (void)frame;
    st->got_failed = true;
}

static const struct hyprland_toplevel_export_frame_v1_listener frame_listener = {
    .buffer      = frame_handle_buffer,
    .linux_dmabuf= frame_handle_linux_dmabuf,
    .buffer_done = frame_handle_buffer_done,
    .damage      = frame_handle_damage,
    .flags       = frame_handle_flags,
    .ready       = frame_handle_ready,
    .failed      = frame_handle_failed,
};

/* Convert captured BGRA/BGRx shm into a downscaled PPM in memory,
   then base64-encode it. Caller frees the returned char* */
static char *capture_window_ppm_base64(const char *addr_hex) {
    if (!addr_hex || !addr_hex[0])
        return NULL;

    unsigned long long addr64 = strtoull(addr_hex, NULL, 16);
    if (addr64 == 0 && addr_hex[0] != '0')
        return NULL;

    uint32_t handle = (uint32_t)addr64;

    struct CaptureState st;
    memset(&st, 0, sizeof(st));

    st.display = wl_display_connect(NULL);
    if (!st.display)
        return NULL;

    st.registry = wl_display_get_registry(st.display);
    if (!st.registry) {
        wl_display_disconnect(st.display);
        return NULL;
    }

    wl_registry_add_listener(st.registry, &reg_listener, &st);
    wl_display_roundtrip(st.display);

    if (!st.shm || !st.export_manager) {
        if (st.registry) wl_registry_destroy(st.registry);
        if (st.display) wl_display_disconnect(st.display);
        return NULL;
    }

    st.frame = hyprland_toplevel_export_manager_v1_capture_toplevel(
        st.export_manager,
        0,      /* overlay_cursor = 0 */
        handle
    );
    if (!st.frame) {
        if (st.export_manager) hyprland_toplevel_export_manager_v1_destroy(st.export_manager);
        if (st.registry) wl_registry_destroy(st.registry);
        if (st.display) wl_display_disconnect(st.display);
        return NULL;
    }

    hyprland_toplevel_export_frame_v1_add_listener(st.frame, &frame_listener, &st);

    while (!st.got_ready && !st.got_failed) {
        if (wl_display_dispatch(st.display) < 0) {
            perror("wl_display_dispatch");
            break;
        }
    }
    wl_display_roundtrip(st.display);

    char *b64 = NULL;

    if (st.got_ready && !st.got_failed && st.shm_data && st.width > 0 && st.height > 0) {
        /* downscale & write PPM to memory */
        const uint32_t src_w = st.width;
        const uint32_t src_h = st.height;

        double scale = 1.0;
        if (src_w > THUMB_MAX_W)
            scale = (double)THUMB_MAX_W / (double)src_w;

        uint32_t out_w = (uint32_t)(src_w * scale + 0.5);
        uint32_t out_h = (uint32_t)(src_h * scale + 0.5);
        if (out_w == 0) out_w = 1;
        if (out_h == 0) out_h = 1;

        char header[64];
        int header_len = snprintf(header, sizeof(header),
                                  "P6\n%u %u\n255\n", out_w, out_h);
        if (header_len < 0 || header_len >= (int)sizeof(header))
            header_len = 0;

        size_t pixels_bytes = (size_t)out_w * (size_t)out_h * 3;
        size_t total = (size_t)header_len + pixels_bytes;

        unsigned char *ppm = malloc(total);
        if (ppm) {
            memcpy(ppm, header, (size_t)header_len);

            uint8_t *dst = ppm + header_len;
            uint8_t *src_base = (uint8_t *)st.shm_data;

            for (uint32_t oy = 0; oy < out_h; ++oy) {
                uint32_t sy = (uint32_t)((double)oy / scale);
                if (sy >= src_h) sy = src_h - 1;

                uint8_t *src_row = src_base + sy * st.stride;

                for (uint32_t ox = 0; ox < out_w; ++ox) {
                    uint32_t sx = (uint32_t)((double)ox / scale);
                    if (sx >= src_w) sx = src_w - 1;

                    uint8_t *p = src_row + sx * 4; /* assume BGRx */

                    *dst++ = p[2]; /* R */
                    *dst++ = p[1]; /* G */
                    *dst++ = p[0]; /* B */
                }
            }

            b64 = base64_encode(ppm, total);
            free(ppm);
        }
    }

    if (st.buffer) wl_buffer_destroy(st.buffer);
    if (st.pool) wl_shm_pool_destroy(st.pool);
    if (st.shm_data && st.shm_data != MAP_FAILED)
        munmap(st.shm_data, st.shm_size);
    if (st.frame) hyprland_toplevel_export_frame_v1_destroy(st.frame);
    if (st.export_manager) hyprland_toplevel_export_manager_v1_destroy(st.export_manager);
    if (st.registry) wl_registry_destroy(st.registry);
    if (st.display) wl_display_disconnect(st.display);

    return b64;
}

/* ---------- STATE response ---------- */

/* send current state to client_fd in line protocol:
 * MON|mon_id|w|h|x|y|active_ws
 * WIN|addr|wsid|mon|x|y|w|h|label|B64:<thumb> or "-"
 * END
 */
/* ---------- main() ---------- */

int main(void) {
    /* don't die if client disconnects while we're writing */
    signal(SIGPIPE, SIG_IGN);

    if (init_hypr_paths() < 0) {
        fprintf(stderr, "[daemon] failed to resolve Hyprland IPC paths\n");
    }

    g_event_thread_running = true;
    if (pthread_create(&g_event_thread, NULL, hypr_event_thread, NULL) != 0) {
        fprintf(stderr, "[daemon] failed to start event listener thread\n");
        g_event_thread_running = false;
    } else {
        g_event_thread_spawned = true;
    }

    refresh_full_state();

    g_event_thread_running = false;
    if (g_event_thread_spawned) {
        pthread_join(g_event_thread, NULL);
    }

    return 0;
}
