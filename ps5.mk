# Console payload build (ps5-payload-sdk). Invoked from Makefile as `make ps5`.

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

VERSION := $(shell sed -n 's/^\#define HEARBRIDGE_VERSION "\(.*\)"/\1/p' src/version.h)
FLAVOR  := $(shell sed -n 's/^\#define HEARBRIDGE_FLAVOR "\(.*\)"/\1/p' src/version.h)
ELF   := dist/HearBridge-PS5-$(VERSION)$(if $(FLAVOR),-$(FLAVOR)).elf
BUILD ?= build/ps5

CFLAGS  := -std=c11 -Wall -Wextra -O2 -Isrc -Isrc/bt -Isrc/a2dp

# libSceSystemService was linked but never used (dropped in the fw13.60
# build). libSceAppInstUtil stays linked so the module is loaded; its
# optional calls are looked up at run time (tile_sys.c).
LDLIBS  += -lSceAppInstUtil -lpthread

# 1.0.0: generic A2DP source — saved device or inquiry → SSP pair →
# SDP A2DP Sink → AVDTP (SNK+SBC) → Avcap2 capture → SBC stream.
SRCS := \
	src/util.c src/stop.c src/log.c src/lock.c src/notify.c src/avcap2.c \
	src/diag.c src/creds.c src/sysinfo.c \
	src/bt/hci_usb.c src/bt/acl_track.c src/bt/usb_hci_desc.c src/bt/hci_evasm.c src/bt/hci_cmd.c \
	src/a2dp/a2dp.c src/a2dp/btlink.c src/a2dp/acl_pool.c src/a2dp/sdp_a2dp.c src/a2dp/avdtp.c src/a2dp/avdtp_media.c \
	src/a2dp/avrcp.c src/a2dp/sdp_server.c src/ctl.c src/gain.c src/http.c \
	src/a2dp/headset_ini.c src/a2dp/sbc_enc.c src/a2dp/rate.c src/tile.c src/tile_sys.c src/utf8.c src/a2dp/devclass.c src/a2dp/paired.c \
	src/main_a2dp_spike.c

OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

$(ELF): $(OBJS)
	@mkdir -p dist
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
