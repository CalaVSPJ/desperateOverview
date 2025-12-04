#ifndef DESPERATE_WINDOW_H
#define DESPERATE_WINDOW_H

#include <stdbool.h>

// Class representing a single window
// Missing thumbnail - add later to constructor/destructor/refresher
typedef struct desperateWindow {
    int id;
    char *initialClass;
    char *initialTitle;
    char *title;
    int at_x;
    int at_y;
    int size_x;
    int size_y;
    bool floating;
} desperateWindow;

// Constructor
desperateWindow *desperate_window_create(int id,
                                         const char *initialClass,
                                         const char *initialTitle,
                                         const char *title,
                                         int at_x,
                                         int at_y,
                                         int size_x,
                                         int size_y,
                                         bool floating);

// Destructor
void desperate_window_destroy(desperateWindow *window);

// Refresher
void desperate_window_refresh(desperateWindow *window,
                              const char *title,
                              int at_x,
                              int at_y,
                              int size_x,
                              int size_y,
                              bool floating);

#endif