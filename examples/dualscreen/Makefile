# Native build (for syntax checking on a dev machine):  make
# Cross build for the handheld (aarch64, glibc):
#   make CROSS=aarch64-linux-gnu- SYSROOT=/path/to/rg-ds-plus-sysroot
# The sysroot needs libSDL2 headers/libs matching the stock firmware's SDL2
# (see docs/FINDINGS.md); simplest is to take them from the stock rootfs.
CROSS   ?=
CC      := $(CROSS)gcc
SYSROOT ?=
CFLAGS  ?= -O2 -Wall -Wextra
ifneq ($(SYSROOT),)
CFLAGS  += --sysroot=$(SYSROOT)
endif
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null || echo -lSDL2)

dualscreen: dualscreen.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS)

clean:
	rm -f dualscreen

.PHONY: clean
