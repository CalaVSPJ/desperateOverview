#define _GNU_SOURCE

#include "thumbnail_capture.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include "hyprland-toplevel-export-v1-client-protocol.h"

#define THUMB_MAX_W 512
#define MAX_CAPTURE_THREADS 4

typedef struct {
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
} CaptureState;

typedef struct {
    WindowInfo **wins;
    int count;
    int next_index;
    pthread_mutex_t lock;
} CaptureQueue;

static char *base64_encode(const unsigned char *src, size_t len);
static int create_shm_file(size_t size);
static void reg_global(void *data, struct wl_registry *registry,
                       uint32_t name, const char *interface, uint32_t version);
static void reg_global_remove(void *data, struct wl_registry *registry,
                              uint32_t name);
static void frame_handle_buffer(void *data,
                                struct hyprland_toplevel_export_frame_v1 *frame,
                                uint32_t format,
                                uint32_t width,
                                uint32_t height,
                                uint32_t stride);
static void frame_handle_linux_dmabuf(void *data,
                                      struct hyprland_toplevel_export_frame_v1 *frame,
                                      uint32_t format,
                                      uint32_t width,
                                      uint32_t height);
static void frame_handle_buffer_done(void *data,
                                     struct hyprland_toplevel_export_frame_v1 *frame);
static void frame_handle_damage(void *data,
                                struct hyprland_toplevel_export_frame_v1 *frame,
                                uint32_t x, uint32_t y,
                                uint32_t width, uint32_t height);
static void frame_handle_flags(void *data,
                               struct hyprland_toplevel_export_frame_v1 *frame,
                               uint32_t flags);
static void frame_handle_ready(void *data,
                               struct hyprland_toplevel_export_frame_v1 *frame,
                               uint32_t tv_sec_hi,
                               uint32_t tv_sec_lo,
                               uint32_t tv_nsec);
static void frame_handle_failed(void *data,
                                struct hyprland_toplevel_export_frame_v1 *frame);
static char *capture_window_ppm_base64_ex(const char *addr_hex, uint32_t max_w);
static void *capture_worker_thread(void *data);

static const struct wl_registry_listener reg_listener = {
    .global = reg_global,
    .global_remove = reg_global_remove,
};

static const struct hyprland_toplevel_export_frame_v1_listener frame_listener = {
    .buffer      = frame_handle_buffer,
    .linux_dmabuf= frame_handle_linux_dmabuf,
    .buffer_done = frame_handle_buffer_done,
    .damage      = frame_handle_damage,
    .flags       = frame_handle_flags,
    .ready       = frame_handle_ready,
    .failed      = frame_handle_failed,
};

void capture_thumbnails_parallel(WindowInfo **wins, int count) {
    if (count <= 0 || !wins)
        return;

    CaptureQueue queue = {
        .wins = wins,
        .count = count,
        .next_index = 0
    };
    pthread_mutex_init(&queue.lock, NULL);

    int thread_count = count < MAX_CAPTURE_THREADS ? count : MAX_CAPTURE_THREADS;
    pthread_t threads[MAX_CAPTURE_THREADS];

    for (int i = 0; i < thread_count; ++i) {
        if (pthread_create(&threads[i], NULL, capture_worker_thread, &queue) != 0)
            threads[i] = 0;
    }

    for (int i = 0; i < thread_count; ++i) {
        if (threads[i])
            pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&queue.lock);
}

char *capture_window_ppm_base64(const char *addr_hex) {
    return capture_window_ppm_base64_ex(addr_hex, THUMB_MAX_W);
}

char *capture_window_ppm_base64_with_limit(const char *addr_hex, uint32_t max_w) {
    return capture_window_ppm_base64_ex(addr_hex, max_w);
}

static void *capture_worker_thread(void *data) {
    CaptureQueue *queue = (CaptureQueue *)data;
    for (;;) {
        int idx;
        pthread_mutex_lock(&queue->lock);
        idx = queue->next_index++;
        pthread_mutex_unlock(&queue->lock);

        if (idx >= queue->count)
            break;

        WindowInfo *win = queue->wins[idx];
        if (!win || !win->addr[0])
            continue;

        free(win->thumb_b64);
        win->thumb_b64 = capture_window_ppm_base64(win->addr);
    }
    return NULL;
}

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

static int create_shm_file(size_t size) {
    char name[64];
    int fd = -1;
    for (int attempt = 0; attempt < 32; ++attempt) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        snprintf(name, sizeof(name), "/hyprthumb-%d-%ld-%d",
                 getpid(), ts.tv_nsec, attempt);
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            break;
        }
        if (errno != EEXIST)
            return -1;
        fd = -1;
    }
    if (fd < 0)
        return -1;

    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void reg_global(void *data, struct wl_registry *registry,
                       uint32_t name, const char *interface, uint32_t version) {
    CaptureState *st = data;

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

static void frame_handle_buffer(void *data,
                                struct hyprland_toplevel_export_frame_v1 *frame,
                                uint32_t format,
                                uint32_t width,
                                uint32_t height,
                                uint32_t stride) {
    CaptureState *st = data;
    (void)frame;

    st->shm_format = format;
    st->width      = width;
    st->height     = height;
    st->stride     = stride;
    st->shm_size   = (size_t)stride * height;

    int fd = create_shm_file(st->shm_size);
    if (fd < 0)
        return;

    st->shm_data = mmap(NULL, st->shm_size,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    if (st->shm_data == MAP_FAILED) {
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
}

static void frame_handle_buffer_done(void *data,
                                     struct hyprland_toplevel_export_frame_v1 *frame) {
    CaptureState *st = data;
    (void)frame;

    if (!st->got_buffer_info)
        return;

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
    CaptureState *st = data;
    (void)frame;
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;

    st->got_ready = true;
}

static void frame_handle_failed(void *data,
                                struct hyprland_toplevel_export_frame_v1 *frame) {
    CaptureState *st = data;
    (void)frame;
    st->got_failed = true;
}

static char *capture_window_ppm_base64_ex(const char *addr_hex, uint32_t max_w) {
    if (!addr_hex || !addr_hex[0])
        return NULL;

    unsigned long long addr64 = strtoull(addr_hex, NULL, 16);
    if (addr64 == 0 && addr_hex[0] != '0')
        return NULL;

    uint32_t handle = (uint32_t)addr64;

    CaptureState st;
    memset(&st, 0, sizeof(st));

    st.display = wl_display_connect(NULL);
    if (!st.display) {
        fprintf(stderr, "[thumb] wl_display_connect failed for %s\n", addr_hex);
        return NULL;
    }

    st.registry = wl_display_get_registry(st.display);
    if (!st.registry) {
        fprintf(stderr, "[thumb] wl_display_get_registry failed for %s\n", addr_hex);
        wl_display_disconnect(st.display);
        return NULL;
    }

    wl_registry_add_listener(st.registry, &reg_listener, &st);
    wl_display_roundtrip(st.display);

    if (!st.shm || !st.export_manager) {
        fprintf(stderr, "[thumb] missing shm/export_manager for %s\n", addr_hex);
        if (st.registry) wl_registry_destroy(st.registry);
        if (st.display) wl_display_disconnect(st.display);
        return NULL;
    }

    st.frame = hyprland_toplevel_export_manager_v1_capture_toplevel(
        st.export_manager,
        0,
        handle
    );
    if (!st.frame) {
        fprintf(stderr, "[thumb] capture_toplevel failed for %s\n", addr_hex);
        if (st.export_manager) hyprland_toplevel_export_manager_v1_destroy(st.export_manager);
        if (st.registry) wl_registry_destroy(st.registry);
        if (st.display) wl_display_disconnect(st.display);
        return NULL;
    }

    hyprland_toplevel_export_frame_v1_add_listener(st.frame, &frame_listener, &st);

    const int max_wait_ms = 500;
    int waited_ms = 0;
    int display_fd = wl_display_get_fd(st.display);

    wl_display_flush(st.display);
    while (!st.got_ready && !st.got_failed) {
        if (wl_display_prepare_read(st.display) < 0) {
            wl_display_dispatch_pending(st.display);
            wl_display_flush(st.display);
            continue;
        }

        struct pollfd pfd = {
            .fd = display_fd,
            .events = POLLIN,
        };

        int rc = poll(&pfd, 1, 50);
        if (rc < 0) {
            wl_display_cancel_read(st.display);
            break;
        }
        if (rc == 0) {
            wl_display_cancel_read(st.display);
            waited_ms += 50;
            if (waited_ms >= max_wait_ms)
                break;
            wl_display_flush(st.display);
            continue;
        }

        if (wl_display_read_events(st.display) < 0)
            break;
        wl_display_dispatch_pending(st.display);
        wl_display_flush(st.display);
    }
    wl_display_roundtrip(st.display);

    char *b64 = NULL;

    if (st.got_ready && !st.got_failed && st.shm_data && st.width > 0 && st.height > 0) {
        const uint32_t src_w = st.width;
        const uint32_t src_h = st.height;

        double scale = 1.0;
        if (max_w > 0 && src_w > max_w)
            scale = (double)max_w / (double)src_w;

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

                    uint8_t *p = src_row + sx * 4;

                    *dst++ = p[2];
                    *dst++ = p[1];
                    *dst++ = p[0];
                }
            }

            b64 = base64_encode(ppm, total);
            free(ppm);
        }
    }

    if (!st.got_ready || st.got_failed)
        fprintf(stderr, "[thumb] capture timeout/fail for %s (ready=%d failed=%d)\n",
                addr_hex, st.got_ready, st.got_failed);

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

