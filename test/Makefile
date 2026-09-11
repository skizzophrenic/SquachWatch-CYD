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

TESTS := remote_id_test ibeacon_test signatures_test emitter_roundtrip_test ignore_list_test hacker_test pwnagotchi_test shim_fidelity_test squachmesh_test qwerty_test meshmsg_test device_info_test security_test

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
squachmesh_test_SRCS         := $(SRC)/squachmesh.cpp
# The codec is #if SQUACH_MESH so it stays out of every shipping board.
# The test is the one place that wants it compiled regardless of build env.
squachmesh_test_FLAGS        := -DSQUACH_MESH=1
qwerty_test_SRCS             := $(SRC)/qwerty.cpp
qwerty_test_FLAGS            := -DSQUACH_MESH=1
# The protocol without the cipher -- see the top of meshmsg_test.cpp.
meshmsg_test_SRCS            := $(SRC)/meshmsg.cpp $(SRC)/meshwords.cpp
meshmsg_test_FLAGS           := -DSQUACH_MESH=1
# The pages against the signature tables and the emulator's profiles.
device_info_test_SRCS        := $(SRC)/device_info.cpp $(SRC)/signatures.cpp
# The lock's arithmetic, through the sim's Preferences shim.
security_test_SRCS           := $(SRC)/security.cpp $(SRC)/ignore_list.cpp
# Header-only: the shim under test IS sim/TFT_eSPI.h.
shim_fidelity_test_SRCS      :=

.PHONY: all clean
all: $(addprefix run-,$(TESTS))

$(OUT):
	@mkdir -p $(OUT)

define TEST_rule
$(OUT)/$(1): $(1).cpp $$($(1)_SRCS) | $(OUT)
	@$$(CXX) $$(CXXFLAGS) $$($(1)_FLAGS) -o $$@ $$< $$($(1)_SRCS)

.PHONY: run-$(1)
run-$(1): $(OUT)/$(1)
	@./$(OUT)/$(1)
endef

$(foreach t,$(TESTS),$(eval $(call TEST_rule,$(t))))

clean:
	@rm -rf $(OUT)
