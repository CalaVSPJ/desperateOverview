#ifndef DESPERATEOVERVIEW_UI_THUMB_CACHE_H
#define DESPERATEOVERVIEW_UI_THUMB_CACHE_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

void desperateOverview_thumb_cache_init(void);
void desperateOverview_thumb_cache_shutdown(void);

guint64 desperateOverview_thumb_cache_bump_generation(void);
void desperateOverview_thumb_cache_prune(guint64 generation);

GdkPixbuf *desperateOverview_thumb_cache_lookup(const char *addr,
                                                guint32 expected_crc,
                                                guint64 generation);
void desperateOverview_thumb_cache_store(const char *addr,
                                         guint32 crc,
                                         GdkPixbuf *pixbuf,
                                         guint64 generation);
guint32 desperateOverview_thumb_cache_crc(const char *b64);

#endif /* DESPERATEOVERVIEW_UI_THUMB_CACHE_H */

