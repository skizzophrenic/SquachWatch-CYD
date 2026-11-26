# Tab5: the C6 radio side

The M5Stack Tab5 is an ESP32-P4 with no radio of its own. Its WiFi and
Bluetooth are an ESP32-C6 on the same board, reached over SDIO through
Espressif's ESP-Hosted. This folder is everything about that C6: the firmware
it runs, the tool that writes it, the probe that found how to talk to it, and
the bench scripts. The P4 side (the app) is `[env:tab5]` in platformio.ini and
`src/tab5_*.cpp`; the board as a whole, and what is proven on it, is
[docs/TAB5.md](../../docs/TAB5.md).

## State on 2026-10-19

- The C6 on the bench unit (P4 MAC 30:ed:a0:e3:24:83, COM15) runs **our
  image**: ESP-Hosted slave 2.12.13 plus `squach_sniff.c`. It answers the
  hello with `squach-sniff 1`.
- The app's WiFi detection runs through it (commit f3c94e7): every WiFi shot
  from SquachEmit was caught.
- Bluetooth runs through it too: the C6's controller (stock in the slave,
  HCI over the same SDIO link) and the NimBLE host the Arduino core builds
  in, driven by esp-nimble-cpp 2.5.0 (same API as NimBLE-Arduino, which
  cannot be used: it brings its own host and expects a local controller).
  Commit 3547b2b; SquachEmit's Bluetooth trackers were all caught.
- Copies of both C6 images and the write tool are in `sim/out/tab5/c6/`
  (ignored by git): `squach-2.12.13.bin` (ours), `stock-2.12.13.bin`
  (Espressif's, for a rollback), and `bootloader.bin`, `partition-table.bin`,
  `tab5_c6_ota.bin` (the P4 tool with our image inside). The unit's original
  P4 app is backed up whole as `sim/out/tab5/tab5_monsterc5_backup_2026-10-14.bin`.

## Facts that cost an evening each

- **Version match.** The P4 Arduino core (pioarduino framework 3.3.12, IDF
  5.5.5) bundles the ESP-Hosted *host* 2.12.13, so the C6 slave must be
  **2.12.13**, not the newest.
- **Reset is active-high.** GPIO15 resets the C6, and on this unit it is
  active-HIGH. ESP-Hosted's `ESP32P4_TAB5_C6_BOARD` preset says active-low,
  and then the C6 never answers (sdmmc send_op_cond 0x107). The core's bundled
  host is already active-high; the app sets the Tab5's SDIO pins at run time
  (CLK 12, CMD 13, D0-D3 11/10/9/8, reset 15).
- **C6 power** is bit 0 of output register 0x05 on the I2C expander at 0x44
  (SDA 31, SCL 32). The app cuts it at panel init so every boot starts the C6
  fresh.
- **The slave does not start WiFi on its own.** Sniffing is refused
  (ESP_ERR_WIFI_NOT_INIT) until the P4 calls the remote esp_wifi_init /
  set_mode / start.
- **Bluetooth needs the controller switched on first.** The app calls
  `esp_hosted_bt_controller_init()` and `_enable()` after the link is up,
  then `NimBLEDevice::init()`. Without them init waits for a host sync that
  never comes, forever, so a boot whose link is down skips Bluetooth.
- **CustomRpc refuses an empty payload** (ESP_ERR_INVALID_ARG). The hello
  sends one byte. The core's host allows only 3 custom handlers.
- **Brownout on weak USB.** On a front-panel USB port the Tab5 browned out
  after about 70 s with the C6 sniffing; a back motherboard port fixed it.
- **No rollback on the C6.** An image that boots and then crashes needs the
  wired recovery: six pads beside the C6 module (GND, G9, RST, RXD, TXD, 3V3)
  and a 3.3 V USB-serial adapter; M5Burner has the factory C6 image.

## Files

| File | What it is |
|---|---|
| `squach_sniff.c` | Our addition to the stock slave. Idle until the host asks. Commands 0x5100 SNIFF, 0x5101 CHANNEL, 0x5102 HOP, 0x5103 HELLO; events 0x5180 ACK, 0x5181 HELLO, 0x5200 FRAME, 0x5201 STATS. |
| `test/` | Desk test of `squach_sniff.c` with stand-ins: `gcc -Istubs -o /tmp/sniff_test sniff_test.c && /tmp/sniff_test` in WSL. |
| `slave_sdkconfig.defaults.esp32c6` | The slave's settings (adds peer data transfer, 8 handlers). |
| `ota_patch.py`, `ota_hello_patch.py` | Patches for shadowlab's `tab5-p4-c6-sdio-ota` (Apache-2.0): retry the link, never write blind, ask for the hello, and never write more than twice without one. |
| `ota_sdkconfig.defaults`, `ota_idf_component.yml`, `ota_main_CMakeLists.txt` | The write tool's settings as built. |
| `probe/` | Read-only probe that found the reset polarity; `build_variants.sh` builds four variants. |
| `bench/` | `emit_walk.py` fires every SquachEmit category over serial and matches catches; `run_probe.py`; console readers. |

## Rebuilding (WSL, which now lives on X:)

- ESP-IDF 5.4.2 is in `~/esp-idf` (C6 and P4 tools), 5.5.1 in `~/esp-idf-5.5`.
- Slave: `~/tab5-c6/slave-2.12` is the stock 2.12.13 slave with
  `squach_sniff.c` in `main/` and hooked in `esp_hosted_coprocessor.c` after
  `example_peer_data_transfer_init()`. `idf.py build` gives
  `build/network_adapter.bin`.
- Write tool: `~/tab5-c6/ota54` (shadowlab's tool, built on IDF 5.4.2, with
  the two patches). Put the image at `firmware/network_adapter.bin`,
  `idf.py build`, write its three files to the P4 at 0x2000 / 0x8000 /
  0x10000 (after `tools/flash_known.py COM15 --check`), read the console.
- To start over from nothing: fetch `espressif/esp_hosted==2.12.13` with the
  component manager into any IDF project, copy its `slave/` folder, add the
  two settings from `slave_sdkconfig.defaults.esp32c6`, and hook in
  `squach_sniff.c` as above.
