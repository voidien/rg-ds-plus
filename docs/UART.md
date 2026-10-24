# UART investigation checklist

Goal: find the debug UART pads, capture a boot log, and read the `Vboot=` line (is
secure boot fused?). Nobody has published the pad locations for this board [?], so
this is a trial-and-error procedure.

## Known from the firmware [V]

- Console is UART2, mux group `uart2m0`: **GPIO0_D0 = RX, GPIO0_D1 = TX** (from the stock
  device tree pinctrl `<0 0x18 1 ...> <0 0x19 1 ...>`), registers at `0xfe660000`.
- 1,500,000 baud, 8N1. Stock: `console=ttyFIQ0` (Rockchip FIQ debugger, serial-id 2). BananaLinux:
  `console=ttyS2,1500000`. Stock U-Boot debug UART is also `0xFE660000`.
- Logic level is very likely 3.3 V (the pin group sits in the PMU IO domain fed from `vcc3v3_pmu`
  in ROCKNIX's device tree) **[I]**. Measure before connecting.

## Gear

- FT232RL (or CP2102N / CH340 that can do >= 1.5 Mbaud) USB-serial adapter with the voltage
  jumper set to **3.3 V**. Plain CP2102 tops out at 1 Mbaud.
- Female-to-female Dupont wires; pogo-pin probe or IC test hooks; optional 30 AWG wire and a
  fine-tip soldering iron; multimeter; optional 8-channel logic analyzer; plastic opening tools;
  small Phillips driver; magnification.

## Procedure

1. Power off, remove both SD cards, disconnect the battery. Open the case. Photograph both
   sides of the board before touching anything.
2. Look near the SoC for a 3-4 pad group labelled `TX`/`RX`/`GND`/`DBG`/`UART`, or unlabelled
   test points in a row. Note where ground is (continuity to a shield can or screw hole).
3. Wire only GND and the adapter's RXD to a candidate pad. Reconnect the battery, insert the
   Banana test card (docs/BANANA.md), power on. Open a terminal: `picocom -b 1500000 /dev/ttyUSB0`.
   Readable text on a pad means that pad is the board's TX.
4. Add the adapter's TXD to another candidate pad to find the board's RX (keystrokes during
   U-Boot's countdown; Banana sets a zero boot delay, so test with the stock card instead).
5. Never connect the adapter's VCC / 3.3 V / 5 V pins to the board.

## What to record

On the **stock** card: the first lines (DDR loader, `U-Boot 2017.09`), then look for
`Vboot=0, AVB images, AVB verify` (not fused) or `Vboot=1, SecureBoot enabled, AVB verify`
(fused), and any `FIT:` line. Save the whole log to `docs/logs/` (not the firmware).

On the **Banana** card: `U-Boot 2026.01` lines and the kernel log, as a known-good reference.

## Safe fallback if no pads are found

Banana can log without UART: its `banana/logs/` folder on the card holds `dmesg.txt` and
`boot.txt`, and it offers SSH over Wi-Fi. That does not show U-Boot's `Vboot=` line.
