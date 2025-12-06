#!/bin/bash

# Integrated app (core + UI)
gcc -Wall -Wextra -O2 \
    $(pkg-config --cflags gtk+-3.0 gtk-layer-shell-0 gdk-pixbuf-2.0) \
    -o overviewApp \
    overview_core.c \
    thumbnail_capture.c \
    overview_geometry.c \
    overview_ui.c \
    overview_app.c \
    hyprland-toplevel-export-v1-protocol.c \
    wlr-foreign-toplevel-management-unstable-v1-protocol.c \
    $(pkg-config --libs gtk+-3.0 gtk-layer-shell-0 gdk-pixbuf-2.0 wayland-client)

