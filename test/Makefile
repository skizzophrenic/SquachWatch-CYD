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

TESTS := uifit_test lora_survey_test lora_msgs_test lora_json_test lora_feed_test lora_airtime_test lora_ident_test lora_enrich_test lora_channels_test lora_nodes_test lora_crypto_test lora_meshtastic_test lora_meshcore_test lora_lorawan_test lora_ham_test remote_id_test ibeacon_test signatures_test emitter_roundtrip_test ignore_list_test hacker_test pwnagotchi_test shim_fidelity_test squachmesh_test qwerty_test meshmsg_test device_info_test security_test emote_script_test blackbox_test bingo_test hunt_test bgretire_test autoquiet_test framepush_test touchfit_test logindex_test dex_test regulars_test spam_test

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
# The emote scripts' tables and the setup byte's arithmetic.
emote_script_test_SRCS       := $(SRC)/emote_script.cpp
emote_script_test_FLAGS      := -DSQUACH_MESH=1
# The pages against the signature tables and the emulator's profiles.
device_info_test_SRCS        := $(SRC)/device_info.cpp $(SRC)/signatures.cpp
# The lock's arithmetic, through the sim's Preferences shim.
security_test_SRCS           := $(SRC)/security.cpp $(SRC)/ignore_list.cpp
# The rings, on the emulator's RAM flash. The two clock calls it makes are
# stubbed in the test.
blackbox_test_SRCS           := $(SRC)/blackbox.cpp
blackbox_test_FLAGS          := -DBLACKBOX_TEST
# One LOG row per device, over the same RAM flash.
logindex_test_SRCS           := $(SRC)/blackbox.cpp $(SRC)/log_index.cpp
# The DEX's cards and the record, through the Preferences shim; the clock is stubbed.
dex_test_SRCS                := $(SRC)/dex.cpp $(SIM)/detection_sim.cpp $(SRC)/remote_id.cpp
# The neighbours table; the clock is stubbed.
regulars_test_SRCS           := $(SRC)/regulars.cpp
# The card: winnable, and it keeps what it earned. The clock is stubbed in
# the test; the engine comes along for its lifetime counts.
bingo_test_SRCS              := $(SRC)/bingo.cpp $(SIM)/detection_sim.cpp $(SRC)/remote_id.cpp
bingo_test_FLAGS             := -DSQUACH_MESH=1
# An unfinished egg hunt, through the settings store. Theme and Clock are
# stubbed in the test; settings.cpp only calls a handful of each.
autoquiet_test_SRCS       := $(SIM)/detection_sim.cpp $(SRC)/remote_id.cpp
bgretire_test_SRCS := $(SRC)/settings.cpp
hunt_test_SRCS               := $(SRC)/settings.cpp
# Header-only, both: the colour arithmetic the overlapped frame push
# builds its table from is inline in frame_push.h, and the shim under
# test IS sim/TFT_eSPI.h.
framepush_test_SRCS          :=
shim_fidelity_test_SRCS      :=
# The touch mapping is plain arithmetic in touch_fit.h.
touchfit_test_SRCS           :=
# The flood rules are plain arithmetic in spam_watch.h.
spam_test_SRCS               :=
# Fitting a name into a cell is arithmetic over a string, inline in ui_fit.h.
uifit_test_SRCS              :=
# The LoRa decoders' AES, SHA-256, CMAC, HMAC and CRCs, against published vectors.
lora_crypto_test_SRCS        := $(SRC)/lora_crypto.cpp
# Meshtastic: the header, the channel hashes, the cipher round trip, the payloads.
lora_meshtastic_test_SRCS    := $(SRC)/lora_meshtastic.cpp $(SRC)/lora_crypto.cpp
# MeshCore: the frame, adverts, the Public and hashtag channels' seal, traces.
lora_meshcore_test_SRCS      := $(SRC)/lora_meshcore.cpp $(SRC)/lora_crypto.cpp
# LoRaWAN: the envelope, the registry, MAC commands, the MIC and cipher on a published vector, the beacon.
lora_lorawan_test_SRCS       := $(SRC)/lora_lorawan.cpp $(SRC)/lora_crypto.cpp
# LoRa APRS, MeshCom and FANET: plaintext, so every field is a coordinate that must land where it says.
lora_ham_test_SRCS           := $(SRC)/lora_aprs.cpp $(SRC)/lora_fanet.cpp $(SRC)/lora_crypto.cpp
# The channel list across a reboot, through the settings store and the
# Preferences shim's real file. Theme and Clock are stubbed in the test, the
# way hunt_test does it -- settings.cpp only calls a handful of each.
lora_channels_test_SRCS      := $(SRC)/lora_channels.cpp $(SRC)/lora_meshcore.cpp $(SRC)/lora_meshtastic.cpp $(SRC)/lora_crypto.cpp $(SRC)/settings.cpp
# Time on air and the duty ceilings: the profile table's numbers, against the
# reference values in docs/LORA.md.
lora_airtime_test_SRCS       := $(SRC)/lora_pkt.cpp $(SRC)/lora_profiles.cpp $(SRC)/lora_crypto.cpp
# The offline half of node enrichment: grid squares, the DXCC prefix table and
# callsign extraction. Pure arithmetic and string work over the node row, which
# is the whole reason it is a separate file from the queue that uses it.
lora_ident_test_SRCS         := $(SRC)/lora_ident.cpp
# The queue, the cache, the request log and both response parsers, with a
# canned transport in place of the socket. The transport is the only part of
# that file a desktop cannot reach, which is why it is the only part behind a
# pointer.
lora_enrich_test_SRCS        := $(SRC)/lora_enrich.cpp $(SRC)/lora_json.cpp
# The scanner that implements the allow-list for both online sources. Its own
# test, because a fix to the escape handling has to be checkable without going
# through a response parser to reach it.
lora_json_test_SRCS          := $(SRC)/lora_json.cpp
# The MeshCore adverts feed: the parser, the allow-list, the prefix a node row
# pins down, and the ambiguous match that is the whole reason it has three
# answers. Enrich comes along for the request log and the canned transport.
lora_feed_test_SRCS          := $(SRC)/lora_feed.cpp $(SRC)/lora_enrich.cpp $(SRC)/lora_json.cpp
# The node table, fed by every decoder. The survey and the message ring come
# along because heard() feeds the survey and two of the decoders feed the ring:
# that is the point -- the DIRECT/VIA rule is decided in exactly one place and
# the survey is downstream of it, not a second copy of it.
lora_nodes_test_SRCS         := $(SRC)/lora_nodes.cpp $(SRC)/lora_classify.cpp $(SRC)/lora_pkt.cpp $(SRC)/lora_profiles.cpp $(SRC)/lora_meshtastic.cpp $(SRC)/lora_meshcore.cpp $(SRC)/lora_lorawan.cpp $(SRC)/lora_aprs.cpp $(SRC)/lora_fanet.cpp $(SRC)/lora_crypto.cpp $(SRC)/lora_survey.cpp $(SRC)/lora_msgs.cpp
# The antenna survey's arithmetic on its own: the median, the paired delta, the
# sign test and every case whose job is to say "this is not a measurement". No
# decoder and no radio -- noteDirect() takes the two numbers a reading is, which
# is the whole of what lora_nodes.cpp hands it.
lora_survey_test_SRCS        := $(SRC)/lora_survey.cpp
# The message ring: newest-first indexing over a wrap, the per-channel filter,
# and the fixed fields a long line must not run past.
lora_msgs_test_SRCS          := $(SRC)/lora_msgs.cpp

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
