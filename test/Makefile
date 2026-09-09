# SquachWatch-CYD — host tests.
#
# The firmware's decoders are pure arithmetic over byte buffers with no
# radio behind them, which means they can be tested on a desktop against
# hand-built packets. That is worth doing precisely because the
# alternative is standing in a field with a drone: an endianness slip in
# a coordinate field produces a plausible-looking wrong answer, not a
# crash, and nothing on the device would ever tell you.
#
#   make          # build and run everything
#   make clean
#
# Include path mirrors sim/Makefile: the sim directory comes first so
# <Arduino.h> and friends resolve to its shims rather than to an ESP32
# core that is not installed on a build machine.

SRC  := ../src
INC  := ../include
SIM  := ../sim
OUT  := out

CXX      ?= g++
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
            -I$(SIM) -I$(INC)

TESTS := remote_id_test ibeacon_test signatures_test emitter_roundtrip_test ignore_list_test hacker_test pwnagotchi_test

# Only the units under test get linked in. Pulling the whole firmware
# would drag in TFT_eSPI, NimBLE and the UI, and a test that needs a
# display driver to check a coordinate is not a test anybody runs.
remote_id_test_SRCS          := $(SRC)/remote_id.cpp
ibeacon_test_SRCS            := $(SRC)/signatures.cpp
signatures_test_SRCS         := $(SRC)/signatures.cpp
emitter_roundtrip_test_SRCS  := $(SRC)/remote_id.cpp
ignore_list_test_SRCS        := $(SRC)/ignore_list.cpp
hacker_test_SRCS             := $(SRC)/signatures.cpp
pwnagotchi_test_SRCS         := $(SRC)/signatures.cpp

.PHONY: all clean
all: $(addprefix run-,$(TESTS))

$(OUT):
	@mkdir -p $(OUT)

define TEST_rule
$(OUT)/$(1): $(1).cpp $$($(1)_SRCS) | $(OUT)
	@$$(CXX) $$(CXXFLAGS) -o $$@ $$< $$($(1)_SRCS)

.PHONY: run-$(1)
run-$(1): $(OUT)/$(1)
	@./$(OUT)/$(1)
endef

$(foreach t,$(TESTS),$(eval $(call TEST_rule,$(t))))

clean:
	@rm -rf $(OUT)
