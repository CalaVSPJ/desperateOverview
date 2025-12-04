#include "desperate_window.h"

#include <stdlib.h>
#include <string.h>

desperateWindow *desperate_window_create
(
    int id,
    const char *initialClass,
    const char *initialTitle,
    const char *title,
    int at_x,
    int at_y,
    int size_x,
    int size_y,
    bool floating
)
{
    desperateWindow *window = malloc(sizeof(*window));

    window->id = id;
    window->at_x = at_x;
    window->at_y = at_y;
    window->size_x = size_x;
    window->size_y = size_y;
    window->floating = floating;

    const char *class_source = initialClass != NULL ? initialClass : "";
    size_t class_len = strlen(class_source) + 1;
    window->initialClass = memcpy(malloc(class_len), class_source, class_len);

    const char *initial_title_source =
        initialTitle != NULL ? initialTitle : "";
    size_t initial_title_len = strlen(initial_title_source) + 1;
    window->initialTitle = memcpy(malloc(initial_title_len),
                                  initial_title_source, initial_title_len);

    const char *title_source = title != NULL ? title : "";
    size_t title_len = strlen(title_source) + 1;
    window->title = memcpy(malloc(title_len), title_source, title_len);

    return window;
}

void desperate_window_destroy(desperateWindow *window)
{
    if (window == NULL) {
        return;
    }

    free(window->initialClass);
    free(window->initialTitle);
    free(window->title);
    free(window);
}

void desperate_window_refresh
(
    desperateWindow *window,
    const char *title,
    int at_x,
    int at_y,
    int size_x,
    int size_y,
    bool floating
)
{
    free(window->title);
    const char *title_source = title != NULL ? title : "";
    size_t title_len = strlen(title_source) + 1;
    window->title = memcpy(malloc(title_len), title_source, title_len);
    window->at_x = at_x;
    window->at_y = at_y;
    window->size_x = size_x;
    window->size_y = size_y;
    window->floating = floating;
}