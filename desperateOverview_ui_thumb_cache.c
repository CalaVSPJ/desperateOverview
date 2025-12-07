#define _GNU_SOURCE

#include "desperateOverview_ui_thumb_cache.h"

#include <string.h>

typedef struct {
    guint32 crc;
    guint64 generation;
    GdkPixbuf *pixbuf;
} ThumbCacheEntry;

static GHashTable *g_thumb_cache = NULL;
static guint64     g_thumb_cache_generation = 0;

static void thumb_cache_entry_free(gpointer data) {
    ThumbCacheEntry *entry = data;
    if (!entry)
        return;
    if (entry->pixbuf)
        g_object_unref(entry->pixbuf);
    g_free(entry);
}

void desperateOverview_thumb_cache_init(void) {
    if (g_thumb_cache)
        return;
    g_thumb_cache = g_hash_table_new_full(g_str_hash,
                                          g_str_equal,
                                          g_free,
                                          thumb_cache_entry_free);
    g_thumb_cache_generation = 0;
}

void desperateOverview_thumb_cache_shutdown(void) {
    if (g_thumb_cache) {
        g_hash_table_destroy(g_thumb_cache);
        g_thumb_cache = NULL;
    }
    g_thumb_cache_generation = 0;
}

guint64 desperateOverview_thumb_cache_bump_generation(void) {
    if (++g_thumb_cache_generation == 0)
        g_thumb_cache_generation = 1;
    return g_thumb_cache_generation;
}

void desperateOverview_thumb_cache_prune(guint64 generation) {
    if (!g_thumb_cache)
        return;
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, g_thumb_cache);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ThumbCacheEntry *entry = value;
        if (entry->generation != generation)
            g_hash_table_iter_remove(&iter);
    }
}

static ThumbCacheEntry *thumb_cache_lookup(const char *addr) {
    if (!g_thumb_cache || !addr || !*addr)
        return NULL;
    return g_hash_table_lookup(g_thumb_cache, addr);
}

GdkPixbuf *desperateOverview_thumb_cache_lookup(const char *addr,
                                                guint32 expected_crc,
                                                guint64 generation) {
    ThumbCacheEntry *entry = thumb_cache_lookup(addr);
    if (!entry)
        return NULL;
    if (expected_crc != 0 && entry->crc != expected_crc)
        return NULL;
    entry->generation = generation;
    return entry->pixbuf ? g_object_ref(entry->pixbuf) : NULL;
}

void desperateOverview_thumb_cache_store(const char *addr,
                                         guint32 crc,
                                         GdkPixbuf *pixbuf,
                                         guint64 generation) {
    if (!g_thumb_cache || !addr || !*addr || !pixbuf)
        return;

    ThumbCacheEntry *entry = g_new0(ThumbCacheEntry, 1);
    entry->crc = crc;
    entry->generation = generation;
    entry->pixbuf = g_object_ref(pixbuf);

    char *key = g_strdup(addr);
    g_hash_table_replace(g_thumb_cache, key, entry);
}

guint32 desperateOverview_thumb_cache_crc(const char *b64) {
    if (!b64 || !*b64)
        return 0;
    return (guint32)g_str_hash(b64);
}

