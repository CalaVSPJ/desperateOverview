#ifndef DESPERATEOVERVIEW_UI_H
#define DESPERATEOVERVIEW_UI_H

#include <stdbool.h>

void desperateOverview_ui_init(const char *config_path);
void desperateOverview_ui_shutdown(void);
void desperateOverview_ui_sync_with_core(void);
void desperateOverview_ui_request_show(void);
void desperateOverview_ui_request_hide(void);
void desperateOverview_ui_request_quit(void);
bool desperateOverview_ui_is_visible(void);
void desperateOverview_ui_core_redraw_callback(void *user_data);

#endif /* DESPERATEOVERVIEW_UI_H */


