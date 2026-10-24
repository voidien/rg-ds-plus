# Native build (syntax check):  make
# Cross build:  make CROSS=aarch64-linux-gnu- SYSROOT=/path/to/sysroot   (needs libdrm headers/libs in the sysroot)
CROSS   ?=
CC      := $(CROSS)gcc
SYSROOT ?=
CFLAGS  ?= -O2 -Wall -Wextra
ifneq ($(SYSROOT),)
CFLAGS  += --sysroot=$(SYSROOT)
endif
DRM_CFLAGS := $(shell pkg-config --cflags libdrm 2>/dev/null || echo -I/usr/include/libdrm)
DRM_LIBS   := $(shell pkg-config --libs libdrm 2>/dev/null || echo -ldrm)

kms_dualscreen: kms_dualscreen.c
	$(CC) $(CFLAGS) $(DRM_CFLAGS) -o $@ $< $(DRM_LIBS)

clean:
	rm -f kms_dualscreen

.PHONY: clean
