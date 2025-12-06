#ifndef THUMBNAIL_CAPTURE_H
#define THUMBNAIL_CAPTURE_H

#include "overview_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void capture_thumbnails_parallel(WindowInfo **wins, int count);
char *capture_window_ppm_base64(const char *addr_hex);
char *capture_window_ppm_base64_with_limit(const char *addr_hex, uint32_t max_w);

#ifdef __cplusplus
}
#endif

#endif /* THUMBNAIL_CAPTURE_H */


