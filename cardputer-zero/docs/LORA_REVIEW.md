# Review — uncommitted LoRa Phase A work (2026-11-23)

Read-only review of the uncommitted LoRa changes on branch `fix/port-review`
(HEAD `d7f0b9d`):

- `Makefile` (+10, opt-in `SQUACH_LORA=1`)
- `src/cm0/lora_lx.h`, `src/cm0/lora_lx.cpp` (new Linux SX1262 backend)
- `docs/LORA_CM0.md` (spec)

Reference points: the firmware's `LoraRadio` contract
(`firmware/include/lora_radio.h`), its RadioLib backend
(`firmware/src/lora_radio.cpp`), and the SX126x command set.

**Verdict: not ready for the device.** `begin()` cannot return true on working
hardware. If it were forced past that point, reception still could not start,
the IRQ pin would never fire, and `readPacket` would read no payload. The
reported "build verified" proves only that the file compiles, because nothing
in `src/` calls `LoraRadio::`.

---

## 1. Fatal bugs (each one alone stops the radio working)

| # | Where | Problem | Fix |
|---|-------|---------|-----|
| F1 | `begin()` reset pulse; `resetBus()` | `GpioLines::set(int i, int v)` takes the line index first, then the value. `s.reset->set(1, 0)` asks for line **1** (which doesn't exist) to go low, so RESET stays low and the chip stays in reset. | `s.reset->set(0, 1)` |
| F2 | `begin()` sync-word check | It expects `0x24 0xB4` (sync 0x2B), but `setSyncWord(0x2B)` is never called, so the register reads the default `0x14 0x24`. Result: `ok = false` and `err = -3` every time. | Call `setSyncWord(0x2B)` before the readback, as the spec's own step list says. |
| F3 | `begin()` version read | `xfer({0x1D,0x03,0x20,0x00})` sends 4 bytes, so 0 version bytes come back. `version` is empty and `chipFound` is always false. The `memcpy` length logic would also read past the buffer if the reply were 17–19 bytes. | Send 4 + 16 bytes and copy `rx[4..19]`. |
| F4 | `startReceive` / `startReceiveFor` | Opcode `0x87` is not a command. SetRx is **`0x82`**. | Use `0x82`. |
| F5 | `startCad` | Sends `0x82` (SetRx), so it starts receiving with CAD bytes as the timeout. CAD needs **SetCadParams `0x88`** (symbols, detPeak, detMin, exitMode, timeout[3] in 15.625 µs units) and then **SetCad `0xC5`**. The timeout is also passed in ms instead of ×64, and `detPeak` defaults to 0x01, which is far too low (typical is about 22 + SF). | Rewrite. |
| F6 | `irq()` | Sends `0x17` (GetDeviceErrors) instead of **GetIrqStatus `0x12`**, and assembles the two bytes in the wrong order (the reply is MSB first). | `0x12`, `(rx[2] << 8) \| rx[3]` |
| F7 | `begin()` SetDioIrqParams | Fills the overall IRQ mask but leaves the **DIO1 mask at 0**, so GPIO23 never goes high and `waitIrq()` always times out. CAD_DONE/CAD_HIT (0x0080/0x0100) are also missing from the mask. | DIO1 mask = IRQ mask; add the CAD bits. |
| F8 | `readPacket()` | GetRxBufferStatus `0x13` replies status, **len (8-bit)**, **start pointer**, but the code reads them as one 16-bit length. It then gets the start pointer from `0x12` (GetIrqStatus) instead. `xfer({0x1E, start, 0})` clocks out 3 bytes, so no payload comes back. | `len = rx[2]`, `start = rx[3]`; ReadBuffer = `{0x1E, start, NOP}` + `len` bytes. |
| F9 | `capPowerOn()` | `::write(fd,&reg,1); ::write(fd,&val,1);` is **two** I2C transactions on Linux, so the second one just sets the register pointer and no value is written. It only appears to work because `pocket-lorascan` or the boot process already set the expander up. | One 2-byte `write(fd, {reg, val}, 2)`. Check the return value. |

## 2. Wrong settings (decoding still fails after the fatal bugs are fixed)

- **Bandwidth is always 250 kHz.** `setModulation` is called with
  `p.bwKhz10 / 10`, but its table is keyed in tenths of a kHz
  (625/1250/2500/5000), so the lookup never matches and falls back to 0x05.
  This breaks MeshCore US (62.5 kHz) and every 125 kHz profile.
- **LDRO (low-data-rate optimisation) is only on when `PF_LDRO` forces it.** The firmware turns it on automatically when
  needed (`Lora::autoLdro`). Without it, SF11/SF12 at narrow bandwidths won't decode.
- **Implicit header is ignored.** `setPreamble` always sends header type
  explicit and length 0xFF; `PF_IMPLICIT` and `implicitLen` are never used.
- **Inverted IQ is missing the SX1262 errata fix**: register 0x0736 bit 2,
  which RadioLib applies.
- **Image calibration**:
  - `apply()` works out `cal` and updates `calHz`, but never runs
    CalibrateImage.
  - `tuneHz` always calibrates for 902–928 MHz whatever the frequency, and
    does it while in RX instead of standby.
  - The calibration band must follow the frequency (430–440, 470–510,
    779–787, 863–870, 902–928).
- **`readPacket` leaves fields empty:**
  - No RSSI/SNR/frequency error (GetPacketStatus `0x14` is never called).
  - No header CR.
  - No `PK_CRC_PRESENT/OK/ERR`, so CRC-failed frames go to the decoders
    unflagged. The ESP32 backend flags them with `PK_CRC_ERR`.
  - `toaUs` assumes CR 4/5 with no CRC.
- **`rssiNow()`** returns `rx[1]` (the status byte). RSSI is `rx[2]`.
- **The BUSY line is never checked before commands.** `cmd()` sends straight away. The SX1262
  requires BUSY low before every command, which RadioLib does automatically.
  Expect intermittent failures after mode changes and calibration.
- The startup frequency is 915.0 MHz; the spec says 906.875 MHz (US LongFast).

## 3. Memory safety and cleanup

- **Stack overflow in `Spi::xfer`.** `txBuf`/`rxBuf` are 256 bytes, but
  `len = n` is passed unclamped and `memset(rxBuf, 0, n)` uses `n` as well. A
  correct 255-byte ReadBuffer is 258 bytes, so **fixing F8 triggers this
  overflow**. Size the buffers from `n`.
- **GPIO/SPI handles leak.**
  - A failure partway through `begin()` leaves the lines already opened
    claimed.
  - A second `begin()` fails because the lines are still claimed (EBUSY).
  - `resetBus()` is never called, although the header says `begin()` and
    `standby()` call it.

  The spec's risk list requires a clean hand-back of the bus to
  `pocket-lorascan`.
- **`waitIrq` checks the pin every 1 ms in a busy loop.** Use gpio-v2 edge events with `poll()`. The `<poll.h>`
  include is already there but unused. This matters for battery life (see
  `cardputer-zero/BATTERY-POWER.md`).
- **The ioctl numbers and struct layouts are hand-coded.** I checked them and they are correct
  (gpio_v2_line_request = 592 bytes, fd at offset 588;
  `SPI_IOC_MESSAGE(1)` = 0x40206b00). Including `<linux/gpio.h>` and
  `<linux/spi/spidev.h>` would remove the risk.
- **Missing includes:** `<algorithm>` (`std::min`), `<cstdlib>` (`std::atoi`).
  `memset` is used without `std::`.
- **Switching `SQUACH_LORA` doesn't trigger a rebuild.** Objects built with the other setting
  are reused, so a build can mix the two.

## 4. Docs, process and accuracy

- **The spec says it isn't approved yet, but the code is already written.** `LORA_CM0.md` says "Not yet approved
  for implementation… needs explicit user approval in chat". The handoff
  summary calls it "approved".
- **The spec doesn't match the code.**
  - The file is named `lora_lx_sx1262.cpp` in the spec and `lora_lx.cpp` in
    the code.
  - The spec describes a `lora_start/lora_scan_once/lora_poll` interface; the
    code implements the firmware's `LoraRadio` interface.
  - The spec says both "receive-then-send" and that TX is out of scope.
- **The dates are in the future.** The spec and `SESSION_STATE.md` record hardware facts
  as "verified on device **2026-11-24**", but this review was written on 2026-11-23.
  Treat those facts (expander state, GPS confirmation, `pocket-lorascan`
  screenshot) as unconfirmed until re-checked.
- **The antenna claim is unsupported.** "FCC Part 15.247 compliant, 868–923 tuned" is stated
  as fact, while the same section admits no document says so.
- **A header comment contradicts itself.** It says GPS uses "GPIO14/15 (GPIO 0/1 on gpiochip0)".
- **The handoff summary overclaims.**
  - It lists the bring-up as done, when its check cannot pass (F1–F3).
  - It mentions a "firmware python/UI path", but there is no Python; the
    LoRa stack is C++.
  - It offers to push to the device for G1/G2, but G2 cannot pass as written.

## 5. Why this happened (assessment)

It is **not** mainly confusion between the Cardputer Zero and the ESP32. The code
clearly targets Linux on this board (spidev, gpiochip, i2c-dev, the
`ext_5v_out` 5 V switch, the board's pin numbers).

- **ESP32/Arduino habits carried over (a small share):**
  - F9: Arduino `Wire` sends `write(); write();` as one transaction; Linux
    `write()` calls are separate transactions.
  - The missing BUSY waits: RadioLib hid them on the ESP32.
  - The single-threaded assumption, although the firmware's sniffer runs in
    its own FreeRTOS task.
- **Writing the SX1262 driver from memory (most of the fatal bugs):** wrong
  command codes, wrong reply offsets, the DIO1 mask, the buffer read. The
  firmware's backend uses RadioLib, so there was no raw-SPI reference to
  follow, and nothing was checked against the datasheet.
- **Ordinary slips and overclaiming:** F1, F2, F3, the bandwidth lookup, the
  "approved" status, the future dates.

## 6. Recommended path

1. Approve or correct `docs/LORA_CM0.md`, and fix its dates and the mismatches
   with the code.
2. **Don't hand-write the SPI protocol.** Either:
   - use **RadioLib with a Linux hardware layer** (it has non-Arduino
     examples for Raspberry Pi using lgpio/spidev; check they cover the CM0's
     gpiochip and spidev setup). That gives the same driver behaviour as the
     firmware's `lora_radio.cpp`, including BUSY handling, CAD, packet status
     and calibration; or
   - closely port `pocket-lorascan`'s driver, which is already proven on this
     exact hardware.

   If it is hand-written anyway, cite the command code and reply layout from
   the SX1262 datasheet for every call.
3. Build a small bring-up test binary that prints the version string, sync
   word, device errors and IRQ word. Run it on the device and paste the output
   under G1/G2 before ticking them (CLAUDE.md rule).
4. Fix the SPI buffer size and the resource cleanup before reading full packets.
5. Only then add `lora_sniffer` (as a Linux thread or event loop) and the LORA page.
