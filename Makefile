CC ?= gcc
PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
APPDATADIR ?= $(DATADIR)/desperateOverview

VERSION ?= 0.1.0

SRC_DIR := src
INC_DIR := include
PROTO_DIR := protocols
PROTO_GEN_DIR := $(PROTO_DIR)/generated
VENDOR_DIR := vendor
YYJSON_VERSION ?= 0.10.0
YYJSON_DIR ?= $(VENDOR_DIR)/yyjson-$(YYJSON_VERSION)
YYJSON_SRC := $(YYJSON_DIR)/src/yyjson.c
YYJSON_VENDOR_FETCH := 0
ifneq (,$(filter $(VENDOR_DIR)/%,$(YYJSON_DIR)))
YYJSON_VENDOR_FETCH := 1
YYJSON_ARCHIVE := $(VENDOR_DIR)/yyjson-$(YYJSON_VERSION).tar.gz
YYJSON_STAMP := $(YYJSON_DIR)/.fetched
YYJSON_URL := https://github.com/ibireme/yyjson/archive/refs/tags/$(YYJSON_VERSION).tar.gz
endif

PKGS = gtk+-3.0 gtk-layer-shell-0 gdk-pixbuf-2.0 wayland-client
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS))

CPPFLAGS += $(PKG_CFLAGS) -I$(INC_DIR) -I$(PROTO_GEN_DIR) -I$(YYJSON_DIR)/src \
            -DDESPERATEOVERVIEW_VERSION=\"$(VERSION)\"
# Generate header dependency files alongside each .o so make knows to
# rebuild a .c when any header it transitively includes changes.
DEPFLAGS = -MMD -MP
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS += $(PKG_LIBS)

PROTO_XML := \
	$(PROTO_DIR)/hyprland-toplevel-export-v1.xml \
	$(PROTO_DIR)/wlr-foreign-toplevel-management-unstable-v1.xml

PROTO_HEADERS := \
	$(PROTO_GEN_DIR)/hyprland-toplevel-export-v1-client-protocol.h \
	$(PROTO_GEN_DIR)/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h

PROTO_SOURCES := \
	$(PROTO_GEN_DIR)/hyprland-toplevel-export-v1-protocol.c \
	$(PROTO_GEN_DIR)/wlr-foreign-toplevel-management-unstable-v1-protocol.c

SRCS := $(wildcard $(SRC_DIR)/*.c) $(PROTO_SOURCES) $(YYJSON_SRC)
OBJS := $(SRCS:.c=.o)
# Header dependency files: one .d per .o, written by -MMD.
DEPS := $(OBJS:.o=.d)

TARGET := desperateOverview

.PHONY: all clean install uninstall deps

all: $(TARGET)

$(TARGET): deps $(PROTO_HEADERS) $(PROTO_SOURCES) $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

deps:
	@$(PKG_CONFIG) --exists $(PKGS) || \
		(echo "Missing required packages: $(PKGS)" >&2 && exit 1)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c | $(YYJSON_SRC)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

$(PROTO_GEN_DIR)/%.o: $(PROTO_GEN_DIR)/%.c | $(YYJSON_SRC)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

YYJSON_CLEAN_CMD :=
ifeq ($(YYJSON_VENDOR_FETCH),1)
$(YYJSON_SRC): | $(YYJSON_STAMP)

$(YYJSON_ARCHIVE):
	@mkdir -p $(VENDOR_DIR)
	curl -L "$(YYJSON_URL)" -o "$@"

$(YYJSON_STAMP): $(YYJSON_ARCHIVE)
	tar -xzf $< -C $(VENDOR_DIR)
	touch $@

YYJSON_CLEAN_CMD = \
	$(RM) $(YYJSON_ARCHIVE); \
	if [ -d "$(YYJSON_DIR)" ]; then rm -rf "$(YYJSON_DIR)"; fi
else
$(YYJSON_SRC):
	@true
endif

$(YYJSON_DIR)/src/yyjson.o: $(YYJSON_SRC)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

$(PROTO_GEN_DIR)/%-client-protocol.h: $(PROTO_DIR)/%.xml
	@mkdir -p $(PROTO_GEN_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(PROTO_GEN_DIR)/%-protocol.c: $(PROTO_DIR)/%.xml
	@mkdir -p $(PROTO_GEN_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

clean:
	$(RM) $(TARGET) $(OBJS) $(DEPS)
ifneq ($(YYJSON_CLEAN_CMD),)
	@$(YYJSON_CLEAN_CMD)
endif

# Pull in auto-generated header dependencies. The leading dash silences
# "no such file" warnings on the first build.
-include $(DEPS)

install: $(TARGET)
	install -Dm755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	install -Dm644 docs/config.example.ini "$(DESTDIR)$(APPDATADIR)/config.example.ini"

uninstall:
	$(RM) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	$(RM) "$(DESTDIR)$(APPDATADIR)/config.example.ini"
	-rmdir "$(DESTDIR)$(APPDATADIR)" 2>/dev/null || true

