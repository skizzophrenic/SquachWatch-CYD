# 2.8-inch CYD + Elecrow GPS kit (BETA)

This experimental profile is for the **ESP32-2432S028R with an ST7789
display, resistive touch and the Elecrow/Biscuit Shop V2 GPS/battery kit**
on its existing P1/CN1 harness. It is a separate `cyd-elecrow-gps` build.
The ordinary CYD choices and generic `cyd-gps` GPIO35 build remain separate.
The board code alone cannot identify an attached accessory.

## Choose the profile

In the installer, explicitly select **2.8" CYD · ST7789 + Elecrow
GPS/battery kit (BETA)** in the CYD group. Entering `ESP32-2432S028R`
selects the ordinary CYD; it does not select this accessory. This profile
uses the stock **40 MHz display clock** and has no overclock option.
For a local build, run `pio run -e cyd-elecrow-gps`; run only one PlatformIO
build at a time. See [BUILD.md](BUILD.md).

The receiver is read through **UART2, RX GPIO1, 9600 baud, 8N1, no TX**.
Keep the kit's existing wiring. No receiver commands or baud changes are
sent. GPIO1 is also the normal UART0 TX net, so this profile disables the
application USB serial console and its commands. Use the touchscreen and
DIAGNOSTICS instead. ROM/reset/download traffic and the USB bridge are
outside application control; disabling the application console does not
prove electrical isolation or eliminate boot-time traffic.

## Use GPS and record a WiGLE CSV

1. Insert a FAT32 microSD card before recording. Open DIAGNOSTICS: GPS UART
   shows RX1/9600, GPS RX counts bytes, GOOD/BAD counts parsed sentences.
   Valid sentences without a fix establish communication; a fix may require
   a clear view of the sky. Check SOURCE and POSITION as well as GPS status.
2. With a fresh receiver fix and UTC, enable **WARDRIVE** in Settings.
   DIAGNOSTICS shows waiting for fix, no card, low memory, write error or
   recording, along with the file name and written/repeat/dropped counts.
   An enabled switch alone does not establish that rows were written.
3. Turn WARDRIVE off and allow the stop to finish before removing the card.
   Read the `wigle-*.csv` file locally with a card reader and inspect the
   coordinates, UTC and WiFi/BLE rows before relying on the export.

WiGLE observations are separate from classified detection logs. Only fresh
physical receiver fixes feed this profile's wardrive export; phone and
bench sources are excluded. A paired phone can still supply a position to
other existing features, so a displayed position alone is not proof of a
receiver fix. Stale/lost fixes stop new positioned wardrive captures;
records already captured with an eligible fix retain that snapshot.

To leave memory for scanning, this profile caches 32 recently recorded devices
for repeat suppression. A full cache replaces its oldest entry; it does not
prevent new devices from being recorded. Busy locations may produce additional
repeat rows. LOW MEMORY pauses new captures while the scanner's RAM reserve
is unavailable, independently of whether GPS currently has a fix.

Working buffers are reserved at startup before the display and radios allocate,
and reused across recording sessions. They remain reserved while WARDRIVE is
off. Switching it off stops recording and clears session data, but does not
return that reservation to the heap. If the boot reservation fails, recording
retains its normal on-demand allocation retry.

Coordinates describe the CYD at observation time, not the transmitter's
exact location. Logs can contain your location, device identifiers and
network names. Inspect and share them deliberately; this workflow does
not upload the CSV. Buffered rows can be lost on sudden power failure,
and FAT metadata can be damaged. A controlled stop is not a guarantee
against every storage or power failure.

### Smaller display buffer

This profile reserves a 51,200-byte, 8-bit display buffer instead of the
76,800-byte full-screen buffer, leaving 25,600 more bytes for the application.
It draws the full display in two sections using the same buffer; display
resolution, colors and touch coordinates are unchanged. Rotation reshapes the
existing allocation and does not free or allocate another buffer. Animations
advance once per complete screen. Portrait rendering uses the library's slower
push path when a section does not meet the fast-push alignment requirement.

This is an experimental memory change. Actual contiguous heap and sustained
GPS-tagged SD recording must be checked on hardware; allocation savings alone
do not prove the low-memory pause has been resolved.

## Battery reporting

The fixed kit's MAX17048 gauge is read on CN1, SDA22/SCL27. This profile
reserves GPIO27 for I2C rather than driving it as an alternate backlight.
Settings → System → BATTERY shows percentage and cell voltage when valid;
UNAVAILABLE means the gauge did not provide a valid current reading. The
home screen also uses the existing battery lamp. Charge percentage is the
gauge's estimate, not a measured runtime prediction. Register polling has a
bounded timeout and does not reset or reconfigure the gauge.

## Hardware evidence and remaining checks

Development builds were exercised on one ST7789/resistive CYD with the fixed
ATGM336H kit: valid NMEA on GPIO1/9600, fresh fixes while scans were active,
battery-powered use, and positioned WiFi/BLE CSVs from a drive lasting more
than an hour. The two-thirds display change improved one reported wardrive
heap reading from 22,724 free/8,692 largest to 48,184 free/32,756 largest.
A later development build read battery percentage and voltage from the kit.
These observations describe development builds; cleaned contribution trees
require their own build checks and are not claimed to have been flashed.

One earlier freeze recovered after a controlled reset, with no new recorded
core dump. Its cause remains unresolved; no fix for it is claimed here.
Further route accuracy, long-run, card-removal/full-card, power-loss and
installer/recovery checks remain useful. Host tests cannot prove hardware
reliability, and the installer/release pipeline needs maintainer validation.

## Installation, settings and recovery

The manifest follows the ordinary ESP32 CYD layout: shared bootloader at
`0x1000`, partitions at `0x8000`, OTA data at `0xe000`, and this profile's
application at `0x10000`. Separate parts avoid writing the NVS settings
region; the manifest retains the project's **new-install erase prompt**.
Choosing erase clears saved settings. Compatibility of settings from
arbitrary previous firmware is not guaranteed. Writing OTA data makes
the installed app0 boot after an earlier OTA update; it is not an automatic
rollback to a previous application.

Before installing, save any firmware/settings backup and SD files you need.
Recovery uses USB through the CYD and the board's normal ROM bootloader,
which is separate from the suppressed application console. Select the
ordinary ST7789 40 MHz CYD profile to return to its non-GPS firmware, or
restore a verified backup using its recorded offsets. A merged full image
can overwrite settings. Recovery with the kit connected has not yet been
validated on hardware. Keep the existing harness connected during GPS use.

## Release integration

The release workflow explicitly builds this environment, compares its
bootloader/partitions with the shared ESP32 files, copies its application,
stamps and attaches its manifest, and publishes both to Pages. Lab runs
also package it under `lab-` names; boards absent from a restored release
are pruned from its public list. Older releases without this profile show
the installer's existing “not in that version” message.

The normal signing loop includes this environment when the maintainer's
key exists. That is release plumbing, not a promise of an available
Bluetooth/OTA path on the classic CYD. USB installation uses the manifest
parts and does not require those detached signatures. The profile appears on
the official site after a release publishes its firmware and manifest.

The install counter's local allowlist and display name include this profile.
Its Cloudflare Worker needs a separate maintainer deployment, as described
in [the counter guide](../tools/install-counter/README.md#adding-a-board).
