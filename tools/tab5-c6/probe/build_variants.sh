#!/bin/bash
# Builds the read-only C6 probe in four variants (WSL, ESP-IDF 5.4):
# reset polarity low/high x bus 4-bit@20MHz / 1-bit@10MHz. Writes nothing to
# the C6. Output: ~/tab5-c6/probe-<variant>/build and a copy under OUT.
set -e
. ~/esp-idf/export.sh >/dev/null 2>&1
SRC=/mnt/i/GitHub/SquachWatch-CYD/SquachWatch-CYD/tools/tab5-c6/probe
OUT=/mnt/i/GitHub/SquachWatch-CYD/SquachWatch-CYD/sim/out/tab5/probe   # ignored bench output
cd ~/tab5-c6
rm -rf probe-base && cp -r ota54 probe-base && cd probe-base
cp "$SRC/main_probe.c" main/main.c
cat > main/CMakeLists.txt <<'EOF'
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES espressif__esp_hosted nvs_flash esp_event esp_timer esp_driver_i2c esp_driver_gpio)
EOF
sed -i 's/^project(.*/project(probe)/' CMakeLists.txt
grep -v '^CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ\|^CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE\|^CONFIG_ESP_HOSTED_SDIO_RESET\|^CONFIG_ESP_HOSTED_SDIO_._BIT\|^CONFIG_TAB5' sdkconfig.defaults > sd.base
rm -rf build sdkconfig
for v in low4 high4 low1 high1; do
  cd ~/tab5-c6 && rm -rf probe-$v && cp -r probe-base probe-$v && cd probe-$v
  cp sd.base sdkconfig.defaults
  echo "CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y" >> sdkconfig.defaults
  case $v in
    low4)  printf 'CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW=y\nCONFIG_ESP_HOSTED_SDIO_4_BIT_BUS=y\nCONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=20000\n' ;;
    high4) printf 'CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y\nCONFIG_ESP_HOSTED_SDIO_4_BIT_BUS=y\nCONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=20000\n' ;;
    low1)  printf 'CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW=y\nCONFIG_ESP_HOSTED_SDIO_1_BIT_BUS=y\nCONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=10000\n' ;;
    high1) printf 'CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y\nCONFIG_ESP_HOSTED_SDIO_1_BIT_BUS=y\nCONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=10000\n' ;;
  esac >> sdkconfig.defaults
  idf.py set-target esp32p4 >/dev/null 2>&1; idf.py build 2>&1 | grep -E 'error:|probe.bin binary' | head -3
  echo "$v: $(grep -c 'RESET_ACTIVE_HIGH=y\|SDIO_1_BIT_BUS=y' sdkconfig) flags set, clk $(grep SDIO_CLOCK_FREQ_KHZ= sdkconfig | head -1)"
  mkdir -p "$OUT/$v"
  cp build/bootloader/bootloader.bin build/partition_table/partition-table.bin build/probe.bin "$OUT/$v/"
done
echo ALL-BUILT
