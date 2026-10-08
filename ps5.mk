# Console payload build (ps5-payload-sdk). Invoked from Makefile as `make ps5`.

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

VERSION := $(shell sed -n 's/^\#define HEARBRIDGE_VERSION "\(.*\)"/\1/p' src/version.h)
ELF   := dist/HearBridge-PS5-$(VERSION).elf
BUILD ?= build/ps5

CFLAGS  := -std=c11 -Wall -Wextra -O2 -Isrc -Isrc/bt -Isrc/a2dp

# One build for every firmware. Keep the DT_NEEDED list and its order the
# same as 1.0.2 (SystemService first): the payload runtime loads these
# modules before main(), and a load failure kills the payload silently.
# Symbols 1.0.2 did not import (sceAppInstUtilAppInstallTitleDir and friends,
# opendir/readdir/closedir) are looked up at run time instead, so a firmware
# without one of them still loads. scripts/check_imports.sh enforces this.
LDLIBS  += -lSceSystemService -lpthread

# 1.0.0: generic A2DP source — saved device or inquiry → SSP pair →
# SDP A2DP Sink → AVDTP (SNK+SBC) → Avcap2 capture → SBC stream.
SRCS := \
	src/util.c src/stop.c src/log.c src/lock.c src/notify.c src/avcap2.c \
	src/diag.c src/creds.c src/sysinfo.c src/hsprefs.c \
	src/bt/hci_usb.c src/bt/acl_track.c src/bt/usb_hci_desc.c src/bt/hci_evasm.c src/bt/hci_cmd.c \
	src/a2dp/a2dp.c src/a2dp/btlink.c src/a2dp/acl_pool.c src/a2dp/sdp_a2dp.c src/a2dp/avdtp.c src/a2dp/avdtp_media.c \
	src/a2dp/avrcp.c src/a2dp/sdp_server.c src/ctl.c src/gain.c src/http.c \
	src/a2dp/headset_ini.c src/a2dp/sbc_enc.c src/a2dp/rate.c src/a2dp/eq.c src/tile.c src/tile_sys.c src/utf8.c src/a2dp/devclass.c src/a2dp/paired.c \
	src/main.c

OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

$(ELF): $(OBJS)
	@mkdir -p dist
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	sh scripts/check_imports.sh $@ scripts/imports-1.0.2.txt || { rm -f $@; exit 1; }

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
