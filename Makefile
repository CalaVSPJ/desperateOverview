CC ?= gcc
PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
YYJSON_DIR ?= ../yyjson-0.10.0/src

PKGS = gtk+-3.0 gtk-layer-shell-0 gdk-pixbuf-2.0 wayland-client
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS))

CPPFLAGS += $(PKG_CFLAGS) -I$(YYJSON_DIR)
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS += $(PKG_LIBS)

PROTO_XML := \
	protocols/hyprland-toplevel-export-v1.xml \
	protocols/wlr-foreign-toplevel-management-unstable-v1.xml

PROTO_BASENAMES := $(notdir $(PROTO_XML:.xml=))
PROTO_HEADERS := $(addsuffix -client-protocol.h,$(PROTO_BASENAMES))
PROTO_SOURCES := $(addsuffix -protocol.c,$(PROTO_BASENAMES))

LOCAL_SRCS := \
	desperateOverview_core.c \
	desperateOverview_core_state.c \
	desperateOverview_core_ipc.c \
	desperateOverview_core_json.c \
	desperateOverview_thumbnail_capture.c \
	desperateOverview_geometry.c \
	desperateOverview_config.c \
	desperateOverview_ui_drag.c \
	desperateOverview_ui_drawing.c \
	desperateOverview_ui_render.c \
	desperateOverview_ui_layout.c \
	desperateOverview_ui_state.c \
	desperateOverview_ui_events.c \
	desperateOverview_ui_css.c \
	desperateOverview_ui_live.c \
	desperateOverview_ui_thumb_cache.c \
	desperateOverview_ui.c \
	desperateOverview_app.c

SRCS := $(LOCAL_SRCS) $(PROTO_SOURCES)
OBJS := $(SRCS:.c=.o) yyjson.o

.PHONY: all clean install uninstall deps

all: desperateOverview

desperateOverview: deps $(PROTO_HEADERS) $(PROTO_SOURCES) $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

deps:
	@$(PKG_CONFIG) --exists $(PKGS) || \
		(echo "Missing required packages: $(PKGS)" >&2 && exit 1)

yyjson.o: $(YYJSON_DIR)/yyjson.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%-client-protocol.h: protocols/%.xml
	$(WAYLAND_SCANNER) client-header $< $@

%-protocol.c: protocols/%.xml
	$(WAYLAND_SCANNER) private-code $< $@

clean:
	$(RM) desperateOverview $(OBJS) $(PROTO_HEADERS) $(PROTO_SOURCES)

install: desperateOverview
	install -Dm755 desperateOverview "$(DESTDIR)$(BINDIR)/desperateOverview"

uninstall:
	$(RM) "$(DESTDIR)$(BINDIR)/desperateOverview"

