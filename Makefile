# SquachWatch-CYD PC emulator.
#
# Builds SquachWatch-CYD's real UI/rendering sources natively against
# the shims in this directory (Arduino.h, TFT_eSPI.h, Preferences.h), so
# layout work can be previewed without flashing hardware.
#
# This lives outside the firmware repo on purpose: it compiles that
# repo's sources but adds nothing to the firmware build.
#
#   make            # build ./squachsim and ./squachsim-live
#   make shots      # render one PNG per screen into out/
#
# Deliberately excluded from the source list: main.cpp (ESP32 setup/loop
# and hardware init), detection.cpp and sd_log.cpp (WiFi/BLE/SD -- see
# detection_sim.cpp for the stand-in), and the touch calibration code.

SIM_DIR  := .
# Path to a SquachWatch-CYD checkout. Defaults to a sibling directory --
# clone the two next to each other -- and can point anywhere:
#   make SQUACHWATCH=/path/to/SquachWatch-CYD
# Nothing is vendored from it, so the emulator always renders whatever
# branch that checkout currently has.
SQUACHWATCH ?= ../SquachWatch-CYD
SRC      := $(SQUACHWATCH)/src
INC      := $(SQUACHWATCH)/include

CXX      ?= g++
# The sim dir comes first on the include path on purpose: that's what
# makes <TFT_eSPI.h>, <Arduino.h> and <Preferences.h> resolve to the
# shims here rather than to an ESP32 core that isn't installed.
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wno-unused-parameter \
            -I$(SIM_DIR) -I$(INC)

UI_SRCS  := $(SRC)/theme.cpp \
            $(SRC)/squachy.cpp \
            $(SRC)/settings.cpp \
            $(SRC)/signatures.cpp \
            $(SRC)/detection_info.cpp \
            $(SRC)/idle_events.cpp \
            $(SRC)/ui_clear.cpp \
            $(SRC)/ui_log.cpp \
            $(SRC)/ui_alert.cpp \
            $(SRC)/ui_settings.cpp \
            $(SRC)/ui_boot.cpp \
            $(SRC)/ui_colorcheck.cpp \
            $(SRC)/ui_diagnostics.cpp \
            $(SRC)/ui_diary.cpp \
            $(SRC)/ui_hunt.cpp \
            $(SRC)/ui_outfit.cpp \
            $(SRC)/ui_detfilter.cpp \
            $(SRC)/ui_rawscan.cpp \
            $(SRC)/ui_watchalert.cpp

SIM_SRCS := $(SIM_DIR)/main_sim.cpp $(SIM_DIR)/detection_sim.cpp

# The interactive target additionally compiles the firmware's real
# main.cpp -- its actual setup()/loop(), state machine, gesture handling
# and touch mapping -- plus the two touch modules main.cpp includes.
# detection.cpp/sd_log.cpp stay replaced by detection_sim.cpp (radios).
#
# -include of the board's user setup supplies the TFT_/pin macros
# main.cpp expects; that header has no includes of its own, so it's safe
# to pull in here. No board macro is defined (no CYD35, no AWOK), which
# selects the same plain XPT2046 path the real cyd board takes.
LIVE_SRCS := $(SRC)/main.cpp \
             $(SRC)/cap_touch.cpp \
             $(SRC)/touch_cal.cpp \
             $(SIM_DIR)/main_live.cpp \
             $(SIM_DIR)/detection_sim.cpp
# -include Arduino.h mirrors what the Arduino build system does for
# every translation unit: cap_touch.cpp and friends call pinMode/delay
# without including it themselves and rely on that being implicit.
LIVE_FLAGS := -include $(SIM_DIR)/Arduino.h -include $(INC)/cyd_user_setup.h

# ---- WebAssembly ----------------------------------------------------
# The same sources as squachsim-live, compiled for the browser. The port
# is small only because every platform dependency already lives behind a
# shim header here: no threads, no sockets, no filesystem, and the
# framebuffer was already a plain array. main_wasm.cpp swaps the native
# harness's stdin/stdout protocol for exported functions.
#
# Needs emsdk on PATH:  source ~/emsdk/emsdk_env.sh
WASM_SRCS := $(UI_SRCS) \
             $(SRC)/main.cpp \
             $(SRC)/cap_touch.cpp \
             $(SRC)/touch_cal.cpp \
             $(SIM_DIR)/main_wasm.cpp \
             $(SIM_DIR)/detection_sim.cpp
WASM_DIR  := web
WASM_OUT  := $(WASM_DIR)/squachsim.js
# -Os over -O2: this ships over the wire, and the emulator spends its
# time waiting on requestAnimationFrame rather than on compute.
# ALLOW_MEMORY_GROWTH because the frame sprite is allocated at runtime.
WASM_FLAGS := -std=c++17 -Os -I$(SIM_DIR) -I$(INC) \
              -Wno-unused-parameter \
              -include $(SIM_DIR)/Arduino.h \
              -include $(INC)/cyd_user_setup.h \
              -sALLOW_MEMORY_GROWTH=1 \
              -sMODULARIZE=1 -sEXPORT_NAME=SquachSim \
              -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8 \
              -sENVIRONMENT=web,node \
              --closure 0

BIN      := squachsim
LIVE_BIN := squachsim-live

# Everything is compiled in one shot (no object files), so the only way
# a header edit can trigger a rebuild is to list the headers as
# prerequisites. Both the shims here and the firmware's own headers
# count -- editing include/theme.h and getting a stale binary is exactly
# the kind of thing that sends you chasing a bug that isn't there.
HDRS     := $(wildcard $(SIM_DIR)/*.h) $(wildcard $(INC)/*.h)

all: $(BIN) $(LIVE_BIN)
live: $(LIVE_BIN)
wasm: $(WASM_OUT)

$(WASM_OUT): $(WASM_SRCS) $(HDRS)
	@command -v em++ >/dev/null || { echo "em++ not found -- run: source ~/emsdk/emsdk_env.sh"; exit 1; }
	@mkdir -p $(WASM_DIR)
	em++ $(WASM_FLAGS) -o $@ $(WASM_SRCS)

$(BIN): $(UI_SRCS) $(SIM_SRCS) $(HDRS)
	@test -d $(INC) || { echo "SquachWatch-CYD not found at $(SQUACHWATCH) -- set SQUACHWATCH=/path/to/checkout"; exit 1; }
	$(CXX) $(CXXFLAGS) -o $@ $(UI_SRCS) $(SIM_SRCS) -lm

$(LIVE_BIN): $(UI_SRCS) $(LIVE_SRCS) $(HDRS)
	@test -d $(INC) || { echo "SquachWatch-CYD not found at $(SQUACHWATCH) -- set SQUACHWATCH=/path/to/checkout"; exit 1; }
	$(CXX) $(CXXFLAGS) $(LIVE_FLAGS) -o $@ $(UI_SRCS) $(LIVE_SRCS) -lm

shots: $(BIN)
	@mkdir -p out
	./$(BIN) clear    out/clear.png
	./$(BIN) log      out/log.png
	./$(BIN) alert    out/alert.png
	./$(BIN) settings out/settings.png

clean:
	rm -f $(BIN) $(LIVE_BIN)
	rm -rf out .nvs $(WASM_DIR)/squachsim.js $(WASM_DIR)/squachsim.wasm

.PHONY: all live wasm shots clean
