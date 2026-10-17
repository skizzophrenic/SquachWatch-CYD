# Hosyond CYD 4.0 port

This branch ports SquachWatch to the Hosyond/LCDWiki E32R40T-style ESP32 4.0-inch CYD using owner-tested local files. Original upstream: https://github.com/skizzophrenic/SquachWatch-CYD

- `cyd40`: ST7796S 320x480, 40 MHz SPI
- `cyd40-fast`: same hardware, 80 MHz experimental SPI clock
- XPT2046 shared display SPI, backlight GPIO27
- Speaker: GPIO26 DAC, GPIO4 active-low amplifier, four saved volume levels
- Battery voltage and estimated percent: ADC GPIO34
- Sound enable/mute UI and alert chirp use original local implementation

GitHub Actions builds both targets on pushes, pull requests, or manual dispatch. Open the Actions run to download firmware components.

**Validation:** Compiling in GitHub Actions is required; this has not been independently flashed and verified on physical hardware in this commit. The 80 MHz clock may be unstable depending on the panel.

**IMPORTANT:** The CYD40 files in this branch are a snapshot of the user's working changes. Merging future upstream updates is not automatic; reconcile conflicts carefully so CYD40 changes remain intact. Avoid flashing the full merged binary at offset zero if preserving NVS settings; the separate bootloader, partition, and app images are provided.
