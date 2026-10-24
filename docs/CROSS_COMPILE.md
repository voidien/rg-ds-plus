# Cross-compiling and running the examples on the handheld

The examples are plain C. `tools/cross_build.sh` builds them for aarch64/glibc on a Debian or Ubuntu
host. Verified here (Ubuntu 24.04 host, gcc 13.3 cross compiler): all three build with no warnings.
Not verified: running any of them on the device.

## One-time host setup

```sh
sudo apt-get install gcc-aarch64-linux-gnu
# let apt *download* arm64 packages (nothing arm64 gets installed; they are unpacked into a folder)
sudo dpkg --add-architecture arm64
sudo sed -i '/^Types:/a Architectures: amd64' /etc/apt/sources.list.d/ubuntu.sources   # Ubuntu 24.04 layout
sudo tee /etc/apt/sources.list.d/arm64.sources <<'EOS'
Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports
Suites: noble noble-updates
Components: main universe
Architectures: arm64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOS
sudo apt-get update
```

Do not `apt-get install` arm64 -dev packages directly: one pulled in an arm64 Python whose install
script cannot run on an x86 host, and left dpkg in a broken state that took cleaning up. Download and
unpack, as the script does.

## Build

```sh
# optional: the device's SDL2, e.g. from `extract_firmware.py ... --partition rootfs` then debugfs rdump
STOCK_SDL2_DIR=/path/to/stock-rootfs/usr/lib tools/cross_build.sh
# -> build/aarch64/{kms_dualscreen,gbm_egl_dualscreen,dualscreen}
```

The script prints the glibc version each binary needs. The stock firmware has **glibc 2.41**, so binaries
needing up to 2.38 are fine there. BananaLinux's toolchain glibc (Bootlin "stable") is not verified:
compare with `ls -l /lib/libc.so.6` or `/lib/libc.so.6` output on the card before relying on it.
Libraries are linked by soname only (`libdrm.so.2`, `libgbm.so.1`, `libEGL.so.1`, `libGLESv2.so.2`,
`libSDL2-2.0.so.0`), so the device's own copies are used at run time.

## Run on the stock firmware

1. Copy `build/aarch64/dualscreen` and `examples/launchers/stock_dualscreen.sh` into `Roms/APPS`
   of the game card (the NNDDSS update readme names this folder and the Apps menu).
2. Run it from Applications > Apps. It creates `dualscreen.log` beside the script: the SDL video
   driver, the number of displays, and each display's bounds. This is the first thing to learn:
   whether SDL2 on Weston sees two displays, and their order.
3. KMS examples need Weston gone: `examples/launchers/stock_kms.sh kms_dualscreen` (or
   `gbm_egl_dualscreen`) stops Weston, runs for 8 seconds, then reboots to restore the UI. Untested:
   confirm `S49weston stop` really releases the displays, and read the log afterwards.

## Run on BananaLinux

Banana has no Apps menu; the sensible route is SSH (enable it in Settings > Network and add your public
key to `banana/ssh/authorized_keys`), `scp` the binaries, free the displays, run them as root. How to stop
`banana-ui` cleanly is not verified (`banana-ui-session` starts it, and it may restart); use
`ps` on the device to see the process tree first. Banana's DRM lease and panel hand-over code
(docs/DUAL_SCREEN_KMS.md) is the model for sharing the displays properly.

## First-run checklist (in order)

1. `dualscreen` on stock: log shows 2 displays? Which index is on top?
2. `kms_dualscreen` via the stock launcher: both panels light, colours differ, bars move.
3. `gbm_egl_dualscreen`: log shows the EGL vendor and GL renderer (Mali blob on stock, Panfrost on
   Banana), both panels animate, the disc crosses the hinge.
4. Report back any failing call: the examples print the failing step and `strerror`.
