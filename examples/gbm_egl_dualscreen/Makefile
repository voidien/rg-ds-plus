# make        build the dual-panel example (needs libdrm, gbm, egl, glesv2)
# make test   build and run the headless scene test (software Mesa is fine; no display needed)
# Cross build:  make CROSS=aarch64-linux-gnu- SYSROOT=/path/to/sysroot  (sysroot with Mesa EGL/GBM/GLES + libdrm)
CROSS   ?=
CC      := $(CROSS)gcc
SYSROOT ?=
CFLAGS  ?= -O2 -Wall -Wextra
ifneq ($(SYSROOT),)
CFLAGS  += --sysroot=$(SYSROOT)
endif
PKGS    := libdrm gbm egl glesv2
PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS) 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs $(PKGS) 2>/dev/null || echo -ldrm -lgbm -lEGL -lGLESv2)

gbm_egl_dualscreen: gbm_egl_dualscreen.c scene.c scene.h
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ gbm_egl_dualscreen.c scene.c $(PKG_LIBS)

test_scene: test_scene.c scene.c scene.h
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ test_scene.c scene.c $(PKG_LIBS)

test: test_scene
	LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless ./test_scene

clean:
	rm -f gbm_egl_dualscreen test_scene

.PHONY: test clean
