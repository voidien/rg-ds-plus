# RG DS Plus: findings from the stock firmware

Source: Anbernic's official Linux image `RG-DS-PLUS-EN16GB-V1.0-20260915`
(`.IMG.7z`, sha256 `d1d5fc3f9a757c9221769da682fa73a4bf0488146546fc055f7d9cb81ee31c6a`)
and the NNDDSS 1.0.2 update (`.tar.xz`, sha256
`e9c2d352efb1914983498081c74a790aa51a491d38d15617ac870058b2eb56aa`).

Everything here was found by static analysis of the image with
`tools/extract_firmware.py` plus `dtc`, `strings` and `debugfs`. Nothing was run
on hardware. Tags: **[V]** read directly from a file, **[I]** inferred,
**[?]** not determined. No firmware files are stored in this repository.

## Partition layout [V]

Standard Rockchip GPT, 15 GB image. The loader (`RKNS` header) sits at sector 64.

| # | Name | Start LBA | Size | Contents |
|---|---|---|---|---|
| 1 | uboot | 16384 | 4 MiB | FIT: gzip U-Boot, TF-A (6 pieces), OP-TEE, U-Boot DT |
| 2 | misc | 24576 | 4 MiB | boot-mode flags |
| 3 | boot | 32768 | 64 MiB | FIT: kernel Image, DTB, resource container |
| 4 | recovery | 163840 | 128 MiB | FIT: same kernel + ramdisk |
| 5 | backup | 425984 | 32 MiB | |
| 6 | rootfs | 491520 | 5 GiB | ext4, Buildroot-style (kernel `root=PARTUUID=614e0000-0000`) |
| 7 | ports | 10977280 | 512 MiB | PortMaster |
| 8 | vendor | 12025856 | 1 GiB | Anbernic launcher (`ctrl/`, `bin/`, `deep/`, `subscreen/`) |
| 9 | oem | 14123008 | 3 GiB | |
| 10 | userdata | 20414464 | 1 GiB | |
| 11 | ROMS | 22511616 | ~3.2 GiB | FAT32 data |

## Software stack [V]

- Kernel: Linux 6.1.141 (Rockchip BSP), built 2026-09-15; full config is
  embedded (IKCONFIG). Mali via Rockchip's `MALI_BIFROST` driver (not Panfrost).
  `ROCKCHIP_VOP2`, `DRM_PANEL_SIMPLE`, `SUSPEND`, `FIQ_DEBUGGER` enabled.
- Bootloader: Rockchip U-Boot 2017.09 vendor fork + TF-A + OP-TEE. Android-style
  `boot` FIT; U-Boot also has distro `extlinux` boot (`boot_extlinux`).
- Userland: Rockchip Buildroot SDK; init is BusyBox `rcS`; `S49weston` starts the
  compositor; `S60launch` mounts partitions and runs `/mnt/vendor/ctrl/loadapp.sh`.
- SDL2 2.32.10 with both `wayland` and `KMSDRM` video backends.
- Wi-Fi/BT: Realtek RTL8821CS (out-of-tree `RTL8821CS.ko`, `rtl8821c_fw`, Realtek
  BT firmware, BT over UART with `rk_hciattach`). The DT still says `ap6330`; ignore.

## Boot security [V + I]

- `boot` FIT config carries a `sha256,rsa2048` signature node (`key-name-hint = "dev"`).
- U-Boot's own DT has **no** `signature` node, so there is no public key to verify
  against [V].
- U-Boot contains Rockchip AVB code with `Vboot=0` / `Vboot=1, SecureBoot enabled`
  paths, which depend on OTP fuses [V]. Whether the fuse is blown on retail
  units cannot be read from files [?].
- Community firmwares replace the whole bootloader at sector 64 and boot, which
  would fail on a fused device [I]. Conclusion: unsigned images almost certainly boot.
- Confirm on your own unit: watch the `Vboot=` line on the UART at boot.
- Recovery paths present in U-Boot: `rockusb`, `fastboot`, "Enter bootrom download" [V].

## Dual-screen architecture [V unless marked]

1. Two MIPI-DSI controllers (`dsi@fe060000`, `dsi@fe070000`), 2 lanes each, each
   with its own panel, PWM backlight, and enable/reset GPIOs (see table below).
2. Kernel exposes DRM connectors `DSI-1` and `DSI-2`.
3. `weston` (DRM backend, Rockchip-patched) drives both. `weston.ini` has
   `[output] name=DSI-1` and `name=DSI-2`, scale 1. `WESTON_OUTPUT_FLOW=horizontal`
   places the outputs side by side in compositor space (the panels are physically
   stacked).
4. Rockchip's Weston accepts runtime commands via `/tmp/.weston_drm.conf`, e.g.
   `output:DSI-2:input=gt9xx*` (bind touch to one output), `output:DSI-1:off`,
   `output:all:rotate90`, `compositor:state:sleep`.
5. Apps use `SDL_VIDEODRIVER=wayland`, `WAYLAND_DISPLAY=wayland-0`,
   `XDG_RUNTIME_DIR=/var/run`, enumerate with `SDL_GetNumVideoDisplays` /
   `SDL_GetDisplayBounds`, and open one window per display (`subscreen.dge`) [I].
6. Which connector is the top screen: **DSI-2 (controller `fe070000`) is top, DSI-1 (`fe060000`) is
   bottom**, per BananaLinux's code and comments (docs/DUAL_SCREEN_KMS.md) [V in their source, not
   yet confirmed by me on hardware]. `examples/dualscreen/` prints and colours each display so you
   can confirm.
7. Touch (Goodix GT911, I2C `0x14`, 1024x768) belongs to one screen only (the
   bottom one, per the community SDK). `echo 0|1 > /sys/class/anbernic_misc/tpctrl`
   toggles it.

## Anbernic's custom kernel interface [V]

`/sys/class/anbernic_misc/`: `tpctrl`, `lcdoff`, `runapp`, `boot_device`, `chrg_dev`,
`shutdown_voltage`, `shutdown_cap`. Not in mainline. `boot_device` reports whether
the system booted from SD (1) or eMMC; the same scripts serve the RG DS (eMMC) and
the Plus (SD only).

## Hardware map from the stock device tree [V]

| Block | Detail |
|---|---|
| SoC | RK3568 (`compatible = "rockchip,rk3568-deep-lp3-v10"`, a reused reference name) |
| Panels | 1024x768, hfront 120 / hsync 80 / hback 80, vfront 16 / vsync 8 / vback 8, pixel clock 62.8 MHz (~60.2 Hz); init sequences in DT |
| DSI0 | enable GPIO0 PC7, reset GPIO0 PB3, backlight PWM1 (enable GPIO4 PA4) |
| DSI1 | enable GPIO0 PC2, reset GPIO0 PB4, backlight PWM2 (enable GPIO4 PA3) |
| Touch | Goodix GT911 (`gt9xx`), I2C `0x14`, reset PA6, irq PC6 |
| Audio | RK817 codec (headphones/mic), 2x Awinic AW88166 speaker amps (I2C `0x34`, `0x35`, resets GPIO4 PA7 / PB2) |
| PMIC / charger | RK817 + SYR827 CPU regulator (I2C `0x40`); CW2015 fuel gauge enabled in stock |
| Battery | 4247 mAh; stock charge limits: input 1500 mA, charge 2000 mA, max 4400 mV, term 100 mA |
| SD | slot 1 sdmmc0, slot 2 sdmmc1 (detect GPIO0 PC0); eMMC controller present but unpopulated on the Plus |
| Lid | hall switch on GPIO0 PC3 |
| Debug UART | UART2 (`uart2m0`: GPIO0_D0 = RX, GPIO0_D1 = TX) at `0xfe660000`, `console=ttyFIQ0`, 1.5 Mbaud; likely 3.3 V [I]; pads not located [?] (see UART.md) |
| Disabled in stock | AW87391 amps, ICM-42607 IMU |

## Comparison with ROCKNIX's `rk3568-anbernic-rg-ds-plus.dts`

Compared at ROCKNIX commit `651150f`:

- Panel init command sequences: **byte-identical** (45 commands DSI0, 46 DSI1).
- Timings, panel/backlight/touch/amp/lid/SD GPIOs, battery capacity: match.
- ROCKNIX pixel clock is 62.77 MHz vs stock 62.80 MHz.
- ROCKNIX uses a custom `rocknix,generic-dsi` panel driver and mainline-style
  `awinic,aw88166` / `goodix,gt911` bindings; stock uses `simple-panel-dsi`,
  vendor `aw883xx_smartpa` and `gt9xx`.
- ROCKNIX disables the CW2015 gauge (conflicts with RK817 in mainline); stock enables it.
- ROCKNIX charger limits are looser (10 A, 4.35 V, 300 mA term) than stock
  (2 A, 4.4 V, 100 mA term). Its own comment calls the 10 A value "likely incorrect".
  **Prefer the stock limits** when building your own device tree.
- Stock panel `width-mm`/`height-mm` (229 x 143) are wrong placeholders; ROCKNIX's
  87 x 66 mm is right for a 4.3" 4:3 panel.

## NNDDSS emulator (v1.0.2) [V + I]

Native glibc binary with proprietary libs (`libnnddss.so`, `libnnddss_renegade_kid.so`)
and Android shims (`libandroid.so`, `libbionic_shim.so`, `libGLESv2_shim.so`,
`libOpenSLES_shim.so`). Strings include `Java_com_dsemu_drastic_DraSticJNI_*`,
which suggests Android DraStic code running through the shims [I]. No license text
ships with it; treat it as closed source.

## Not yet examined

`oem`, `userdata`, `ports` and `backup` partitions; the compiled `.dge` helpers
beyond their strings; the resource containers; the `anbernic_misc` driver code.
