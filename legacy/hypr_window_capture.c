// hypr_window_capture.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include "hyprland-toplevel-export-v1-client-protocol.h"  // generated

struct state {
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

static void
registry_global(void *data, struct wl_registry *registry,
                uint32_t name, const char *interface, uint32_t version)
{
    struct state *st = data;

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

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int
create_shm_file(size_t size)
{
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

/* ---- frame events ---- */

static void
frame_handle_buffer(void *data,
                    struct hyprland_toplevel_export_frame_v1 *frame,
                    uint32_t format,
                    uint32_t width,
                    uint32_t height,
                    uint32_t stride)
{
    struct state *st = data;
    (void)frame;

    st->shm_format = format;
    st->width = width;
    st->height = height;
    st->stride = stride;
    st->shm_size = (size_t)stride * height;

    int fd = create_shm_file(st->shm_size);
    if (fd < 0) {
        fprintf(stderr, "failed to create shm file\n");
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

static void
frame_handle_linux_dmabuf(void *data,
                          struct hyprland_toplevel_export_frame_v1 *frame,
                          uint32_t format,
                          uint32_t width,
                          uint32_t height)
{
    (void)data;
    (void)frame;
    (void)format;
    (void)width;
    (void)height;
    /* We ignore dmabuf and just use wl_shm. */
}

static void
frame_handle_buffer_done(void *data,
                         struct hyprland_toplevel_export_frame_v1 *frame)
{
    struct state *st = data;
    (void)frame;

    if (!st->got_buffer_info) {
        fprintf(stderr, "buffer_done but no buffer info\n");
        return;
    }

    /* ignore_damage = 1 (copy immediately) */
    hyprland_toplevel_export_frame_v1_copy(st->frame, st->buffer, 1);
}

static void
frame_handle_damage(void *data,
                    struct hyprland_toplevel_export_frame_v1 *frame,
                    uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height)
{
    (void)data;
    (void)frame;
    (void)x; (void)y; (void)width; (void)height;
}

static void
frame_handle_flags(void *data,
                   struct hyprland_toplevel_export_frame_v1 *frame,
                   uint32_t flags)
{
    (void)data;
    (void)frame;
    (void)flags;
}

static void
frame_handle_ready(void *data,
                   struct hyprland_toplevel_export_frame_v1 *frame,
                   uint32_t tv_sec_hi,
                   uint32_t tv_sec_lo,
                   uint32_t tv_nsec)
{
    struct state *st = data;
    (void)frame;
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;

    st->got_ready = true;
}

static void
frame_handle_failed(void *data,
                    struct hyprland_toplevel_export_frame_v1 *frame)
{
    struct state *st = data;
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

/* ---- write PPM ---- */

static int
write_ppm(const char *path, struct state *st)
{
    if (!st->shm_data || !st->got_ready)
        return -1;

    /* hard cap preview width to 400px; scale height proportionally */
    const uint32_t MAX_W = 400;

    uint32_t src_w = st->width;
    uint32_t src_h = st->height;

    double scale = 1.0;
    if (src_w > MAX_W)
        scale = (double)MAX_W / (double)src_w;

    uint32_t out_w = (uint32_t)(src_w * scale + 0.5);
    uint32_t out_h = (uint32_t)(src_h * scale + 0.5);
    if (out_w == 0) out_w = 1;
    if (out_h == 0) out_h = 1;

    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    fprintf(f, "P6\n%u %u\n255\n", out_w, out_h);

    /* src pixels: 4-byte (assume BGRx). We do a simple nearest-neighbor downscale. */
    uint8_t *src_row0 = st->shm_data;

    for (uint32_t oy = 0; oy < out_h; ++oy) {
        uint32_t sy = (uint32_t)((double)oy / scale);
        if (sy >= src_h) sy = src_h - 1;

        uint8_t *src_row = src_row0 + sy * st->stride;

        for (uint32_t ox = 0; ox < out_w; ++ox) {
            uint32_t sx = (uint32_t)((double)ox / scale);
            if (sx >= src_w) sx = src_w - 1;

            uint8_t *p = src_row + sx * 4;   /* [B,G,R,X] */

            uint8_t rgb[3] = { p[2], p[1], p[0] };
            fwrite(rgb, 1, 3, f);
        }
    }

    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr,
            "Usage:\n"
            "  %s <window_address_hex> <out.ppm or ->\n\n"
            "Where <window_address_hex> is exactly what hyprctl prints, e.g.:\n"
            "  Window 55777df43500 -> ...  =>  use 55777df43500\n",
            argv[0]);
        return 1;
    }

    const char *addr_hex = argv[1];
    const char *out_path = argv[2];

    unsigned long long addr64 = strtoull(addr_hex, NULL, 16);
    if (addr64 == 0 && addr_hex[0] != '0') {
        fprintf(stderr, "Failed to parse address '%s' as hex\n", addr_hex);
        return 1;
    }

    uint32_t handle = (uint32_t)addr64;

    fprintf(stderr, "Using handle = %u (from addr 0x%llx)\n",
            handle, addr64);

    struct state st = {0};

    st.display = wl_display_connect(NULL);
    if (!st.display) {
        fprintf(stderr, "failed to connect to Wayland display\n");
        return 1;
    }

    st.registry = wl_display_get_registry(st.display);
    wl_registry_add_listener(st.registry, &registry_listener, &st);
    wl_display_roundtrip(st.display);

    if (!st.shm || !st.export_manager) {
        fprintf(stderr, "missing wl_shm or hyprland_toplevel_export_manager_v1\n");
        wl_display_disconnect(st.display);
        return 1;
    }

    st.frame = hyprland_toplevel_export_manager_v1_capture_toplevel(
        st.export_manager,
        0,      /* overlay_cursor = 0 */
        handle
    );
    if (!st.frame) {
        fprintf(stderr, "capture_toplevel returned NULL\n");
        wl_display_disconnect(st.display);
        return 1;
    }

    hyprland_toplevel_export_frame_v1_add_listener(
        st.frame, &frame_listener, &st
    );

    while (!st.got_ready && !st.got_failed) {
        if (wl_display_dispatch(st.display) < 0) {
            perror("wl_display_dispatch");
            break;
        }
    }

    int ret = 0;
    if (st.got_ready && !st.got_failed) {
        if (write_ppm(out_path, &st) != 0) {
            fprintf(stderr, "failed to write output\n");
            ret = 1;
        } else {
            fprintf(stderr, "saved frame to %s\n", out_path);
        }
    } else {
        fprintf(stderr, "frame capture failed\n");
        ret = 1;
    }

    if (st.buffer) wl_buffer_destroy(st.buffer);
    if (st.pool) wl_shm_pool_destroy(st.pool);
    if (st.shm_data && st.shm_data != MAP_FAILED)
        munmap(st.shm_data, st.shm_size);
    if (st.frame) hyprland_toplevel_export_frame_v1_destroy(st.frame);
    if (st.export_manager) hyprland_toplevel_export_manager_v1_destroy(st.export_manager);
    if (st.registry) wl_registry_destroy(st.registry);
    if (st.display) wl_display_disconnect(st.display);

    return ret;
}
