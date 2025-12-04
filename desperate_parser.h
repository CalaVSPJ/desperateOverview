#ifndef DESPERATE_PARSER_H
#define DESPERATE_PARSER_H

#include <stddef.h>

#include "desperate_window.h"

// Aggregated parser output: array of window pointers plus count.
typedef struct desperateParserResult {
    desperateWindow **windows;
    size_t count;
} desperateParserResult;

// Parse `hyprctl clients -j` for a single address (returns first match).
desperateWindow *desperate_parser_parse_address(const char *address);

// Parse all mapped clients on a specific workspace ID.
desperateParserResult desperate_parser_parse_workspace(int workspace_id);

// Parse all mapped clients regardless of workspace.
desperateParserResult desperate_parser_parse_all(void);

// Free the windows + container produced by workspace/all helpers.
void desperate_parser_result_destroy(desperateParserResult *result);

#endif
