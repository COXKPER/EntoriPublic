# Root Makefile — delegates to component Makefiles

.PHONY: all clean ci essentials-all essentials-ci essentials-clean mola-ci mola-clean

# --- Default: build Essentials locally ---
all: essentials-all

essentials-all:
	$(MAKE) -C Essentials all

essentials-ci:
	$(MAKE) -C Essentials ci

essentials-clean:
	$(MAKE) -C Essentials clean

# --- MoLa integration ---
mola-ci:
	$(MAKE) -C MoLa ci

mola-clean:
	$(MAKE) -C MoLa clean

# --- CI target: build and install everything into the chroot tree ---
ci: essentials-ci mola-ci

# --- Clean all ---
clean: essentials-clean mola-clean
