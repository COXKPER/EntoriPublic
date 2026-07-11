CC         = gcc
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   = $(shell pkg-config --libs gtk+-3.0)
WNK_CFLAGS = $(shell pkg-config --cflags libwnck-3.0)
WNK_LIBS   = $(shell pkg-config --libs libwnck-3.0)
JSON_CFLAGS = $(shell pkg-config --cflags json-glib-1.0)
JSON_LIBS   = $(shell pkg-config --libs json-glib-1.0)
X11_CFLAGS  = $(shell pkg-config --cflags x11)
X11_LIBS    = $(shell pkg-config --libs x11)
PULSE_CFLAGS= $(shell pkg-config --cflags libpulse libpulse-mainloop-glib)
PULSE_LIBS  = $(shell pkg-config --libs libpulse libpulse-mainloop-glib)
NM_CFLAGS   = $(shell pkg-config --cflags libnm)
NM_LIBS     = $(shell pkg-config --libs libnm)
DBUS_CFLAGS = $(shell pkg-config --cflags dbusmenu-gtk3-0.4)
DBUS_LIBS   = $(shell pkg-config --libs dbusmenu-gtk3-0.4)
BASE_FLAGS = -Wall -O2

BUILD_DIR  = build
ESSDIR     = ../config/includes.chroot/usr/essentials

TARGETS    = $(BUILD_DIR)/about $(BUILD_DIR)/options $(BUILD_DIR)/menu $(BUILD_DIR)/keyver $(BUILD_DIR)/volume $(BUILD_DIR)/networkgui

.PHONY: all clean ci

all: $(TARGETS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/about: about.c | $(BUILD_DIR)
	$(CC) $(GTK_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS)

$(BUILD_DIR)/options: options.c | $(BUILD_DIR)
	$(CC) $(GTK_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS)

$(BUILD_DIR)/menu: menu.c sni_host.c | $(BUILD_DIR)
	$(CC) $(GTK_CFLAGS) $(X11_CFLAGS) $(WNK_CFLAGS) $(DBUS_CFLAGS) $(BASE_FLAGS) -o $@ menu.c sni_host.c $(GTK_LIBS) $(WNK_LIBS) $(DBUS_LIBS) $(X11_LIBS)
	
$(BUILD_DIR)/keyver: keyver.c | $(BUILD_DIR)
	$(CC) $(GTK_CFLAGS) $(JSON_CFLAGS) $(X11_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS) $(JSON_LIBS) $(X11_LIBS)

$(BUILD_DIR)/volume: volume.c | $(BUILD_DIR)
	$(CC) $(GTK_CFLAGS) $(PULSE_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS) $(PULSE_LIBS)

$(BUILD_DIR)/networkgui: networkgui.c | $(BUILD_DIR)
	$(CC) $(GTK_CFLAGS) $(NM_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS) $(NM_LIBS)

ci: $(TARGETS)
	mkdir -p $(ESSDIR)
	install -m755 $(BUILD_DIR)/about      $(ESSDIR)/about
	install -m755 $(BUILD_DIR)/options    $(ESSDIR)/options
	install -m755 $(BUILD_DIR)/menu       $(ESSDIR)/menu
	install -m755 $(BUILD_DIR)/keyver     $(ESSDIR)/keyver
	install -m755 $(BUILD_DIR)/volume     $(ESSDIR)/volume
	install -m755 $(BUILD_DIR)/networkgui $(ESSDIR)/networkgui
	@echo "Installed essentials to $(ESSDIR)"

clean:
	rm -rf $(BUILD_DIR)

