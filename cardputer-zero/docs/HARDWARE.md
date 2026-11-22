# Cardputer Zero — hardware facts

M5Stack CardputerZero (SKU C154/C154-B standard with 32 GB microSD,
C155/C155-B Lite without). Sources: M5Stack docs
(docs.m5stack.com/en/CardputerZero — spec table and pinmap), CNX-Software
(2026-05-25), Liliputing. The board is a CM0-based pocket Linux computer,
84×54×23.1 mm, 95 g.

## Verified (vendor documentation)

| Item | Value |
|---|---|
| SoC | Raspberry Pi CM0; vendor says "RP3A0 (BCM2837), quad-core Cortex-A53 @ 1 GHz, ARMv8-A (aarch64)" |
| RAM | 512 MB LPDDR2 |
| Display | 1.9" **ST7789v3, 320×170**, SPI from CM0 GPIO: G10 MOSI, G11 SCLK, G25 DC, G8 CS, G5 TE; LCD_RST/BL via M5IOE1 expander (PYG12/PYG10 PWM) |
| Keyboard | 46-key matrix (user-confirmed: includes arrow keys) |
| Wi-Fi | 2.4 GHz 802.11 b/g/n — **chipset not documented** |
| Bluetooth | present — chipset not documented |
| Audio | ES8389 codec, AW8737A amp, 1 W @ 8 Ω speaker, MEMS mic, 3.5 mm TRRS |
| Storage | microSD (standard unit ships a 32 GB card with M5Stack's OS image) |
| Battery | 3.7 V 1750 mAh LiPo, BQ27220 gauge, NTC |
| Ethernet | SR9900A USB2-to-Ethernet (10/100) |
| USB | GL852G hub; 1× USB-A 2.0; 2× USB-C (right-side port is OTG) |
| Camera | IMX219 8 MP, CSI 4-lane |
| Sensors | BMI270 IMU, BMM150 magnetometer, RX8130 RTC, IR TX/RX |
| Power | idle ~2.5 W; supply 5 V @ 2 A |

## To verify on the device (Phase 0, `scripts/hw_probe.sh`)

Resolved on 2026-11-16 (probe v1): 1, 2, 3, 4, 6 — see below.
Resolved on the re-test (2026-11-16): 5 (full input list), 9 (bq27220-0),
10/11 (display stack = labwc Wayland), 7 (monitor vif create — sudo re-run
refused with `Operation not supported (-95)`). Still open: 8 (BLE passive
sniff — the chip is right; confirm the scan runs, Phase 3).

## Verified on device (2026-11-16, probe v1)

| Item | Finding |
|---|---|
| OS | **Debian 13 (trixie), aarch64**, kernel 6.18.39+rpt-rpi-v8 (official Raspberry Pi kernel) — not Raspberry Pi OS, 64-bit despite the official CM0's 32-bit-only designation |
| CPU | 4× Cortex-A53 @ 1 GHz, 32/64-bit, 512 MB LPDDR2, 768 MB zram swap |
| Wi-Fi chip | **BCM43439 (CYW43439) over SDIO (mmc1:0001:1), driver brcmfmac**, FW brcmfmac43439-sdio 7.95.75; 2.4 GHz ch 1–11 only |
| Bluetooth chip | **CYW4343A2 (BCM4343A2)** — the Pi Zero 2 W's controller; MGMT 1.23, LE + advertising supported |
| Framebuffer | `/dev/fb0` = **320×170 16 bpp**; the panel is DRM card1 over SPI (ST7789v3); card0 is HDMI (the board has an HDMI output too) |
| Audio | card 1 = **ES8389Audio** ("cardputerzero-a", HiFi-0); card 0 = vc4-hdmi |
| Input | event0 vc4-hdmi (CEC), event1 HDMI jack, event2 gpio_ir_recv (IR), **event3 tca8418c = the 46-key keyboard** (re-test section below), event4 ES8389 headset jack |
| Storage | mmcblk0 58.6 GB: /boot/firmware (512 M) + / (58.1 G) |
| Battery | `/sys/class/power_supply/battery` empty — the real node is **`bq27220-0`** (re-test) |

**G0 radio verdict (one confirmation command pending):** Wi-Fi is a
BCM43439 on brcmfmac, which does not implement monitor mode (STA/AP/P2P
only). The dmesg "entered promiscuous mode" lines are managed-interface
promiscuous (802.3 frames, no raw beacons/deauths) — not raw 802.11 sniffing.
→ **PATH A (onboard Wi-Fi sniffing) is dead.** BLE (CYW4343A2) is unaffected:
raw HCI + passive LE scan + advertising is well proven on this controller.
**Decision made (2026-11-16): PATH B** — USB adapter on the board's USB-A
(see `RADIO.md`).

## Implications for the port

- 320×170 ≠ 320×240 (CYD): a new compact UI variant is required — new
  `cardputer_zero_user_setup.h` plus a layout-fit pass. Precedent: the
  135×240 Cardputer ADV build.
- 1.75 Wh at ~2.5 W idle ⇒ ~40 min. This device is primarily a plug-in device;
  the battery is for short field bursts.
- The USB-A port is the escape hatch if the onboard radio can't monitor
  (PATH B in `RADIO.md`).
- The board never transmits in sniff mode (monitor = passive, no scan
  requests) — same emissions posture as the ESP32 builds.

## Verified on device (2026-11-16, re-test)

- **Display stack: labwc** (Wayland compositor, `labwc -m` mini mode +
  `labwc-pi` + `swayidle -w timeout 600 wlopm --off`). No X11. The compositor
  holds the DRM master on the panel; fb0 is the kernel's view of its plane.
  → **Display backend decided: SDL2 borderless window (window mode)**, no
  fb0/DRM takeover — see ARCHITECTURE.md §5.
- **Input devices (complete list):**
  | node | device | role |
  |---|---|---|
  | event0 | vc4-hdmi | HDMI CEC (kbd) |
  | event1 | vc4-hdmi HDMI Jack | connector switch |
  | event2 | gpio_ir_recv | IR remote (kbd) |
  | **event3** | **tca8418c** (TI TCA8418C, i2c-1 @ 0x34) | **the 46-key keyboard** — its KEY bitmap spans the full standard range, consistent with a vendor-extended matrix driver (the M5Cardputer family drives its keyboards with TCA8418s; keymap references: dazhi-firmware's `tca8418_keyboard.cc`, the cardputer-adv-keyboard project) |
  | event4 | ES8389-Audio Headphones | headset jack switch |

  Exact key→keycode mapping to verify with `evtest /dev/input/event3` in
  Phase 1 (the re-test's `/sys/class/input` loop didn't expand — paste
  artifact — but the `Handlers=` lines in `/proc/bus/input/devices` give the
  mapping).
- **Battery:** `/sys/class/power_supply/bq27220-0` (type `Battery`; capacity
  69 at run time).
- **Monitor test on wlan0:** first run (no root) → `Operation not permitted`
  (EPERM, inconclusive). Re-run with `sudo`: **`Operation not supported
  (-95)`** — the kernel/driver formally refuses the monitor interface type on
  brcmfmac. PATH A is closed with driver-level evidence, not just capability
  analysis.
