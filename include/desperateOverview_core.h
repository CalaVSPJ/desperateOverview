#ifndef DESPERATEOVERVIEW_CORE_H
#define DESPERATEOVERVIEW_CORE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_WS          32
#define MAX_WINS_PER_WS 32
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
    char name[CORE_WS_NAME_LEN];
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
    CoreWorkspace workspaces[MAX_WS];
} CoreState;

typedef void (*CoreRedrawCallback)(void *user_data);

int  desperateOverview_core_init(CoreRedrawCallback cb, void *user_data);
void desperateOverview_core_shutdown(void);

void desperateOverview_core_copy_state(CoreState *out_state);
void desperateOverview_core_free_state(CoreState *state);

void desperateOverview_core_move_window(const char *addr, int wsid);
void desperateOverview_core_switch_workspace(const char *name, int wsid);
char *desperateOverview_core_capture_window_raw(const char *addr);
void desperateOverview_core_set_thumbnail_capture_enabled(bool enabled);
void desperateOverview_core_request_full_refresh(void);
bool desperateOverview_core_state_needs_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* DESPERATEOVERVIEW_CORE_H */


