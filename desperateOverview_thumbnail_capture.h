#ifndef DESPERATEOVERVIEW_THUMBNAIL_CAPTURE_H
#define DESPERATEOVERVIEW_THUMBNAIL_CAPTURE_H

#include "desperateOverview_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void capture_thumbnails_parallel(WindowInfo **wins, int count);
char *capture_window_ppm_base64(const char *addr_hex);
char *capture_window_ppm_base64_with_limit(const char *addr_hex, uint32_t max_w);
typedef void (*WindowCaptureTask)(WindowInfo *win);
void capture_windows_parallel(WindowInfo **wins, int count, WindowCaptureTask task);

#ifdef __cplusplus
}
#endif

#endif /* DESPERATEOVERVIEW_THUMBNAIL_CAPTURE_H */


