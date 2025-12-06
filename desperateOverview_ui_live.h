#ifndef DESPERATEOVERVIEW_UI_LIVE_H
#define DESPERATEOVERVIEW_UI_LIVE_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include "desperateOverview_types.h"

typedef void (*DesperateOverviewLiveApply)(WindowInfo *win, GdkPixbuf *pixbuf, gpointer user_data);

void desperateOverview_ui_live_init(DesperateOverviewLiveApply cb, gpointer user_data);
void desperateOverview_ui_build_live_previews(int active_workspace,
                                              WorkspaceWindows workspaces[]);

#endif /* DESPERATEOVERVIEW_UI_LIVE_H */

