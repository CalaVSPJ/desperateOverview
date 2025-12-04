#define _POSIX_C_SOURCE 200809L

#include "desperate_parser.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yyjson.h"

// Small helper for holding the raw `hyprctl clients -j` output.
typedef struct desperateParserBuffer 
{
    char *data;
    size_t length;
} desperateParserBuffer;

static desperateParserBuffer desperate_parser_capture_clients(void);
static void desperate_parser_buffer_destroy(desperateParserBuffer *buffer);

// Core collection routine shared between the three public entry points.
static desperateParserResult desperate_parser_collect
(
    yyjson_doc *doc,
    const char *address,
    bool filter_address,
    int workspace_id,
    bool filter_workspace
);

// Convert one JSON object into a `desperateWindow` if it passes the filters.
static desperateWindow *desperate_parser_build_window
(
    yyjson_val *object,
    const char *address,
    bool filter_address,
    int workspace_id,
    bool filter_workspace
);

// Extract nested `workspace.id` field.
static bool desperate_parser_extract_workspace_id
(
    yyjson_val *object,
    int *workspace_id
);

// Parse Hyprland's `[x, y]` arrays into integer coordinates.
static bool desperate_parser_extract_vec2
(
    yyjson_val *array,
    int *out_x,
    int *out_y
);

// Parse a single window matching the given address (first match wins).
desperateWindow *desperate_parser_parse_address(const char *address)
{
    desperateParserBuffer buffer = desperate_parser_capture_clients();

    if (buffer.data == NULL)
        return NULL;

    yyjson_read_err err = {0};
    yyjson_doc *doc = yyjson_read_opts(buffer.data, buffer.length, YYJSON_READ_NOFLAG, NULL, &err);

    if (doc == NULL)
    {
        desperate_parser_buffer_destroy(&buffer);
        return NULL;
    }

    desperateParserResult result = desperate_parser_collect(doc, address, true, 0, false);

    yyjson_doc_free(doc);
    desperate_parser_buffer_destroy(&buffer);
    desperateWindow *window = NULL;

    if (result.count > 0)
        window = result.windows[0];

    free(result.windows);

    return window;
}

// Parse all mapped clients on `workspace_id`.
desperateParserResult desperate_parser_parse_workspace(int workspace_id)
{
    desperateParserBuffer buffer = desperate_parser_capture_clients();

    if (buffer.data == NULL)
    {
        desperateParserResult empty = {0};
        return empty;
    }

    yyjson_read_err err = {0};
    yyjson_doc *doc = yyjson_read_opts(buffer.data, buffer.length, YYJSON_READ_NOFLAG, NULL, &err);

    if (doc == NULL)
    {
        desperate_parser_buffer_destroy(&buffer);
        desperateParserResult empty = {0};
        return empty;
    }

    desperateParserResult result = desperate_parser_collect(doc, NULL, false, workspace_id, true);

    yyjson_doc_free(doc);
    desperate_parser_buffer_destroy(&buffer);
    return result;
}

// Parse every mapped client regardless of workspace.
desperateParserResult desperate_parser_parse_all(void)
{
    desperateParserBuffer buffer = desperate_parser_capture_clients();

    if (buffer.data == NULL)
    {
        desperateParserResult empty = {0};
        return empty;
    }

    yyjson_read_err err = {0};
    yyjson_doc *doc = yyjson_read_opts(buffer.data, buffer.length, YYJSON_READ_NOFLAG, NULL, &err);

    if (doc == NULL)
    {
        desperate_parser_buffer_destroy(&buffer);
        desperateParserResult empty = {0};
        return empty;
    }

    desperateParserResult result = desperate_parser_collect(doc, NULL, false, 0, false);

    yyjson_doc_free(doc);
    desperate_parser_buffer_destroy(&buffer);

    return result;
}

// Helper to release parser-generated window arrays.
void desperate_parser_result_destroy(desperateParserResult *result)
{
    if (result == NULL || result->windows == NULL)
        return;

    for (size_t i = 0; i < result->count; ++i)
        desperate_window_destroy(result->windows[i]);

    free(result->windows);
    result->windows = NULL;
    result->count = 0;
}

// Run `hyprctl clients -j` and capture stdout into memory.
static desperateParserBuffer desperate_parser_capture_clients(void)
{
    desperateParserBuffer buffer = {0};
    FILE *pipe = popen("hyprctl clients -j", "r");

    if (pipe == NULL)
        return buffer;

    size_t capacity = 4096;
    buffer.data = malloc(capacity);

    if (buffer.data == NULL)
    {
        pclose(pipe);
        return buffer;
    }

    size_t length = 0;
    char chunk[1024];

    while (!feof(pipe))
    {
        size_t read_bytes = fread(chunk, 1, sizeof(chunk), pipe);

        if (read_bytes == 0)
            break;

        if (length + read_bytes + 1 >= capacity)
        {
            while (length + read_bytes + 1 >= capacity)
                capacity *= 2;

            char *resized = realloc(buffer.data, capacity);

            if (resized == NULL)
            {
                free(buffer.data);
                buffer.data = NULL;
                pclose(pipe);
                return (desperateParserBuffer){0};
            }

            buffer.data = resized;
        }

        memcpy(buffer.data + length, chunk, read_bytes);
        length += read_bytes;
    }

    pclose(pipe);

    if (buffer.data != NULL)
    {
        buffer.data[length] = '\0';
        buffer.length = length;
    }

    return buffer;
}

static void desperate_parser_buffer_destroy(desperateParserBuffer *buffer)
{
    if (buffer == NULL)
        return;

    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
}

// Walk the JSON document and build windows while respecting filters.
static desperateParserResult desperate_parser_collect
(
    yyjson_doc *doc,
    const char *address,
    bool filter_address,
    int workspace_id,
    bool filter_workspace
)
{
    desperateParserResult result = {0};

    if (doc == NULL)
        return result;

    yyjson_val *root = yyjson_doc_get_root(doc);

    if (root == NULL || !yyjson_is_arr(root))
        return result;

    size_t capacity = 0;
    size_t idx, max;
    yyjson_val *entry;

    yyjson_arr_foreach(root, idx, max, entry)
    {
        desperateWindow *window = desperate_parser_build_window(entry, address, filter_address, workspace_id, filter_workspace);

        if (window == NULL)
            continue;

        if (filter_address)
        {
            result.windows = malloc(sizeof(*result.windows));

            if (result.windows == NULL)
            {
                desperate_window_destroy(window);
                break;
            }

            result.windows[0] = window;
            result.count = 1;
            break;
        }

        if (result.count == capacity)
        {
            capacity = capacity == 0 ? 4 : capacity * 2;
            desperateWindow **resized = realloc(result.windows, capacity * sizeof(*resized));

            if (resized == NULL)
            {
                desperate_window_destroy(window);
                break;
            }

            result.windows = resized;
        }

        result.windows[result.count++] = window;
    }

    return result;
}

// Convert a single JSON object into a window; returns NULL if filtered out.
static desperateWindow *desperate_parser_build_window(
    yyjson_val *object,
    const char *address,
    bool filter_address,
    int workspace_id,
    bool filter_workspace
)
{
    if (object == NULL || !yyjson_is_obj(object))
        return NULL;

    yyjson_val *mapped = yyjson_obj_get(object, "mapped");

    if (mapped == NULL || !yyjson_is_bool(mapped) || !yyjson_is_true(mapped))
        return NULL;

    if (filter_address)
    {
        yyjson_val *address_value = yyjson_obj_get(object, "address");

        if (address_value == NULL || !yyjson_is_str(address_value))
            return NULL;

        if (strcmp(yyjson_get_str(address_value), address) != 0)
            return NULL;
    }

    if (filter_workspace)
    {
        int parsed_workspace_id = 0;

        if (!desperate_parser_extract_workspace_id(object, &parsed_workspace_id) || parsed_workspace_id != workspace_id)
            return NULL;

    }

    yyjson_val *title_val = yyjson_obj_get(object, "title");

    if (title_val == NULL || !yyjson_is_str(title_val)) {
        return NULL;
    }

    const char *title = yyjson_get_str(title_val);
    const char *initial_class = "";
    yyjson_val *initial_class_val = yyjson_obj_get(object, "initialClass");

    if (initial_class_val != NULL && yyjson_is_str(initial_class_val))
        initial_class = yyjson_get_str(initial_class_val);

    const char *initial_title = title;
    yyjson_val *initial_title_val = yyjson_obj_get(object, "initialTitle");

    if (initial_title_val != NULL && yyjson_is_str(initial_title_val))
        initial_title = yyjson_get_str(initial_title_val);

    yyjson_val *floating_val = yyjson_obj_get(object, "floating");

    bool floating = (floating_val != NULL && yyjson_is_bool(floating_val) && yyjson_is_true(floating_val));
    int id = 0;
    yyjson_val *id_val = yyjson_obj_get(object, "focusHistoryID");

    if (id_val != NULL && yyjson_is_num(id_val))
        id = yyjson_get_int(id_val);

    int at_x = 0;
    int at_y = 0;

    if (!desperate_parser_extract_vec2(yyjson_obj_get(object, "at"), &at_x, &at_y))
        return NULL;

    int size_x = 0;
    int size_y = 0;

    if (!desperate_parser_extract_vec2(yyjson_obj_get(object, "size"), &size_x, &size_y))
        return NULL;

    return desperate_window_create(id, initial_class, initial_title, title, at_x, at_y, size_x, size_y, floating);
}

// Read the nested `workspace.id` field; returns false if missing.
static bool desperate_parser_extract_workspace_id
(
    yyjson_val *object,
    int *workspace_id
)
{
    if (object == NULL || workspace_id == NULL)
        return false;

    yyjson_val *workspace = yyjson_obj_get(object, "workspace");

    if (workspace == NULL || !yyjson_is_obj(workspace))
        return false;

    yyjson_val *id_val = yyjson_obj_get(workspace, "id");

    if (id_val == NULL || !yyjson_is_num(id_val))
        return false;

    *workspace_id = yyjson_get_int(id_val);

    return true;
}

// Extract the x and y coordinates from a 2D array.
static bool desperate_parser_extract_vec2
(
    yyjson_val *array,
    int *out_x,
    int *out_y
)
{
    if (array == NULL || out_x == NULL || out_y == NULL || !yyjson_is_arr(array))
        return false;

    yyjson_val *x_val = yyjson_arr_get(array, 0);
    yyjson_val *y_val = yyjson_arr_get(array, 1);

    if (x_val == NULL || y_val == NULL || !yyjson_is_num(x_val) || !yyjson_is_num(y_val))
        return false;

    *out_x = yyjson_get_int(x_val);
    *out_y = yyjson_get_int(y_val);

    return true;
}