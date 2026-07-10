CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -std=c11 -D_GNU_SOURCE
CFLAGS  += $(shell pkg-config --cflags gtk+-3.0 dbus-1 x11)
LDFLAGS  = $(shell pkg-config --libs gtk+-3.0 dbus-1 x11) -lm

SRC_DIR  = src
BUILD_DIR = build
TARGET   = $(BUILD_DIR)/mola-desktop

SRCS     = $(SRC_DIR)/main.c \
           $(SRC_DIR)/dock.c \
           $(SRC_DIR)/dbus_server.c \
           $(SRC_DIR)/config.c \
           $(SRC_DIR)/window_monitor.c

OBJS     = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
HEADERS  = $(SRC_DIR)/dock.h \
           $(SRC_DIR)/dbus_server.h \
           $(SRC_DIR)/config.h \
           $(SRC_DIR)/window_monitor.h

.PHONY: all clean install test

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

clean:
	rm -rf $(BUILD_DIR)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/bin/mola-desktop

MOLA_ESSDIR = ../config/includes.chroot/usr/essentials/mola

ci: $(TARGET)
	mkdir -p $(MOLA_ESSDIR)
	install -m755 $(TARGET) $(MOLA_ESSDIR)/mola-desktop
	@if [ -d icons ] && [ "$$(ls -A icons 2>/dev/null)" ]; then \
		cp -r icons $(MOLA_ESSDIR)/; \
	fi
	@echo "Installed to $(MOLA_ESSDIR)"
