# HearBridge PS5: Bluetooth headphones (A2DP) on a jailbroken PS5.
#
#   make test                 host tests (cc, ffmpeg, python3 + numpy)
#   make ps5                  builds dist/HearBridge-PS5-<version>.elf
#   make send PS5_HOST=ip     builds and sends it to the console's ELF loader
#
# PS5_PAYLOAD_SDK must point at an unpacked ps5-payload-sdk release
# (https://github.com/ps5-payload-dev/sdk/releases), e.g.
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
# There is no default, so a missing SDK is reported instead of guessed.

PS5_HOST ?= ps5
PS5_PORT ?= 9021
VERSION := $(shell sed -n 's/^\#define HEARBRIDGE_VERSION "\(.*\)"/\1/p' src/version.h)

BUILD := build

.PHONY: all ps5 send clean test test-sbc test-dump test-crypto test-control webpage test-tile test-devices test-rate icon test-acl test-mtu test-track test-pace test-diag test-reinstall test-page test-link test-prefs test-eq test-cswitch test-rejoin test-forgot test-switch test-connreq test-cfg test-chip

all: ps5

ps5:
ifndef PS5_PAYLOAD_SDK
	$(error PS5_PAYLOAD_SDK is undefined)
endif
	$(MAKE) -f ps5.mk PS5_PAYLOAD_SDK=$(PS5_PAYLOAD_SDK)

send: ps5
	$(PS5_PAYLOAD_SDK)/bin/prospero-deploy -h $(PS5_HOST) -p $(PS5_PORT) dist/HearBridge-PS5-$(VERSION).elf

clean:
	rm -rf $(BUILD) dist

# Host test for the SBC encoder (needs cc, ffmpeg, python3+numpy).
test-sbc:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -Isrc -Isrc/a2dp tests/test_sbc_enc.c src/a2dp/sbc_enc.c -lm -o $(BUILD)/host/test_sbc_enc
	$(BUILD)/host/test_sbc_enc $(BUILD)/host/t.sbc $(BUILD)/host/ref.s16 53 8 16 1 0
	ffmpeg -v error -y -f sbc -i $(BUILD)/host/t.sbc -f s16le -ac 2 $(BUILD)/host/dec.s16
	python3 tests/sbc_snr.py $(BUILD)/host/ref.s16 $(BUILD)/host/dec.s16

# Media path: console SBC config + RTP builder -> media_dump.bin -> checker.
test-dump:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -Isrc -Isrc/a2dp -Isrc/bt tests/test_media_dump.c src/a2dp/avdtp_media.c src/a2dp/sbc_enc.c -lm -o $(BUILD)/host/test_media_dump
	$(BUILD)/host/test_media_dump $(BUILD)/host/media_dump.bin 200
	python3 tests/decode_dump $(BUILD)/host/media_dump.bin $(BUILD)/host/media_dump.wav --expect-hz 1000
	$(BUILD)/host/test_media_dump $(BUILD)/host/media_dump_xq.bin 200 xq
	python3 tests/decode_dump $(BUILD)/host/media_dump_xq.bin $(BUILD)/host/media_dump_xq.wav --expect-hz 1000

test-crypto:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -Isrc -Isrc/bt tests/test_bt_crypto.c src/bt/smp_crypto.c src/bt/crc32.c -o $(BUILD)/host/test_bt_crypto
	$(BUILD)/host/test_bt_crypto

# Web page, SDP records, AVRCP absolute volume, gain/limiter.
test-control:
	@mkdir -p $(BUILD)/host
	python3 scripts/gen_webpage.py src/web/index.html $(BUILD)/host/webpage.h src/web/i18n.json
	cmp -s $(BUILD)/host/webpage.h src/webpage.h || (echo "src/webpage.h is stale: make webpage"; false)
	python3 scripts/gen_font.py assets/fonts $(BUILD)/host/font_he.h
	cmp -s $(BUILD)/host/font_he.h src/font_he.h || (echo "src/font_he.h is stale: make webpage"; false)
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -DHB_HTTP_HOST_TEST -Isrc -Isrc/a2dp -Isrc/bt tests/test_control.c src/http.c src/bt/btchip.c src/diag.c src/ctl.c src/gain.c src/a2dp/eq.c src/a2dp/rate.c src/a2dp/avrcp.c src/a2dp/sdp_server.c -lpthread -lm -o $(BUILD)/host/test_control
	$(BUILD)/host/test_control $(BUILD)/host/status.json
	python3 -c "import json;d=json.load(open('$(BUILD)/host/status.json'));print('ok   status JSON parses,', len(d), 'keys')"

webpage:
	python3 scripts/gen_webpage.py
	python3 scripts/gen_font.py

test-tile:
	@mkdir -p $(BUILD)/host
	python3 scripts/gen_icon.py assets/icon0.png $(BUILD)/host/icon_png.h
	cmp -s $(BUILD)/host/icon_png.h src/icon_png.h || (echo "src/icon_png.h is stale: make icon"; false)
	python3 scripts/gen_start.py src/web/start.html $(BUILD)/host/start_html.h
	cmp -s $(BUILD)/host/start_html.h src/start_html.h || (echo "src/start_html.h is stale: make icon"; false)
	cc -std=c11 -Wall -Wextra -O2 -Isrc tests/test_tile.c src/tile.c -o $(BUILD)/host/test_tile
	$(BUILD)/host/test_tile

test-rate:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -Isrc -Isrc/a2dp tests/test_rate.c src/a2dp/rate.c src/a2dp/sbc_enc.c -lm -o $(BUILD)/host/test_rate
	$(BUILD)/host/test_rate $(BUILD)/host/bp.sbc $(BUILD)/host/bp_ref.s16
	ffmpeg -v error -y -f sbc -i $(BUILD)/host/bp.sbc -f s16le -ac 2 $(BUILD)/host/bp_dec.s16
	python3 tests/sbc_snr.py $(BUILD)/host/bp_ref.s16 $(BUILD)/host/bp_dec.s16

test-devices:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -Isrc -Isrc/a2dp tests/test_devices.c src/utf8.c src/a2dp/devclass.c src/a2dp/paired.c -o $(BUILD)/host/test_devices
	$(BUILD)/host/test_devices

icon:
	python3 scripts/make_icon.py assets/icon0.png
	python3 scripts/gen_icon.py assets/icon0.png src/icon_png.h
	python3 scripts/gen_start.py src/web/start.html src/start_html.h

test: test-sbc test-dump test-crypto test-control test-tile test-devices test-rate test-acl test-mtu test-track test-pace test-diag test-reinstall test-page test-link test-prefs test-eq test-cswitch test-rejoin test-forgot test-switch test-connreq test-cfg test-chip

test-acl:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -Isrc/a2dp tests/test_acl_pool.c src/a2dp/acl_pool.c -o $(BUILD)/host/test_acl_pool
	$(BUILD)/host/test_acl_pool

test-mtu:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -Isrc -Isrc/a2dp tests/test_mtu.c src/a2dp/rate.c src/a2dp/sbc_enc.c -lm -o $(BUILD)/host/test_mtu
	$(BUILD)/host/test_mtu

test-track:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -Isrc -Isrc/bt tests/test_acl_track.c src/bt/acl_track.c -o $(BUILD)/host/test_acl_track
	$(BUILD)/host/test_acl_track

# The console case of a clean link whose bitpool stayed at 22: real pacer,
# queue cap and adaptive controller over a bursty-credit link model.
test-link:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -Isrc -Isrc/a2dp tests/test_link_bitpool.c src/a2dp/acl_pool.c src/a2dp/rate.c -o $(BUILD)/host/test_link_bitpool
	$(BUILD)/host/test_link_bitpool

test-pace:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -Isrc -Isrc/a2dp tests/test_pace.c src/a2dp/acl_pool.c src/a2dp/rate.c -o $(BUILD)/host/test_pace
	$(BUILD)/host/test_pace

# Equalizer: flat = bit-exact, band gains, headroom/limiter, cost.
test-cswitch:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -Isrc -Isrc/a2dp tests/test_cswitch.c src/a2dp/cswitch.c -o $(BUILD)/host/test_cswitch
	$(BUILD)/host/test_cswitch

test-rejoin:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -Isrc tests/test_rejoin.c src/rejoin.c -o $(BUILD)/host/test_rejoin
	$(BUILD)/host/test_rejoin

test-switch:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -Isrc -Isrc/bt -Isrc/a2dp tests/test_switch_ab.c src/bt/connreq.c src/a2dp/btlink.c src/bt/acl_track.c src/a2dp/acl_pool.c src/a2dp/avrcp.c src/a2dp/sdp_server.c src/bt/hci_cmd.c src/util.c -o $(BUILD)/host/test_switch_ab
	$(BUILD)/host/test_switch_ab

# L2CAP config pacing on a slow (MediaTek) and a quick (Marvell) link.
test-cfg:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -Isrc -Isrc/bt -Isrc/a2dp tests/test_cfg_mtk.c src/bt/connreq.c src/a2dp/btlink.c src/bt/acl_track.c src/a2dp/acl_pool.c src/a2dp/avrcp.c src/a2dp/sdp_server.c src/bt/hci_cmd.c src/util.c -o $(BUILD)/host/test_cfg_mtk
	$(BUILD)/host/test_cfg_mtk

# Chip profile, scan pause, ACL pipe and the AVDTP PSM fallback.
test-chip:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -Isrc -Isrc/bt -Isrc/a2dp tests/test_chip_profile.c src/bt/btchip.c src/bt/hci_cmd.c src/bt/usb_hci_desc.c src/util.c -o $(BUILD)/host/test_chip_profile
	$(BUILD)/host/test_chip_profile

test-connreq:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -Isrc/bt tests/test_connreq.c src/bt/connreq.c -o $(BUILD)/host/test_connreq
	$(BUILD)/host/test_connreq

test-forgot:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -Isrc tests/test_forgot.c src/forgot.c -o $(BUILD)/host/test_forgot
	$(BUILD)/host/test_forgot

test-eq:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -Isrc -Isrc/a2dp tests/test_eq.c src/a2dp/eq.c src/gain.c -lm -lpthread -o $(BUILD)/host/test_eq
	$(BUILD)/host/test_eq

# Per-headset settings file (codec, latency, equalizer).
test-prefs:
	@mkdir -p $(BUILD)/host/prefs
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -Isrc tests/test_prefs.c src/hsprefs.c -o $(BUILD)/host/test_prefs
	$(BUILD)/host/test_prefs $(BUILD)/host/prefs

# Diagnostics report (/api/diag, diag.txt), USB descriptor summary.
test-diag:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -Isrc -Isrc/bt tests/test_diag.c src/diag.c src/bt/usb_hci_desc.c -lpthread -o $(BUILD)/host/test_diag
	$(BUILD)/host/test_diag

# Re-install: icon registered on every run (fake installer), lock cases.
test-reinstall:
	@mkdir -p $(BUILD)/host
	cc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -DHB_LOCK_HOST_TEST -Isrc tests/test_reinstall.c src/tile.c src/lock.c src/log.c src/util.c -o $(BUILD)/host/test_reinstall
	$(BUILD)/host/test_reinstall

# Control page script: GET for reads, POST + token for actions (needs node;
# skipped when node is not installed).
test-page:
	@if command -v node >/dev/null 2>&1; then node tests/test_page.js src/web/index.html src/web/i18n.json; \
	else echo "skip test-page: node not installed"; fi
