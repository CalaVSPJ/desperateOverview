#ifndef DESPERATEOVERVIEW_TYPES_H
#define DESPERATEOVERVIEW_TYPES_H

#include <glib.h>
#include <stdint.h>
#include "desperateOverview_core.h"

typedef struct _GdkPixbuf GdkPixbuf;

typedef struct {
    int  x, y, w, h;
    char  addr[64];
    GdkPixbuf *thumb_pixbuf;
    GdkPixbuf *live_pixbuf;
    char *thumb_b64;
    char *class_name;
    char *initial_class;
    char *title;
    double top_preview_x, top_preview_y, top_preview_w, top_preview_h;
    gboolean top_preview_valid;
    double bottom_preview_x, bottom_preview_y, bottom_preview_w, bottom_preview_h;
    gboolean bottom_preview_valid;
} WindowInfo;

typedef struct {
    int count;
    WindowInfo wins[MAX_WINS_PER_WS];
    char name[CORE_WS_NAME_LEN];
} WorkspaceWindows;

#endif /* DESPERATEOVERVIEW_TYPES_H */


