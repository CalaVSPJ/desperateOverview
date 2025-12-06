#!/bin/bash

# Integrated app (core + UI)
gcc -Wall -Wextra -O2 \
    $(pkg-config --cflags gtk+-3.0 gtk-layer-shell-0 gdk-pixbuf-2.0) \
    -I../yyjson-0.10.0/src \
    -o desperateOverview \
    desperateOverview_core.c \
    desperateOverview_core_json.c \
    desperateOverview_thumbnail_capture.c \
    desperateOverview_geometry.c \
    desperateOverview_config.c \
    desperateOverview_ui_drag.c \
    desperateOverview_ui_drawing.c \
    desperateOverview_ui_live.c \
    ../yyjson-0.10.0/src/yyjson.c \
    desperateOverview_ui.c \
    desperateOverview_app.c \
    hyprland-toplevel-export-v1-protocol.c \
    wlr-foreign-toplevel-management-unstable-v1-protocol.c \
    $(pkg-config --libs gtk+-3.0 gtk-layer-shell-0 gdk-pixbuf-2.0 wayland-client)

