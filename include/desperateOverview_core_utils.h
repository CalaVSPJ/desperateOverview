#ifndef DESPERATEOVERVIEW_CORE_UTILS_H
#define DESPERATEOVERVIEW_CORE_UTILS_H

#include <string.h>

static inline void desperateOverview_core_sanitize_addr(char *s) {
    if (!s)
        return;
    char out[64];
    int j = 0;
    for (int i = 0; s[i] && j < 63; ++i) {
        char c = s[i];
        if ((c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F') ||
            c == 'x' || c == 'X') {
            out[j++] = c;
        } else {
            break;
        }
    }
    out[j] = '\0';
    strcpy(s, out);
}

#endif /* DESPERATEOVERVIEW_CORE_UTILS_H */

