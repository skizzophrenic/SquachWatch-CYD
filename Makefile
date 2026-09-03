# SquachWatch-CYD PC emulator.
#
# Builds SquachWatch-CYD's real UI/rendering sources natively against
# the shims in this directory (Arduino.h, TFT_eSPI.h, Preferences.h), so
# layout work can be previewed without flashing hardware.
#
# This lives outside the firmware repo on purpose: it compiles that
# repo's sources but adds nothing to the firmware build.
#
#   make            # build ./squachsim
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
            $(SRC)/ui_rawscan.cpp \
            $(SRC)/ui_watchalert.cpp

SIM_SRCS := $(SIM_DIR)/main_sim.cpp $(SIM_DIR)/detection_sim.cpp

BIN      := squachsim

all: $(BIN)

$(BIN): $(UI_SRCS) $(SIM_SRCS)
	@test -d $(INC) || { echo "SquachWatch-CYD not found at $(SQUACHWATCH) -- set SQUACHWATCH=/path/to/checkout"; exit 1; }
	$(CXX) $(CXXFLAGS) -o $@ $^ -lm

shots: $(BIN)
	@mkdir -p out
	./$(BIN) clear    out/clear.png
	./$(BIN) log      out/log.png
	./$(BIN) alert    out/alert.png
	./$(BIN) settings out/settings.png

clean:
	rm -f $(BIN)
	rm -rf out

.PHONY: all shots clean
