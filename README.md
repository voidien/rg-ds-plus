# rg-ds-plus

Research and tooling for writing custom software for the Anbernic RG DS Plus
(Rockchip RK3568, dual 1024x768 screens, microSD boot).

- [docs/RESEARCH.md](docs/RESEARCH.md): feasibility report, community landscape, development paths.
- [docs/FINDINGS.md](docs/FINDINGS.md): hardware and software facts extracted from the stock firmware,
  and a comparison with ROCKNIX's device tree.
- [docs/UART.md](docs/UART.md): checklist for finding the UART pads and capturing the boot log.
- [docs/BANANA.md](docs/BANANA.md): what BananaLinux's build files show (kernel, U-Boot, image layout)
  and how to use it as a UART test image.
- [tools/extract_firmware.py](tools/extract_firmware.py): unpacks an official image
  (partitions, boot FIT, kernel, kernel config, device trees).
- [examples/dualscreen](examples/dualscreen): SDL2 test that opens one window per display.

## Using the tools

```sh
# Anbernic's image is not in this repo; download it yourself and unpack the .7z.
mkdir /some/new/empty/dir
python3 tools/extract_firmware.py RG-DS-PLUS-EN16GB-V1.0-20260915.IMG /some/new/empty/dir
# add --partition rootfs --partition vendor to also dump those filesystems
```

Needs Python 3 and (optionally) `dtc` to produce `.dts` files. Extracted files are
Anbernic's property: `.gitignore` keeps them out of the repo.

## Suggested next steps

1. Run `examples/dualscreen` on the device (stock firmware, from the launcher's Apps menu)
   to learn which display index is the top screen and confirm per-display windows work.
2. Capture a UART boot log (look for the `Vboot=` line) once the pads are found.
3. Build BananaLinux, then adapt it into your own Buildroot image.
