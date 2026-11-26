# src/ — planned CM0 glue (created in Phase 1)

Nothing exists here yet. Phase 1 creates:

```
src/cm0/
  main_linux.cpp      process bootstrap: threads, fb init, evdev, radio start
  fb_display.cpp      sim framebuffer -> /dev/fb0 (320x170, 16 bpp)
  evdev_keys.cpp      46-key keyboard -> firmware keyboard input events
  radio_linux.h       the shim contract (ARCHITECTURE.md section 4)
  wifi_mon.cpp        monitor vif + AF_PACKET -> promiscuous rx callback
  wifi_scan.cpp       managed-mode scan for network lists / NTP
  ble_hci.cpp         raw HCI -> BLEScan callback + LE advertising (squad hello)
  audio_alsa.cpp      ES8389 ALSA playback of the firmware's sound samples
  sd_file.cpp         CSV detection log + file-backed Preferences (32 GB SD)
  ota_linux.cpp       signed OTA: download -> verify (project key) -> atomic
                      swap -> reboot
```

Plus `cardputer_zero_user_setup.h` (TFT_WIDTH 320 / TFT_HEIGHT 170) —
provided as an include override to the firmware build, not committed to the
firmware repo unless it lands upstream.
