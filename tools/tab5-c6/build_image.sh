#!/bin/bash
# Build the Tab5's C6 radio image (tools/tab5-c6/c6_radio.bin) with BOTH
# extensions: the sniff (squach_sniff.c) and HUMAN SENSING (sense/: sense_c6.cpp,
# the motion model and the VITALS chest signal; hello tag "squach-sniff 2").
# The sensing files came from the Tab5 sensing app (2026-11-17) and are kept
# here so this image can be rebuilt from this repo alone.
# Runs in WSL on the ESP-Hosted 2.12.13 slave tree set up for SquachWatch:
#   ~/tab5-c6/slave-2.12   (ESP-IDF 5.4.2 at ~/esp-idf)
# Usage (from Windows):
#   MSYS_NO_PATHCONV=1 wsl bash /mnt/i/GitHub/SquachWatch-CYD/SquachWatch-CYD/tools/tab5-c6/build_image.sh
# Output: tools/tab5-c6/c6_radio.bin (embedded in [env:tab5]; written to the
# C6 by the radio update).
set -e
SQUACH=/mnt/i/GitHub/SquachWatch-CYD/SquachWatch-CYD
SLAVE=$HOME/tab5-c6/slave-2.12
cd "$SLAVE"
cp "$SQUACH/tools/tab5-c6/squach_sniff.c" main/
cp "$SQUACH"/tools/tab5-c6/sense/* main/
# The build list: the three C++ files beside squach_sniff.c, once.
if ! grep -q sense_c6.cpp main/CMakeLists.txt; then
  sed -i 's|\t"squach_sniff.c"|\t"squach_sniff.c"\n\t"sense_c6.cpp"\n\t"csi_motion_model.cpp"\n\t"csi_vitals.cpp"|' main/CMakeLists.txt
fi
# The extensions compile only when asked for, and the version shows in $I.
VER=squachwatch
if ! grep -q SENSE_C6 main/CMakeLists.txt; then
  printf '\ntarget_compile_definitions(${COMPONENT_LIB} PRIVATE SENSE_C6=1 FIRMWARE_VERSION="%s")\n' "$VER" >> main/CMakeLists.txt
else
  sed -i "s|FIRMWARE_VERSION=\"[^\"]*\"|FIRMWARE_VERSION=\"$VER\"|" main/CMakeLists.txt
fi
# The hook: after the sniff extension, in app_main.
if ! grep -q squach_sense_init main/esp_hosted_coprocessor.c; then
  sed -i 's|\tsquach_sniff_init();|\tsquach_sniff_init();\n\t/* Tab5 CSI Human Sensing: the sensing extension, idle until asked. */\n\textern void squach_sense_init(void);\n\tsquach_sense_init();|' main/esp_hosted_coprocessor.c
fi
# CSI on in the WiFi driver (off in the stock slave).
grep -q '^CONFIG_ESP_WIFI_CSI_ENABLED=y' sdkconfig || sed -i 's|^# CONFIG_ESP_WIFI_CSI_ENABLED is not set|CONFIG_ESP_WIFI_CSI_ENABLED=y|' sdkconfig
grep -q '^CONFIG_ESP_WIFI_CSI_ENABLED=y' sdkconfig.defaults.esp32c6 || echo 'CONFIG_ESP_WIFI_CSI_ENABLED=y' >> sdkconfig.defaults.esp32c6
. "$HOME/esp-idf/export.sh" >/dev/null
idf.py build 2>&1 | tail -n 25
cp build/network_adapter.bin "$SQUACH/tools/tab5-c6/c6_radio.bin"
ls -l "$SQUACH/tools/tab5-c6/c6_radio.bin"
