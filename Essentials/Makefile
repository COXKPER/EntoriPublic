CC         = gcc
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   = $(shell pkg-config --libs gtk+-3.0)
WNK_CFLAGS = $(shell pkg-config --cflags libwnck-3.0)
WNK_LIBS   = $(shell pkg-config --libs libwnck-3.0)
JSON_CFLAGS = $(shell pkg-config --cflags json-glib-1.0)
JSON_LIBS   = $(shell pkg-config --libs json-glib-1.0)
X11_CFLAGS  = $(shell pkg-config --cflags x11)
X11_LIBS    = $(shell pkg-config --libs x11)
BASE_FLAGS = -Wall -O2

BUILD_DIR  = build
ESSDIR     = ../config/includes.chroot/usr/essentials

TARGETS    = $(BUILD_DIR)/about $(BUILD_DIR)/options $(BUILD_DIR)/menu $(BUILD_DIR)/keyver

.PHONY: all clean ci

all: $(TARGETS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/about: about.c | $(BUILD_DIR)
	$(CC) $(GTK_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS)

$(BUILD_DIR)/options: options.c | $(BUILD_DIR)
	$(CC) $(GTK_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS)

$(BUILD_DIR)/menu: menu.c | $(BUILD_DIR)
	$(CC) $(GTK_CFLAGS) $(WNK_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS) $(WNK_LIBS)

$(BUILD_DIR)/keyver: keyver.c | $(BUILD_DIR)
	$(CC) $(GTK_CFLAGS) $(JSON_CFLAGS) $(X11_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS) $(JSON_LIBS) $(X11_LIBS)

ci: $(TARGETS)
	mkdir -p $(ESSDIR)
	install -m755 $(BUILD_DIR)/about   $(ESSDIR)/about
	install -m755 $(BUILD_DIR)/options $(ESSDIR)/options
	install -m755 $(BUILD_DIR)/menu    $(ESSDIR)/menu
	install -m755 $(BUILD_DIR)/keyver  $(ESSDIR)/keyver
	@echo "Installed essentials to $(ESSDIR)"

clean:
	rm -rf $(BUILD_DIR)
