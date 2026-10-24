# GPU path: GBM + EGL + OpenGL ES on both panels

`examples/gbm_egl_dualscreen` renders with the GPU into both panels with no compositor. It is the
GPU counterpart of `examples/kms_dualscreen` (CPU-drawn dumb buffers) and follows the same KMS
method that BananaLinux uses (see DUAL_SCREEN_KMS.md).

## Design

| Piece | Choice | Why |
|---|---|---|
| Output | one `gbm_device` on the DRM fd, one `gbm_surface` per panel (XRGB8888, `SCANOUT \| RENDERING`) | buffers the GPU renders into and the display controller can scan out directly |
| EGL | `eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR)`, one context, two window surfaces | one GL state shared by both panels; `eglMakeCurrent` per panel per frame |
| Config | the EGLConfig whose native visual id equals the GBM format | the standard way to avoid a format mismatch at scanout |
| Pacing | `eglSwapInterval(0)`; `drmModePageFlip` with a flip event gates each panel | the display's vblank paces drawing, EGL never blocks |
| Buffers | after each swap: `gbm_surface_lock_front_buffer`, wrap in a DRM fb (cached on the bo), flip; release the previously shown bo when its flip completes | at most one bo shown + one in flight per panel, so the surface never runs out of buffers |
| Drawing | `scene.c`: GLES2 full-screen plasma + a disc whose path spans both panels | easy to replace with real rendering; separated so it can be tested headless |
| Panels | `DSI-2` = top, `DSI-1` = bottom | from BananaLinux's code (not yet confirmed on your unit) |

## Test status

- `make test` (headless, software Mesa 25.2.8 llvmpipe, EGL surfaceless pbuffers): **passes**. It
  checks that the shaders compile and link, every pixel of both panels is painted, the panels are
  tinted differently, and the disc is continuous across the hinge seam at t=0.
- `make` builds `gbm_egl_dualscreen` without warnings.
- **Not tested**: the DRM/GBM/page-flip path, because this sandbox has no `/dev/dri`. The
  EGL-on-GBM platform call, `drmModeAddFB2` usage and flip handling are standard, but nothing has
  run against a real KMS device. Expect to debug on first run. Run it as root with the display free
  (stop Weston or banana-ui) and watch the stdout lines (EGL vendor, GL renderer, per-panel mode,
  frame counts).

## Where it should run

1. **BananaLinux / a custom Mesa image (intended).** Banana builds Mesa with Panfrost, EGL on DRM,
   GBM and GLES (`BR2_PACKAGE_MESA3D_PANFROST`, SDL2 KMSDRM + OpenGLES). Panfrost is a module loaded
   after the menu's first frame, so make sure it is loaded. Cross-compile with the Buildroot
   toolchain and a sysroot (`make CROSS=aarch64-linux-gnu- SYSROOT=...`).
2. **Stock firmware.** `libEGL`, `libgbm` and `libGLESv2` all resolve to
   `libmali-bifrost-g52-g24p0-wayland-gbm.so` (Rockchip's Mali blob), so GBM/EGL should exist there
   too. Weston must be stopped to free DRM master. Blob behaviour (platform extension names, config
   attributes) can differ from Mesa. Unverified.

## Next steps

- Run on hardware and confirm `DSI-2` is the top screen.
- Check frame pacing against the panel refresh (about 60.2 Hz before tuning); Banana retunes to
  59.999 Hz with `banana-panel-tune`.
- Real content: move from the demo scene to an SDL-less renderer (e.g. textured quads, a font
  atlas) or let SDL2's KMSDRM + GLES2 do the setup and keep one process per panel via DRM leases.
- Touch: read the bottom panel's Goodix evdev device (`fe5e0000.i2c`) and map it to panel 1.
