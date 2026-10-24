# How BananaLinux drives both panels

Read from BananaLinux commit `6d48eb5`: `ui/src/kms.cpp`, `kms.h`, `leaseshim.c`, `main.cpp`,
`system/bananad/bananad.c` and the launcher scripts in `system/rootfs/usr/libexec/banana/`.
Read only, not run on hardware. Tags: **[V]** stated in the code or comments, **[I]** my reading.

## Which panel is which [V]

| Connector | DSI controller | Position | Backlight node |
|---|---|---|---|
| `DSI-2` | `fe070000` (dsi1 in the device tree) | **top** | `backlight1` (PWM2) |
| `DSI-1` | `fe060000` (dsi0) | **bottom** | `backlight0` (PWM1) |

The bottom panel carries the touchscreen: the I2C controller `fe5e0000.i2c` (I2C5, the Goodix
GT911). `backlight-path` resolves each panel's sysfs backlight by following the `backlight`
phandle in `/proc/device-tree/dsi@<addr>/panel@0`. This resolves the open question in
FINDINGS.md about which display index is the top screen (stock Weston's `DSI-1`/`DSI-2`
naming is the same kernel naming).

## The display layer (`kms.cpp`) [V]

1. Scan `/dev/dri/card*` for a device with two **connected connectors of the same type**
   (`DSI`); the one named in `top` (default `DSI-2`) is screen 0, the other screen 1. Fall back
   to the second connector if neither matches.
2. For each connector pick a **CRTC** from its encoder's `possible_crtcs` that the other panel
   isn't using. On RK3568 this is one VOP2 video port per panel (ROCKNIX's tree wires `vp0` to
   `dsi0` and `vp1` to `dsi1`).
3. Mode: the connector's preferred mode, overridden by a `video=` command-line mode
   (`DRM_MODE_TYPE_USERDEF`). Both panels must end up the same size (1024x768).
4. Two **dumb buffers** per panel, `XR24` (XRGB8888), mapped with `mmap`. `drmModeSetCrtc` shows the
   first one; afterwards `drmModePageFlip` with `DRM_MODE_PAGE_FLIP_EVENT` swaps buffers on vblank.
5. A flip event handler decrements a pending counter; the main loop does not draw into a
   buffer until its flip completes.
6. If a flip fails (lost master, a mode set in the way) it falls back to `drmModeSetCrtc`.
7. After resume or losing DRM master: `drmSetMaster` and `SetCrtc` again (`restore()`).

**No GPU and no compositor in the menu.** Each screen is composed in cached memory, and only the
changed rectangle ("damage") is copied into the hidden scanout buffer, because the scanout
buffer is uncached and every byte written costs. The hidden buffer also gets the previous frame's
damage so both buffers stay in step [V, from comments].

## Handing the panels to games [V]

- **DS emulators (SuperDrastic, BananaDS):** `banana-ui` **closes its display entirely** while the
  game runs; the emulator opens KMS itself (`DSFLIP_TOP=DSI-2`, `BANANADS_TOP=DSI-2`) and puts one
  DS screen on each panel. Touch comes from the `fe5e0000.i2c` input device.
- **RetroArch (one panel):** `Display::lease()` creates a **DRM lease**
  (`drmModeCreateLease`) holding one panel's connector, CRTC and primary plane. The game gets the
  lease fd through `BANANA_DRM_LEASE_FD`, and `leaseshim.c`, preloaded into RetroArch, makes
  `open("/dev/dri/card0")` return a dup of that fd. The menu keeps drawing on the other panel
  and ignores the leased one in `present()` and `restore()`. The unused panel's backlight is
  switched off through `bl_power`.
- **PortMaster ports:** SDL's KMSDRM driver (`SDL_KMSDRM_FIRST=DSI-2`).
- **Other SDL programs:** custom patched SDL2 (`buildroot/patches/sdl2`) adds
  `SDL_BANANA_PANELS=side` and `SDL_BANANA_TOP`, i.e. one window spanning both panels **[I]**.

## Power and brightness (`bananad`) [V]

- Sleep (power key, lid, idle) is suspend-to-RAM: the running game is `SIGSTOP`ped, Wi-Fi is
  unloaded, the card synced (the kernel skips its own sync), an RTC alarm set for a battery check,
  then `echo mem > /sys/power/state` (fallbacks: device-by-device, then `s2idle`).
- Wakeups: lid hall switch (GPIO0 PC3), power key, RTC alarm.
- Backlights: `/sys/class/backlight/*/brightness`, `bl_power`. Keys: F (`BTN_MODE`) + volume sets
  both panels. The bottom panel's idle dim belongs to `banana-ui` while it runs.
- A charging-with-lid-shut case stays awake and dark, since suspend would stop the PWM charging
  LED.

## What this means for a custom OS

- The proven recipe is KMS dumb buffers + page flips, one CRTC per DSI connector; no compositor is
  required. `examples/kms_dualscreen` is a minimal independent version of it.
- For GPU drawing, replace dumb buffers with GBM + EGL (Mesa Panfrost, GLES) per panel; Banana
  loads Panfrost after the first frame to keep boot fast.
- Run one process per panel with DRM leases if you want isolated programs per screen.
- Keep the panel timing note in mind: both panels run slightly above 60 Hz (62.77-62.8 MHz clock,
  60.17 Hz); Banana's `banana-panel-tune` retunes them to 59.999 Hz for smoother pacing.
