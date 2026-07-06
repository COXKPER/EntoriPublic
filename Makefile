CC      = gcc
CFLAGS  = $(shell pkg-config --cflags gtk+-3.0 libwnck-3.0) -Wall -O2
LDLIBS  = $(shell pkg-config --libs gtk+-3.0 libwnck-3.0)

ESSDIR  = config/includes.chroot/usr/essentials
ESS_SRC = $(ESSDIR)/about.c $(ESSDIR)/options.c $(ESSDIR)/menu.c
ESS_BIN = $(ESSDIR)/about $(ESSDIR)/options $(ESSDIR)/menu

all: $(ESS_BIN)

$(ESSDIR)/about: $(ESSDIR)/about.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

$(ESSDIR)/options: $(ESSDIR)/options.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

$(ESSDIR)/menu: $(ESSDIR)/menu.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(ESS_BIN)

.PHONY: all clean
