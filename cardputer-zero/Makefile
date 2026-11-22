# SquachWatch-CM0 -- the Cardputer Zero build of SquachWatch-CYD.
#
# An app on the stock OS, not a kiosk: the firmware's real UI and real
# detection engine, compiled against the firmware's own sim shims (reused,
# not vendored -- the -I$(SIM) below) plus this port's radio stubs
# (src/cm0/radio_stub/, Phase 2 swaps the definitions) and the SDL2
# display harness (src/cm0/main_linux.cpp).
#
#   make                             native build, fast iteration
#   make CROSS=aarch64-linux-gnu-    the device binary (aarch64)
#   make SQUACHWATCH=/path/to/checkout
#
# The firmware checkout is never modified: every line this build compiles
# comes from it, exactly as the firmware's own sim/Makefile does.
#
# Objects go under build/ with -MMD dependency files, so an edit recompiles
# only what changed. Every object also depends on this Makefile: a flag
# change here rebuilds everything instead of leaving a binary of mixed
# flags, which is exactly the kind of thing that sends you chasing a bug
# that is not there.

# The firmware is the enclosing SquachWatch-CYD tree (this folder lives in
# it, next to sim/). After a firmware change, rebuild and re-audit the
# CARDPUTER_ADV / SQW_SMALL code (this build borrows those flags, see
# "Board flags" below); `make test` flags the copies that drifted.
# SQUACHWATCH=/path builds against another checkout.
SQUACHWATCH ?= ..
SRC   := $(SQUACHWATCH)/src
INC   := $(SQUACHWATCH)/include
SIM   := $(SQUACHWATCH)/sim
CM0   := src/cm0
STUB  := $(CM0)/radio_stub

CROSS ?=
CXX := $(CROSS)g++

ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifeq ($(wildcard $(INC)),)
$(error SquachWatch-CYD not found at $(SQUACHWATCH) -- set SQUACHWATCH=/path/to/checkout)
endif
endif

EXTRA_CXXFLAGS ?=

# The firmware's version, stamped from its git tag as the firmware's own
# build stamps it (v1.49.0, or v1.49.0-3-gabc1234 between tags). It is not
# only shown: OtaCore::runningVersion() (ota_cm0.cpp) is what the squad
# protocol tells squadmates and compares against.
FIRMWARE_VERSION := $(shell git -C $(SQUACHWATCH) describe --tags --always 2>/dev/null || echo unknown)

# Include order, first wins:
#  1. radio_stub: stand-ins for the ESP32 radio headers the firmware
#     includes. Phase 2 replaces the definitions in radio_stub.cpp with
#     the USB adapter (AF_PACKET monitor vif) and the raw-HCI scanner;
#     the headers -- and the firmware -- do not move.
#  2. the firmware's sim/: Arduino.h, TFT_eSPI.h, Preferences.h, the SPI/
#     Wire/XPT2046 shims, the PNG writer -- the exact shims the firmware's
#     own emulator links against, so the UI renders the way the firmware's
#     sim renders it.
#  3. the firmware's include/: the project's own headers.
#
# Board flags: the firmware's own Cardputer build (sim/Makefile, the
# wasm-cardputer target), minus the browser-only bits:
#  - CARDPUTER_ADV: the TCA8418-keyboard board family; the firmware's
#    keyboard-driven cursor path
#  - SQW_SMALL:     the small-screen layout
#  - SQUACHSIM:     smallKeysRead() returns s_simKeys, which this harness
#    feeds through simCardKey() (defined in main.cpp's SQUACHSIM branch);
#    it also skips the real TCA8418 I2C polling, which the Wire shim
#    would NACK anyway
#  - SQW_DEVTOOLS:  for ONE thing -- the firmware's typed-character channel
#    (g_devType, read by devKeysMerge()), which the harness feeds so the
#    text screens (squad name, WiFi password, PIN) can be typed on and
#    letters work as hotkeys; simCardKey() cannot carry a character.
#    Audited for v1.49: every other SQW_DEVTOOLS hook runs only on a command
#    from the firmware's serial dev console, and that console has no input
#    in this build (SQUACHSIM_SERIAL_IN is not defined, so the sim's
#    Serial.available() is always 0). Re-check on an upstream bump.
#  - SQW_WIFI_5G:   the 5 GHz sniffer path (boot probe of the 28 channels
#    the C5 bit table maps to, the 5 GHz slice in the hop clock, the
#    per-channel accounting). Whether it is actually active at runtime is
#    decided by the adapter's wiphy (radio_stub asks wifiMonHasFiveGhz),
#    so a 2.4-only dongle boots with the path compiled but inert.
#
# sys_shim (first of all): esp_system.h/esp_random.h on the kernel CSPRNG,
# shadowing the sim's fixed-seed LCG -- the device's keys, nonces and
# phrases must not be the emulator's repeatable ones.
#
# mbedtls: the firmware's REAL mesh cipher (src/meshcrypto.cpp) links
# against the system mbedtls 3 -- never the sim's meshcrypto_sim.cpp
# stand-in, which says of itself "must never be linked into firmware".
# Arch ships it under /usr/include/mbedtls3 + /usr/lib/mbedtls3; Debian
# (the arm64 build container: apt-get install libmbedtls-dev) in the
# default paths. libmbedcrypto is linked statically so the device needs no
# extra package; MBEDTLS_STATIC=0 links it dynamically instead.
ifneq ($(wildcard /usr/include/mbedtls3/mbedtls/version.h),)
MBEDTLS_INC ?= /usr/include/mbedtls3
MBEDTLS_LIBDIR ?= /usr/lib/mbedtls3
endif
MBEDTLS_STATIC ?= 1
MBEDTLS_CFLAGS := $(if $(MBEDTLS_INC),-isystem $(MBEDTLS_INC))
ifeq ($(MBEDTLS_STATIC),1)
MBEDTLS_LIBS := $(if $(MBEDTLS_LIBDIR),-L$(MBEDTLS_LIBDIR)) -Wl,-Bstatic -lmbedcrypto -Wl,-Bdynamic
else
MBEDTLS_LIBS := $(if $(MBEDTLS_LIBDIR),-L$(MBEDTLS_LIBDIR)) -lmbedcrypto
endif

CXXFLAGS := -std=c++17 -O2 -g -Wall -Wno-unused-parameter $(EXTRA_CXXFLAGS) \
            -DFIRMWARE_VERSION=\"$(FIRMWARE_VERSION)\" \
            -I$(CM0)/sys_shim -I$(STUB) -I$(CM0) -I$(SIM) -I$(INC) \
            $(MBEDTLS_CFLAGS) \
            -include $(CM0)/sys_shim/arduino_cm0.h \
            -include $(CM0)/cardputer_zero_user_setup.h \
            -DSQUACH_MESH=1 -DCYD_GPS=1 \
            -DCARDPUTER_ADV=1 -DSQW_SMALL=1 -DSQUACHSIM=1 -DSQW_DEVTOOLS=1 \
            -DSQW_WIFI_5G=1 \
            -DCYD_GPS_BAUD=115200

LDLIBS := -lSDL2 $(MBEDTLS_LIBS) -lpthread -lasound

# The firmware's shared UI: the sim Makefile's list, minus its two
# stand-ins -- detection_sim.cpp (the real detection.cpp below replaces it)
# and meshcrypto_sim.cpp (the real meshcrypto.cpp replaces it).
UI_SRCS := $(SRC)/theme.cpp \
            $(SRC)/gnss.cpp \
            $(SRC)/squachy.cpp \
            $(SRC)/squachmesh.cpp \
            $(SRC)/mesh.cpp \
            $(SRC)/meshmsg.cpp \
            $(SRC)/emote_script.cpp \
            $(SRC)/crowd_bench.cpp \
            $(SRC)/meshwords.cpp \
            $(SRC)/meshtalk.cpp \
            $(SRC)/meshtutor.cpp \
            $(SRC)/meshcrypto.cpp \
            $(SRC)/ui_phone.cpp \
            $(SRC)/qwerty.cpp \
            $(SRC)/ui_meshmenu.cpp \
            $(SRC)/ui_meshwarn.cpp \
            $(SRC)/ui_beaconwarn.cpp \
            $(SRC)/security.cpp \
            $(SRC)/ui_security.cpp \
            $(SRC)/ui_squad.cpp \
            $(SRC)/ui_meshphrase.cpp \
            $(SRC)/ui_meshcompose.cpp \
            $(SRC)/settings.cpp \
            $(SRC)/lora_myname.cpp \
            $(SRC)/signatures.cpp \
            $(SRC)/remote_id.cpp \
            $(SRC)/clock.cpp \
            $(SRC)/blackbox.cpp \
            $(SRC)/log_index.cpp \
            $(SRC)/bingo.cpp \
            $(SRC)/ui_bingo.cpp \
            $(SRC)/dex.cpp \
            $(SRC)/regulars.cpp \
            $(SRC)/notices.cpp \
            $(SRC)/ui_dex.cpp \
            $(SRC)/detection_info.cpp \
            $(SRC)/device_info.cpp \
            $(SRC)/idle_events.cpp \
            $(SRC)/ui_clear.cpp \
            $(SRC)/ui_power.cpp \
            $(SRC)/ui_light.cpp \
            $(SRC)/status_light.cpp \
            $(SRC)/pet.cpp \
            $(SRC)/ui_log.cpp \
            $(SRC)/ui_alert.cpp \
            $(SRC)/ui_settings.cpp \
            $(SRC)/ui_boot.cpp \
            $(SRC)/ui_colorcheck.cpp \
            $(SRC)/ui_diagnostics.cpp \
            $(SRC)/ui_desk.cpp \
            $(SRC)/ui_zone.cpp \
            $(SRC)/ui_diary.cpp \
            $(SRC)/ui_hunt.cpp \
            $(SRC)/ui_outfit.cpp \
            $(SRC)/ui_outfit_unlock.cpp \
            $(SRC)/ignore_list.cpp \
            $(SRC)/ui_ignorelist.cpp \
            $(SRC)/ui_detfilter.cpp \
            $(SRC)/ui_rawscan.cpp \
            $(SRC)/ui_watchalert.cpp \
            $(SRC)/ui_update.cpp \
            $(SRC)/ui_nudge.cpp \
            $(SRC)/ui_squadupdate.cpp \
            $(SRC)/ui_invite.cpp \
            $(SRC)/ui_wifipass.cpp \
            $(SRC)/ui_sysprops.cpp \
            $(SRC)/ui_wifinets.cpp \
            $(SRC)/frame_push.cpp \
            $(SRC)/frame_prof.cpp \
            $(SRC)/fast_sprite.cpp \
            $(SRC)/wardrive_fmt.cpp \
            $(SRC)/wifi_auth.cpp \
            $(SRC)/wardrive_seen.cpp \
            $(SRC)/wardrive_sd.cpp \
            $(SRC)/sd_row.cpp \
            $(SRC)/sound.cpp \
            $(SRC)/my_alerts.cpp \
            $(SRC)/ui_myalerts.cpp \
            $(SRC)/ui_cryptid_guide.cpp \
            $(CM0)/ota_cm0.cpp

# The radio half: the real engine plus the firmware's SD log (detection.cpp
# carries an SdLog member, include/sd_log.h), and the stub that stands in
# for the ESP32's radios until Phase 2.
RADIO_SRCS := $(SRC)/detection.cpp \
              $(SRC)/sd_log.cpp \
              $(STUB)/radio_stub.cpp

# The app's own harness: the SDL2 window, keyboard and stdin protocol; and
# the firmware's real main.cpp with the two modules it drags in.
APP_SRCS := $(SRC)/main.cpp \
            $(SRC)/cap_touch.cpp \
            $(SRC)/touch_cal.cpp \
            $(CM0)/main_linux.cpp \
            $(CM0)/wifi_mon.cpp \
            $(CM0)/ble_scan.cpp \
            $(CM0)/battery.cpp \
            $(CM0)/selftest.cpp \
            $(CM0)/prefs_store.cpp \
            $(CM0)/user_fs.cpp \
            $(CM0)/sd_card.cpp \
            $(CM0)/gps_serial.cpp \
            $(CM0)/fb_io.cpp \
            $(CM0)/overlay.cpp \
            $(CM0)/dimmer.cpp \
            $(CM0)/i2s_alsa.cpp \
            $(CM0)/coming_soon.cpp \
            $(CM0)/sound_log.cpp \
            $(CM0)/shake_iio.cpp

SRCS := $(UI_SRCS) $(RADIO_SRCS) $(APP_SRCS)

# Settings for features this board does not have yet (EXT SCREEN, UPDATE
# CHECK): their calls are routed to coming_soon.cpp, which keeps them OFF
# and shows "FEATURE COMING SOON" -- see that file. Theme::showToast is
# wrapped as well, to turn the EXT SCREEN row's own "EXT SCREEN OFF" toast
# (which would replace ours) into the same message. Sound::play is wrapped
# by sound_log.cpp: one log line per alert sound or dropped request.
COMING_SOON_LDFLAGS := -Wl,--wrap=_ZN8Settings9extScreenEv \
                       -Wl,--wrap=_ZN8Settings15toggleExtScreenEv \
                       -Wl,--wrap=_ZN8Settings11updateCheckEv \
                       -Wl,--wrap=_ZN8Settings17toggleUpdateCheckEv \
                       -Wl,--wrap=_ZN5Theme9showToastEPKcS1_tj \
                       -Wl,--wrap=_ZN5Sound4playENS_2IdEb

BIN := squachd
OBJ_DIR := build
UI_OBJS   := $(patsubst %.cpp,$(OBJ_DIR)/ui/%.o,$(notdir $(UI_SRCS)))
CM0_OBJS  := $(patsubst %.cpp,$(OBJ_DIR)/cm0/%.o,$(notdir $(RADIO_SRCS) $(APP_SRCS)))
OBJS := $(UI_OBJS) $(CM0_OBJS)
DEPS := $(OBJS:.o=.d)

# Where a .cpp can come from. A same-basename collision would let vpath
# silently pick the wrong file; the guard keeps that honest.
vpath %.cpp $(SRC) $(SIM) $(CM0) $(STUB)
CLASH := $(filter $(notdir $(wildcard $(SIM)/*.cpp) $(wildcard $(CM0)/*.cpp) $(wildcard $(STUB)/*.cpp)),$(notdir $(wildcard $(SRC)/*.cpp)))
ifneq ($(CLASH),)
$(error same basename in the firmware src/ and the sim/cm0 dirs; vpath would pick the wrong one: $(CLASH))
endif

all: $(BIN)

# FIRMWARE_VERSION comes from git, not from this file, so the Makefile
# dependency below cannot see it change (a new commit, a tag). The
# stamp is rewritten only when the version differs from what it holds, and
# every object depends on it: a new version rebuilds everything that
# compiled the old one into it -- never a binary that tells the squad a
# stale version.
VERSION_STAMP := $(OBJ_DIR)/firmware-version
$(shell mkdir -p $(OBJ_DIR); [ "$$(cat $(VERSION_STAMP) 2>/dev/null)" = "$(FIRMWARE_VERSION)" ] || echo "$(FIRMWARE_VERSION)" > $(VERSION_STAMP))

$(OBJ_DIR)/ui/%.o: %.cpp Makefile $(VERSION_STAMP)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/cm0/%.o: %.cpp Makefile $(VERSION_STAMP)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# The detection log on the "card" (a folder, see radio_stub/SD.h). Up to
# v1.41 the firmware's SdLog::begin() refused on every SQW_SMALL build, so
# this unit was compiled with -USQW_SMALL. Since v1.43 the refusal skips
# CARDPUTER_ADV builds (the Cardputer ADV has a slot) and this build
# defines that flag, so sd_log.cpp now compiles with the same flags as
# everything else. sdlog-slot-check guards it: the TWATCH_S3/SQW_SMALL
# "no card slot" refusal must stay compiled out of this build.
sdlog-slot-check:
	@mkdir -p $(OBJ_DIR)/check
	@$(CXX) $(CXXFLAGS) -E -C $(SRC)/sd_log.cpp -o $(OBJ_DIR)/check/sd_log.i
	@if grep -q 'GPIO19/20 are the S3' $(OBJ_DIR)/check/sd_log.i; then \
	   echo "sdlog-slot-check: FAIL -- SdLog::begin() refuses on this build again (upstream changed the gate)"; exit 1; \
	 else echo "sdlog-slot-check: ok (SdLog::begin's no-slot refusal is compiled out)"; fi

# main.cpp includes <SD.h> only `#if defined(ESP32)`, but its CARDPUTER_ADV
# code (the /gps-status.log wardrive diary, v1.43) uses File and SD on any
# build that defines the flag -- this one. The folder-backed SD.h is what
# the ESP32 header would have given it.
$(OBJ_DIR)/cm0/main.o: CXXFLAGS += -include $(STUB)/SD.h

# The speaker: upstream gives CARDPUTER_ADV a tones-only player (no noise,
# breath or echo: that board has no PSRAM). The Cardputer Zero has memory
# to spare, so sound.cpp alone is compiled without the flag and keeps the
# full renderer (the firmware's #else pins, which i2s_alsa ignores). That
# is only sound while no header in the unit changes with the flag (an
# inline or a layout that did would break the one-definition rule), so
# `make test` runs sound-odr-check over every header the unit includes.
$(OBJ_DIR)/ui/sound.o: CXXFLAGS += -UCARDPUTER_ADV

sound-odr-check:
	@mkdir -p $(OBJ_DIR)/check
	@$(CXX) $(CXXFLAGS) -UCARDPUTER_ADV -MM -MT x $(SRC)/sound.cpp -MF $(OBJ_DIR)/check/sound.deps
	@hdrs=$$(tr ' \\' '\n\n' < $(OBJ_DIR)/check/sound.deps | grep '\.h$$'); \
	 bad=$$(grep -l CARDPUTER_ADV $$hdrs src/cm0/sys_shim/arduino_cm0.h src/cm0/cardputer_zero_user_setup.h); \
	 if [ -n "$$bad" ]; then echo "sound-odr-check: FAIL -- headers that change with CARDPUTER_ADV:"; echo "$$bad"; exit 1; \
	 else echo "sound-odr-check: ok ($$(echo "$$hdrs" | wc -l) headers, none read CARDPUTER_ADV)"; fi

# The Debian package (scripts/build-deb.sh): dist/squachwatch_<ver>_arm64.deb,
# installing /usr/bin/squachd (root:netdev 0750 + the two caps, set by its
# postinst), the launcher entry and its icon. Build it in the arm64
# container (docker/Dockerfile); install with `sudo apt install ./<deb>`.
deb: $(BIN)
	sh scripts/build-deb.sh $(BIN) dist

# Install on the device and into its app launcher (scripts/install.sh):
#   make deploy DEVICE=user@host     (after building the aarch64 binary)
# Copies the binary, the launcher entry and its icon to a fresh temp dir
# there and runs the installer with sudo (root:netdev 0750 + the two caps).
deploy: $(BIN)
	@test -n "$(DEVICE)" || { echo "make deploy DEVICE=user@host"; exit 1; }
	@d=$$(ssh $(DEVICE) mktemp -d) && \
	 scp -q $(BIN) assets/squachwatch.desktop assets/squachwatch.png scripts/install.sh $(DEVICE):$$d/ && \
	 ssh $(DEVICE) "sudo sh $$d/install.sh $$d; rm -f $$d/squachd $$d/squachwatch.desktop $$d/squachwatch.png $$d/install.sh; rmdir $$d"

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(COMING_SOON_LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

-include $(DEPS)

# The RX-pipeline unit test: the real wifi_mon.o, no radio needed.
TEST_BIN := build/wifi_mon_test
$(OBJ_DIR)/test/wifi_mon_test.o: $(CM0)/wifi_mon_test.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(TEST_BIN): $(OBJ_DIR)/test/wifi_mon_test.o $(OBJ_DIR)/cm0/wifi_mon.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread
# The BLE scanner's wire format + report parse: the real ble_scan.o.
BLE_TEST_BIN := build/ble_scan_test
$(OBJ_DIR)/test/ble_scan_test.o: $(CM0)/ble_scan_test.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BLE_TEST_BIN): $(OBJ_DIR)/test/ble_scan_test.o $(OBJ_DIR)/cm0/ble_scan.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread
# The battery lamp's status rule.
BATT_TEST_BIN := build/battery_test
$(OBJ_DIR)/test/battery_test.o: $(CM0)/battery_test.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BATT_TEST_BIN): $(OBJ_DIR)/test/battery_test.o $(OBJ_DIR)/cm0/battery.o
	$(CXX) $(CXXFLAGS) -o $@ $^
# Preferences on disk: the sys_shim Preferences.h + the real prefs_store.o.
PREFS_TEST_BIN := build/prefs_test
$(OBJ_DIR)/test/prefs_test.o: $(CM0)/prefs_test.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(PREFS_TEST_BIN): $(OBJ_DIR)/test/prefs_test.o $(OBJ_DIR)/cm0/prefs_store.o $(OBJ_DIR)/cm0/user_fs.o
	$(CXX) $(CXXFLAGS) -o $@ $^
# The SD library over a folder: the real sd_card.o.
SD_TEST_BIN := build/sd_test
$(OBJ_DIR)/test/sd_test.o: $(CM0)/sd_test.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(SD_TEST_BIN): $(OBJ_DIR)/test/sd_test.o $(OBJ_DIR)/cm0/sd_card.o $(OBJ_DIR)/cm0/user_fs.o
	$(CXX) $(CXXFLAGS) -o $@ $^
# The exit modal over a frame: geometry, the progress bar, bounds.
OVERLAY_TEST_BIN := build/overlay_test
$(OBJ_DIR)/test/overlay_test.o: $(CM0)/overlay_test.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OVERLAY_TEST_BIN): $(OBJ_DIR)/test/overlay_test.o $(OBJ_DIR)/cm0/overlay.o
	$(CXX) $(CXXFLAGS) -o $@ $^
# Auto-dim on a fake backlight directory.
DIM_TEST_BIN := build/dimmer_test
$(OBJ_DIR)/test/dimmer_test.o: $(CM0)/dimmer_test.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(DIM_TEST_BIN): $(OBJ_DIR)/test/dimmer_test.o $(OBJ_DIR)/cm0/dimmer.o
	$(CXX) $(CXXFLAGS) -o $@ $^
# Shake and tap over IIO on a fake bmi270 directory and a fake backlight;
# shake-sync-check keeps its copies of the firmware's detector verbatim.
# The keyboard's characters (keymap.h), header-only.
KEYMAP_TEST_BIN := build/keymap_test
$(OBJ_DIR)/test/keymap_test.o: $(CM0)/keymap_test.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(KEYMAP_TEST_BIN): $(OBJ_DIR)/test/keymap_test.o
	$(CXX) $(CXXFLAGS) -o $@ $^
SHAKE_TEST_BIN := build/shake_test
$(OBJ_DIR)/test/shake_test.o: $(CM0)/shake_test.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(SHAKE_TEST_BIN): $(OBJ_DIR)/test/shake_test.o $(OBJ_DIR)/cm0/shake_iio.o $(OBJ_DIR)/cm0/dimmer.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread
shake-sync-check:
	@sh scripts/shake_sync_check.sh $(SQUACHWATCH)
# The speaker: the real firmware sound.o (unmodified) over i2s_alsa.o, on
# the null PCM -- no card needed; --loopback is the device's real card.
AUDIO_TEST_BIN := build/audio_test
$(OBJ_DIR)/test/audio_test.o: $(CM0)/audio_test.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(AUDIO_TEST_BIN): $(OBJ_DIR)/test/audio_test.o $(OBJ_DIR)/cm0/i2s_alsa.o \
                   $(OBJ_DIR)/ui/sound.o $(OBJ_DIR)/ui/settings.o \
                   $(OBJ_DIR)/ui/clock.o \
                   $(OBJ_DIR)/cm0/audio_test_stubs.o \
                   $(OBJ_DIR)/ui/lora_myname.o \
                   $(OBJ_DIR)/cm0/prefs_store.o \
                   $(OBJ_DIR)/cm0/user_fs.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread -lasound
# The shim's sound.h is a verbatim copy of the firmware's enum and API
# declarations: if the firmware's Id list drifts (a new sound), the copy
# is stale and the build must say so.
sound-enum-check:
	@mkdir -p $(OBJ_DIR)/check
	@awk '/enum class Id/,/COUNT/' $(INC)/sound.h > $(OBJ_DIR)/check/sound.enum.fw
	@awk '/enum class Id/,/COUNT/' $(CM0)/sys_shim/sound.h > $(OBJ_DIR)/check/sound.enum.shim
	@if diff -u $(OBJ_DIR)/check/sound.enum.fw $(OBJ_DIR)/check/sound.enum.shim >/dev/null; then \
	   echo "sound-enum-check: ok"; \
	 else echo "sound-enum-check: FAIL -- the shim's Sound::Id drifted from the firmware's"; \
	   diff -u $(OBJ_DIR)/check/sound.enum.fw $(OBJ_DIR)/check/sound.enum.shim | head -20; exit 1; fi
test: $(TEST_BIN) $(BLE_TEST_BIN) $(BATT_TEST_BIN) $(PREFS_TEST_BIN) $(SD_TEST_BIN) $(OVERLAY_TEST_BIN) $(DIM_TEST_BIN) $(SHAKE_TEST_BIN) $(KEYMAP_TEST_BIN) $(AUDIO_TEST_BIN) sdlog-slot-check sound-odr-check sound-enum-check shake-sync-check
	./$(TEST_BIN)
	./$(BLE_TEST_BIN)
	./$(BATT_TEST_BIN)
	./$(PREFS_TEST_BIN)
	./$(SD_TEST_BIN)
	./$(OVERLAY_TEST_BIN)
	./$(DIM_TEST_BIN)
	./$(SHAKE_TEST_BIN)
	./$(KEYMAP_TEST_BIN)
	./$(AUDIO_TEST_BIN)

# Non-root runs: the two capabilities the sniffer needs, on the binary.
# wifi_mon raises them into the ambient set so its iw/ip children get them
# too -- which is safe only because those children are fixed root-owned
# tools exec'd without a shell, with validated argv and a fixed env, and
# SQUACH_WIFI_IF is ignored when privileged (see wifi_mon.cpp, runTool).
# Whoever can run a CAP_NET_RAW binary can sniff, so the binary is made
# owner/group-only (0750) first. Re-run after every rebuild.
caps: $(BIN)
	chmod 0750 $(BIN)
	sudo setcap cap_net_admin,cap_net_raw+ep $(BIN)

clean:
	rm -rf $(OBJ_DIR) $(BIN)

.PHONY: all clean test caps sdlog-slot-check sound-odr-check sound-enum-check shake-sync-check deploy deb
