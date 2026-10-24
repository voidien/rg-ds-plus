# BananaLinux as a starting point

[BananaLinux](https://github.com/JorreFog/BananaLinux) (MIT, alpha, v0.4.2 at the time of
reading, commit `6d48eb5` of 2026-10-24) is a Buildroot system that targets exactly the RG DS
and RG DS Plus. It is the closest existing thing to "path b" (own distro, own kernel config,
own device tree). This note records what its build files say. I read the source tree only:
I did not build it, run it, or inspect a prebuilt image. Tags as in FINDINGS.md.

## What it builds [V, from the repo]

| Part | Choice |
|---|---|
| Build system | Buildroot 2026.02.3 (`make rgds-plus`, `make rgds`, `make universal`) |
| Toolchain | Bootlin external aarch64 glibc "stable" |
| Kernel | **Mainline Linux 7.2.7** (gregkh mirror tarball) + ROCKNIX's RK3566 patch set |
| Kernel config | ROCKNIX's `linux-rocknix.config` + Banana fragment (squashfs root, no initramfs, no VT, no fbdev, Panfrost as a module) |
| Device trees | `rk3568-anbernic-rg-ds.dts`, `...-plus.dts` (from ROCKNIX), plus a patch setting the panels to 60 Hz |
| Out-of-tree drivers | `panel-generic-dsi.c` (ROCKNIX generic MIPI-DSI panel driver, GPL-2.0), `rocknix-singleadc-joypad.c` |
| Bootloader | **U-Boot 2026.01**, `quartz64-a-rk3566` defconfig + 2 patches, console left on |
| Rockchip blobs | `rk3568_bl31_v1.45.elf` (TF-A, kept in the repo), DDR init `rk3568_ddr_1056MHz_v1.23.bin` (from `rkbin`), both under Rockchip's binary licence |
| GPU | Mesa Panfrost, no compositor; menu draws on KMS dumb buffers |
| Init / root | BusyBox init, read-only squashfs (LZ4) root, two root slots (A/B), exFAT data partition made on first boot |
| Userland | `banana-ui` (C++), `bananad` (sleep/input daemon), RetroArch + cores, two DS emulators, PortMaster |

Patches worth reading in `buildroot/board/rgds/linux-patches/`: AW87391/AW88166 amplifier
support, RK817 charger/battery fixes, Goodix touch fixes, a CMA region patch, an SD
resume retry, and a PM patch that disables async suspend by default.

## Boot layout [V]

- `u-boot-rockchip.bin` at 32 KiB (sector 64), where the BootROM looks, then a 64 MiB FAT
  boot partition at 16 MiB with `a/Image`, `a/<board>.dtb`, `extlinux/extlinux.conf`, then
  two 256 MiB squashfs root slots.
- U-Boot SPL boot order is `sdmmc0, sdhci` (patch `002-boot-order.patch`): the first SD slot
  wins, eMMC is a fallback.
- Kernel command line: `root=PARTLABEL=banana-root-a rootfstype=squashfs ro rootwait quiet
  loglevel=3 console=ttyS2,1500000 init=/sbin/init`.
- U-Boot debug UART base is `0xFE660000` (UART2). **This agrees with the stock firmware's
  console (UART2, 1.5 Mbaud)**, so one serial setup serves both systems.

## What it tells us for the project

1. **Mainline works on this hardware**, but not purely upstream: it needs ROCKNIX's out-of-tree
   panel driver and a patch stack. That corrects my earlier "unconfirmed" on dual-panel mainline.
2. **Bring-up recipe exists**: U-Boot at sector 64, extlinux, one DTB, these patches. Reusing it is
   weeks of work, not months.
3. **RTL8821CS Wi-Fi works** via the in-tree `rtw88_8821cs`; Bluetooth is not enabled.
4. **Known gaps (their notes)**: suspend/resume timing and standby drain still unchecked on
   units, no Bluetooth, no charging while off on the Plus.
5. **Caveats**: Banana's docs disagree on whether it has booted on hardware (`FIRST-BOOT.md` says no;
   `ROADMAP.md` ticks the first boot; the README reports measured boot times), so confirm yourself.

## Using it for the UART investigation

Banana prints U-Boot and kernel messages on the same UART (1.5 Mbaud, 8N1), which makes it a
good known-good test image when you probe pads: you get text on a much quieter, more
predictable boot than stock. Plan, keeping the stock card untouched:

1. Flash `banana-rgds-plus.img.gz` (Banana's release page, or build it) to a spare microSD with
   balenaEtcher. Do not overwrite your stock card.
2. Put it in the slot the stock OS boots from (stock uses "TF1"; the first SD controller,
   `sdmmc0`, is tried first) **[I]**.
3. With the adapter on the right pads, you should see the Rockchip DDR loader, then
   `U-Boot 2026.01`, then the kernel log.
4. For userland messages as well, add `banana.console=1` to `APPEND` in
   `extlinux/extlinux.conf` on the card's FAT partition (their FIRST-BOOT.md).
5. To compare with stock, boot the original card and look for the `Vboot=` line.

## Building it yourself

```sh
git clone https://github.com/JorreFog/BananaLinux && cd BananaLinux
make rgds-plus      # downloads Buildroot, toolchain, kernel, U-Boot; expect an hour or more
```

I have not run this. Disk need is unknown to me (Buildroot builds with RetroArch, Python,
Mesa and a full kernel are large, tens of GB is a safe assumption **[?]**). `make test` builds
the UI for the host and renders it headlessly, which is a cheaper first check.

## Licensing notes

Banana's own code is MIT. Kernel patches, device trees and the panel driver are GPL-2.0 from
ROCKNIX. The TF-A and DDR blobs are under Rockchip's binary licence (redistribution terms are
in `rkbin/LICENSE`). DraStic is proprietary freeware that Banana downloads at build time; it is
not stored in their repository. Anything we ship should follow the same split.
