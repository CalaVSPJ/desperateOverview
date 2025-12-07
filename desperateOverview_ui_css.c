#define _GNU_SOURCE

#include "desperateOverview_ui_css.h"

#include <gtk/gtk.h>
#include <glib.h>

static GtkCssProvider *g_css_provider = NULL;
static char *g_css_override_path = NULL;

void desperateOverview_css_set_override(const char *path) {
    g_free(g_css_override_path);
    g_css_override_path = path ? g_strdup(path) : NULL;
}

void desperateOverview_css_init(void) {
    if (g_css_provider)
        return;

    GtkCssProvider *provider = gtk_css_provider_new();
    gboolean loaded = FALSE;

    if (g_css_override_path && g_css_override_path[0]) {
        if (gtk_css_provider_load_from_path(provider, g_css_override_path, NULL)) {
            g_message("desperateOverview: loaded CSS from override %s", g_css_override_path);
            loaded = TRUE;
        } else {
            g_warning("desperateOverview: failed to load CSS override %s", g_css_override_path);
        }
    }

    gchar *user_css = NULL;
    const char *config_dir = g_get_user_config_dir();
    if (config_dir)
        user_css = g_build_filename(config_dir, "desperateOverview", "style.css", NULL);

    const char *search_paths[] = {
        user_css,
        "desperateOverview.css",
        "data/desperateOverview.css",
        NULL
    };

    for (int i = 0; !loaded && search_paths[i]; ++i) {
        const char *path = search_paths[i];
        if (path && g_file_test(path, G_FILE_TEST_EXISTS)) {
            if (gtk_css_provider_load_from_path(provider, path, NULL)) {
                g_message("desperateOverview: loaded CSS from %s", path);
                loaded = TRUE;
                break;
            }
        }
    }

    g_free(user_css);

    if (!loaded) {
        static const gchar *fallback_css =
            ".desperateOverview-status {\n"
            "  color: #cad4ff;\n"
            "  font-weight: 600;\n"
            "}\n"
            ".desperateOverview-ghost {\n"
            "  transition: opacity 120ms ease;\n"
            "}\n";
        gtk_css_provider_load_from_data(provider, fallback_css, -1, NULL);
        loaded = TRUE;
    }

    if (!loaded) {
        g_object_unref(provider);
        return;
    }

    GdkScreen *screen = gdk_screen_get_default();
    if (!screen) {
        g_object_unref(provider);
        return;
    }

    gtk_style_context_add_provider_for_screen(
        screen,
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_css_provider = provider;
}

void desperateOverview_css_shutdown(void) {
    if (!g_css_provider)
        return;
    GdkScreen *screen = gdk_screen_get_default();
    if (screen)
        gtk_style_context_remove_provider_for_screen(
            screen,
            GTK_STYLE_PROVIDER(g_css_provider));
    g_object_unref(g_css_provider);
    g_css_provider = NULL;
    g_clear_pointer(&g_css_override_path, g_free);
}

