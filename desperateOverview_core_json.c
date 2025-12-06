#define _GNU_SOURCE

#include "desperateOverview_core_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

static char *run_cmd(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return NULL;

    size_t cap = 8192;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        pclose(fp);
        return NULL;
    }

    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, fp)) > 0) {
        len += n;
        if (cap - len - 1 == 0) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) {
                free(buf);
                pclose(fp);
                return NULL;
            }
            buf = tmp;
        }
    }
    buf[len] = 0;
    pclose(fp);
    return buf;
}

int desperateOverview_json_get_int(yyjson_val *val, int def) {
    if (!val)
        return def;
    if (yyjson_is_int(val))
        return (int)yyjson_get_int(val);
    if (yyjson_is_num(val))
        return (int)yyjson_get_sint(val);
    return def;
}

bool desperateOverview_json_is_true(yyjson_val *val) {
    return val && yyjson_is_bool(val) && yyjson_is_true(val);
}

bool desperateOverview_json_get_vec2(yyjson_val *arr, int *out_x, int *out_y) {
    if (!arr || !out_x || !out_y || !yyjson_is_arr(arr))
        return false;
    yyjson_val *x_val = yyjson_arr_get(arr, 0);
    yyjson_val *y_val = yyjson_arr_get(arr, 1);
    if (!x_val || !y_val || !yyjson_is_num(x_val) || !yyjson_is_num(y_val))
        return false;
    *out_x = (int)yyjson_get_int(x_val);
    *out_y = (int)yyjson_get_int(y_val);
    return true;
}

char *desperateOverview_json_dup_str(yyjson_val *val) {
    if (!val || !yyjson_is_str(val))
        return NULL;
    const char *s = yyjson_get_str(val);
    if (!s || !*s)
        return NULL;
    return strdup(s);
}

yyjson_doc *desperateOverview_read_json_from_cmd(const char *cmd) {
    char *json = run_cmd(cmd);
    if (!json)
        return NULL;

    yyjson_read_err err = (yyjson_read_err){0};
    yyjson_doc *doc = yyjson_read_opts(json, strlen(json), YYJSON_READ_NOFLAG, NULL, &err);
    free(json);
    if (!doc)
        g_warning("Hyprland JSON parse failed for %s: %s (pos=%zu)", cmd, err.msg, err.pos);
    return doc;
}

