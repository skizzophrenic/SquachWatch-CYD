#!/bin/sh
# hw_probe.sh — run on the Cardputer Zero.
# Paste the whole output into docs/HARDWARE.md, section "Verified on device".
# Non-destructive: the monitor-mode test creates and deletes a vif; nothing
# else is changed.

echo "=== 1. arch / os ==="
uname -a
lscpu 2>/dev/null | sed -n '1,15p'
head -6 /etc/os-release 2>/dev/null

echo; echo "=== 2. wifi chip + driver ==="
lsusb 2>/dev/null
dmesg 2>/dev/null | grep -iE 'wlan|wifi|cyw|brcm|bcmdhd|rtl|ath|firmware' | tail -30
for i in /sys/class/net/*/device; do
  [ -e "$i" ] || continue
  printf '%s: ' "$(basename "$(dirname "$i")")"
  cat "$i/vendor" "$i/device" 2>/dev/null | tr '\n' ' '; echo
done
ip -br link 2>/dev/null

echo; echo "=== 3. wifi capabilities ==="
iw list 2>/dev/null | grep -E 'Frequencies|MHz|Supported interface modes|valid' | head -30

echo; echo "=== 4. monitor-mode test (creates + deletes mon0) ==="
WLAN=$(ls /sys/class/net | grep -v '^lo$' | head -1)
if [ -n "$WLAN" ]; then
  iw dev "$WLAN" 2>/dev/null | sed -n '1,30p'
  if iw dev "$WLAN" interface add mon0 type monitor 2>&1; then
    ip link set mon0 up 2>/dev/null
    echo "MONITOR: ok — frames seen in 3 s:"
    timeout 3 cat /sys/class/net/mon0/statistics/rx_packets 2>/dev/null
    ip link set mon0 down 2>/dev/null
    iw dev del mon0 2>&1
  else
    echo "MONITOR: failed"
  fi
else
  echo "no wifi interface found"
fi

echo; echo "=== 5. bluetooth ==="
lsusb 2>/dev/null | grep -iE 'blue|bt|wlan'
ls /sys/class/bluetooth 2>/dev/null
btmgmt info 2>/dev/null | head -20 || echo "btmgmt unavailable"
dmesg 2>/dev/null | grep -iE 'hci|bluetooth' | tail -15

echo; echo "=== 6. framebuffer / display ==="
ls -l /dev/fb* 2>/dev/null || echo "no /dev/fb*"
for f in /sys/class/graphics/fb*/virtual_size /sys/class/graphics/fb*/bits_per_pixel; do
  [ -e "$f" ] && echo "$f: $(cat "$f")"
done
ls /sys/class/drm 2>/dev/null

echo; echo "=== 7. keyboard / input ==="
for d in /dev/input/event*; do
  [ -e "$d" ] || continue
  echo "$d: $(cat /sys/class/input/$(basename "$d")/name 2>/dev/null)"
done
sed -n '1,40p' /proc/bus/input/devices 2>/dev/null

echo; echo "=== 8. audio ==="
cat /proc/asound/cards 2>/dev/null || echo "no /proc/asound/cards"
aplay -l 2>/dev/null || echo "aplay unavailable"

echo; echo "=== 9. storage / power ==="
lsblk 2>/dev/null
echo "--- power supplies ---"
ls /sys/class/power_supply/ 2>/dev/null
for p in /sys/class/power_supply/*; do
  printf '%s: type=' "$(basename "$p")"
  cat "$p/type" 2>/dev/null || printf '?'
  printf ' capacity='
  cat "$p/capacity" 2>/dev/null || printf '?'
  echo
done

echo; echo "=== 10. display stack ==="
ps aux 2>/dev/null | grep -iE 'xorg|wayland|weston|labwc|matchbox|wayfire|sway|kiosk|hub' | grep -v grep
echo "DISPLAY=$DISPLAY WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
ls /usr/bin 2>/dev/null | grep -iE '^(X|xorg|weston|wayfire|labwc|matchbox)' | head

echo; echo "=== 11. full input list ==="
cat /proc/bus/input/devices 2>/dev/null
for f in /sys/class/input/event*/name; do
  printf '%s: ' "$f"; cat "$f" 2>/dev/null; echo
done

echo; echo "=== done ==="
