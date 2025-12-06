#ifndef DESPERATEOVERVIEW_CORE_H
#define DESPERATEOVERVIEW_CORE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_WS          32
#define MAX_WINS_PER_WS 32
#define CORE_WS_NAME_LEN 64
#define CORE_WS_NAME_LEN 64

typedef struct {
    int  x, y, w, h;
    char addr[64];
    char *thumb_b64;  /* caller must free copy */
    char *class_name;
    char *initial_class;
    char *title;
} CoreWindow;

typedef struct {
    int count;
    CoreWindow wins[MAX_WINS_PER_WS];
} CoreWorkspace;

typedef struct {
    int mon_id;
    int mon_width;
    int mon_height;
    int mon_off_x;
    int mon_off_y;
    int mon_transform;
    int active_workspace;
    int active_count;
    int active_list[MAX_WS];
    char active_names[MAX_WS][CORE_WS_NAME_LEN];
    CoreWorkspace workspaces[MAX_WS];
} CoreState;

typedef void (*CoreRedrawCallback)(void *user_data);

int  core_init(CoreRedrawCallback cb, void *user_data);
void core_shutdown(void);

void core_copy_state(CoreState *out_state);
void core_free_state(CoreState *state);

void core_move_window(const char *addr, int wsid);
void core_switch_workspace(const char *name, int wsid);
void core_focus_window(const char *addr);
char *core_capture_window_raw(const char *addr);

#ifdef __cplusplus
}
#endif

#endif /* DESPERATEOVERVIEW_CORE_H */


