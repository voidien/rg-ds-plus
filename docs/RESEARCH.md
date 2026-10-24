# Developing custom software for the Anbernic RG DS Plus

Research date: 2026-10-24. Tags: **[V]** verified in a source, **[I]** inference,
**[?]** unconfirmed. Hardware-level facts from the stock image are in
[FINDINGS.md](FINDINGS.md) and supersede anything here that disagrees.

## Summary

- Feasible: apps on the stock OS (reverse-engineered SDK exists), a custom Buildroot
  distro (BananaLinux already ships one for this device), community-patched kernels.
- Hard: no official Anbernic kernel/bootloader source was found; dual-screen polish
  (suspend, Wi-Fi, Bluetooth, charging) is alpha-quality in every community OS; a
  from-scratch OS still needs Rockchip's closed DRAM-init blob.
- Brick risk is low [I]: no eMMC, boot is from microSD, so a bad image is fixed by
  swapping the card.
- Start: SDL2 apps on stock, then read BananaLinux/ROCKNIX, then build your own
  Buildroot image reusing their kernel fragment and U-Boot patches.

## Device facts

| Claim | Finding |
|---|---|
| RK3568, 4x Cortex-A55, Mali-G52 2EE | Confirmed. One review calls it RK3566; ROCKNIX files it under an "RK3566" directory but the DT includes `rk3568.dtsi`. The "2.0 GHz" figure is unverified. |
| 1 GB RAM, no eMMC, dual microSD | Confirmed ([unboxing](https://retrohandhelds.gg/anbernic-rg-ds-plus-unboxing-reveals-full-specs-and-more/), [review](https://retrohandhelds.gg/anbernic-rg-ds-plus-review/)). |
| Two 4.3" 1024x768 screens, touch + stylus | Screens confirmed. Touch is on the bottom screen only (single Goodix GT911). |
| Wi-Fi, BT 4.2 | Confirmed; chip is Realtek RTL8821CS (from the firmware). |
| Anbernic Linux + "NNDDSS" | Confirmed; spelling varies (NNDDSS/NNDSS). |
| Sibling of RG DS, 3 GB RAM | Partly wrong: the RG DS has 3 GB, 32 GB eMMC, Android 14 and two 4" 640x480 screens ([source](https://retrohandhelds.gg/?p=28448)). Device trees differ; the SoC is shared. |

## 1. Openness

- No GPL source release from Anbernic found. [Anbernic's firmware page](https://win.anbernic.com/linux_xitong/)
  offers Google Drive downloads with no source or licence pointer. A [Knulli page](https://knulli.org/pl/devices/anbernic/rg35xx)
  says Anbernic did not publish U-Boot/kernel source for the RG35XX (different device).
- Device trees exist from ROCKNIX ([distribution repo](https://github.com/ROCKNIX/distribution),
  `rk3568-anbernic-rg-ds.dts` / `-plus.dts`), and from the stock image itself.
- Open: TF-A, U-Boot, Mesa Panfrost. Proprietary: DRAM init in `u-boot-rockchip.bin`,
  RTL8821CS firmware, Rockchip Mali userspace, NNDDSS.

## 2. Community work

| Project | Status |
|---|---|
| [ROCKNIX](https://github.com/ROCKNIX/distribution) | Device trees for RG DS and Plus; install guide for the RG DS: [gist](https://gist.github.com/ggtylerr/75750b7b26627d6b9cd95edf12b6b92d). Plus support not confirmed. |
| Knulli | RG DS alpha, supporters only ([article](https://retrohandhelds.gg/anbernic-rg-ds-how-to-install-gammaos-anbernic-linux-rocknix-and-knulli/)). |
| GammaOS | Android build for the RG DS. |
| [BananaLinux](https://github.com/JorreFog/BananaLinux) | Buildroot OS for both; own KMS UI; two DS emulators; alpha. |
| [ROCKNIXDS](https://github.com/JorreFog/ROCKNIXDS), [RetroDualStation](https://github.com/Ninte86hl/RetroDualStation) | Dual-screen DraStic/EmulationStation on ROCKNIX. |
| [anbernic_rgds_plus SDK](https://github.com/youngelf/anbernic_rgds_plus), [dsboot](https://github.com/abradburne/dsboot), [rgds-core](https://github.com/kleinrein/rgds-core) | Stock-OS tooling. |

Generic ROCKNIX dual-screen is weak: MelonDS-SA reportedly misbehaves; see
[advanced_drastic#10](https://github.com/trngaje/advanced_drastic/issues/10).

## 3. Mainline Linux (RK356x family, from [Pine64's Quartz64 table](https://wiki.pine64.org/wiki/Quartz64_Development))

VOP2 5.19; DSI 6.1; Panfrost 5.18; I2S 5.16; RK817 codec 5.14, charger 6.1.
Panthor covers CSF Valhall GPUs (RK3588), not the G52 (Bifrost, Panfrost).
Update after reading BananaLinux's build files ([BANANA.md](BANANA.md)): it boots both DSI panels on
mainline Linux 7.2.7 plus ROCKNIX's patch set and an out-of-tree generic DSI panel driver, so "works on
mainline" is true with patches, not purely upstream. Suspend-to-RAM is implemented but still being
validated on units, and it must unload the Wi-Fi module first.

## 4. Boot chain and recovery

BootROM loads `u-boot-rockchip.bin` from sector 64 (DRAM init + SPL + TF-A + U-Boot),
then U-Boot reads `extlinux/extlinux.conf` (BananaLinux/ROCKNIX) or the stock Android-style
`boot` FIT. Stock U-Boot has rockusb/fastboot/bootrom-download commands. Signature
enforcement almost certainly off; see FINDINGS.md. Maskrom pads not located [?].

## 5. Development paths

| Path | Difficulty | Time to first result | Payoff | Risk |
|---|---|---|---|---|
| a. Apps/ports on stock or community OS (SDL2, Love2D, Godot, PortMaster) | Low | Days | Working dual-screen apps now | Very low |
| b. Buildroot/Yocto distro, own kernel config + DT | Medium | 2-6 weeks reusing BananaLinux/ROCKNIX [I] | Full control of boot, services, UI | Low |
| c. Custom kernel (mainline vs vendor BSP) | High | Months | Upstream-quality support; BSP is easier but unmaintainable | Low, heavy maintenance |
| d. From-scratch OS / bare metal | Very high | Months to years [I] | Learning value | Low |

TRM: only Part 1 V1.3 found ([PDF](https://opensource.rock-chips.com/images/2/26/Rockchip_RK3568_TRM_Part1_V1.3-20220930P.PDF), via [Pine64](https://pine64.org/documentation/PineTab2/Further_information/Datasheets)). No bare-metal hobby project for this chip found.

## 6-8. Dual-screen, toolchain, references

See FINDINGS.md (Weston + two DRM outputs, SDL2 per-display windows, BananaLinux's
direct KMS). Toolchain: aarch64 GCC or Buildroot; UART2 at 1.5 Mbaud (pads unlocated);
learning path: SDL2 app on stock, flash BananaLinux/ROCKNIX, rebuild BananaLinux,
own image, then kernel/panel upstreaming.

## Open questions

1. Is the RG DS Plus image published for ROCKNIX, and how well does it run?
2. UART pad and maskrom pad locations.
3. Does Anbernic supply GPL source on request?
4. Confirm on hardware that DSI-2 is the top screen (BananaLinux says so; run `examples/dualscreen`)
