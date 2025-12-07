#ifndef DESPERATEOVERVIEW_CORE_JSON_H
#define DESPERATEOVERVIEW_CORE_JSON_H

#include <stdbool.h>
#include "yyjson.h"

int desperateOverview_json_get_int(yyjson_val *val, int def);
bool desperateOverview_json_is_true(yyjson_val *val);
bool desperateOverview_json_get_vec2(yyjson_val *arr, int *out_x, int *out_y);
char *desperateOverview_json_dup_str(yyjson_val *val);
yyjson_doc *desperateOverview_read_json_from_cmd(const char *cmd);

#endif /* DESPERATEOVERVIEW_CORE_JSON_H */

