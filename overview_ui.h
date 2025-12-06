#ifndef OVERVIEW_UI_H
#define OVERVIEW_UI_H

#include <stdbool.h>

void overview_ui_init(const char *config_path);
void overview_ui_shutdown(void);
void overview_ui_sync_with_core(void);
void overview_ui_request_show(void);
void overview_ui_request_hide(void);
void overview_ui_request_quit(void);
bool overview_ui_is_visible(void);
void overview_ui_core_redraw_callback(void *user_data);

#endif /* OVERVIEW_UI_H */


