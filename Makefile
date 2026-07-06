CC        = gcc
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS  = $(shell pkg-config --libs gtk+-3.0)
WNK_CFLAGS = $(shell pkg-config --cflags libwnck-3.0)
WNK_LIBS  = $(shell pkg-config --libs libwnck-3.0)
BASE_FLAGS = -Wall -O2

ESSDIR    = config/includes.chroot/usr/essentials
ESS_BIN   = $(ESSDIR)/about $(ESSDIR)/options $(ESSDIR)/menu

all: $(ESS_BIN)

$(ESSDIR)/about: $(ESSDIR)/about.c
	$(CC) $(GTK_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS)

$(ESSDIR)/options: $(ESSDIR)/options.c
	$(CC) $(GTK_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS)

$(ESSDIR)/menu: $(ESSDIR)/menu.c
	$(CC) $(GTK_CFLAGS) $(WNK_CFLAGS) $(BASE_FLAGS) -o $@ $< $(GTK_LIBS) $(WNK_LIBS)

clean:
	rm -f $(ESS_BIN)

.PHONY: all clean
