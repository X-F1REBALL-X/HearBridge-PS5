/* Developed by X-F1REBALL-X. Per-headset settings file. */
#include "hsprefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

int main(int argc, char **argv)
{
    hb_prefs p, q;
    char buf[512], path[160];
    static const unsigned char addr[6] = { 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };
    const char *dir = argc > 1 ? argv[1] : "/tmp/hb_prefs_test";

    hb_prefs_default(&p);
    CHECK(p.codec == HB_CODEC_AUTO && p.latency_ms == HB_LAT_DEFAULT_MS && !p.eq_on,
          "defaults: auto codec, 200 ms, EQ off");
    hb_prefs_parse(&p, "codec=xq\nlatency_ms=90\r\neq=on\neq_db=6,3,0,-2,-30\njunk\nfoo=bar\n");
    CHECK(p.codec == HB_CODEC_SBC_XQ && p.latency_ms == 90 && p.eq_on && p.eq_db[0] == 6 &&
          p.eq_db[3] == -2 && p.eq_db[4] == -HB_EQ_MAX_DB, "parse: values read, out-of-range clamped");
    hb_prefs_parse(&p, "codec=aptx\nlatency_ms=5000\nlatency_ms=x\n");
    CHECK(p.codec == HB_CODEC_SBC_XQ && p.latency_ms == HB_LAT_MAX_MS, "parse: unknown codec ignored, latency clamped");
    CHECK(hb_prefs_format(&p, buf, sizeof buf) > 0 && strstr(buf, "codec=xq\n") && strstr(buf, "eq_db=6,3,0,-2,-12\n"),
          "format: key=value lines");
    hb_prefs_default(&q);
    hb_prefs_parse(&q, buf);
    CHECK(!memcmp(&p, &q, sizeof p), "format -> parse round trip");
    hb_prefs_path("/d", addr, path, sizeof path);
    CHECK(!strcmp(path, "/d/112233445566.txt"), "file name is the address in display order");
    CHECK(hb_prefs_save(dir, addr, &p), "save");
    hb_prefs_default(&q);
    CHECK(hb_prefs_load(dir, addr, &q) && !memcmp(&p, &q, sizeof p), "load what was saved");
    CHECK(hb_codec_from_key("hq") == HB_CODEC_SBC_HQ && hb_codec_from_key("x") < 0 &&
          !strcmp(hb_codec_key(99), "auto"), "codec keys");
    CHECK(p.gain_pct < 0, "no gain line: left unset, an old file is not overwritten");
    hb_prefs_parse(&p, "held=sbc:28\nheld=nope\n");
    CHECK(p.held_codec == HB_CODEC_SBC && p.held_bp == 28, "held codec and bitpool are read");
    CHECK(hb_prefs_format(&p, buf, sizeof buf) > 0 && strstr(buf, "held=sbc:28\n"), "held line is written");
    p.latency_ms = 1000;
    p.codec = HB_CODEC_SBC_XQ;
    hb_prefs_new_headset(&p);
    CHECK(p.latency_ms == HB_LAT_DEFAULT_MS && p.codec == HB_CODEC_SBC_XQ,
          "new headset: buffer is 200 ms, the rest stays");
    p.gain_pct = 250;
    p.gain_user = 1;
    CHECK(hb_prefs_format(&p, buf, sizeof buf) > 0 && strstr(buf, "gain=250\ngain_user=1\n") &&
          strstr(buf, "latency_ms=200\n"), "format: gain the user set and the 200 ms default");

    hb_prefs_default(&q);
    CHECK(!q.night, "night mode off by default");
    hb_prefs_parse(&q, "night=on\n");
    CHECK(q.night && hb_prefs_format(&q, buf, sizeof buf) > 0 && strstr(buf, "night=on\n"), "night mode saved per headset");
    hb_prefs_parse(&q, "night=off\n");
    CHECK(!q.night && hb_prefs_format(&q, buf, sizeof buf) > 0 && !strstr(buf, "night="), "night mode off: no line");

    /* Gain: remembered only when the user set it for this headset. */
    hb_prefs_default(&q);
    CHECK(hb_prefs_gain(&q) == 250 && hb_prefs_hs_volume(&q) == 64,
          "new headset: gain 250, headset volume 64 (50%)");
    hb_prefs_parse(&q, "codec=auto\nlatency_ms=200\neq=off\neq_db=0,0,0,0,0\ngain=500\n");
    CHECK(q.gain_pct == 500 && !q.gain_user && hb_prefs_gain(&q) == 250,
          "old file with an automatic gain=500: starts at 250");
    CHECK(hb_prefs_format(&q, buf, sizeof buf) > 0 && !strstr(buf, "gain="),
          "old automatic gain is dropped on the next save");
    hb_prefs_default(&q);
    hb_prefs_parse(&q, "gain=420\ngain_user=1\n");
    CHECK(hb_prefs_gain(&q) == 420, "gain the user set is used");
    hb_prefs_default(&q);
    hb_prefs_parse(&q, "gain_user=1\n");
    CHECK(hb_prefs_gain(&q) == 250, "gain_user without a value: 250");
    hb_prefs_default(&q);
    q.gain_pct = 480;            /* e.g. copied from another headset, never set here */
    CHECK(hb_prefs_format(&q, buf, sizeof buf) > 0 && !strstr(buf, "gain=") && hb_prefs_gain(&q) == 250,
          "a gain not set by the user is not written");

    /* Headset volume: 50% until the user moves it, then kept. */
    hb_prefs_default(&q);
    CHECK(hb_prefs_format(&q, buf, sizeof buf) > 0 && !strstr(buf, "hs_vol="), "no user volume: not written");
    q.hs_vol = 100;
    CHECK(hb_prefs_format(&q, buf, sizeof buf) > 0 && strstr(buf, "hs_vol=100\n"), "user volume written");
    hb_prefs_default(&p);
    hb_prefs_parse(&p, buf);
    CHECK(hb_prefs_hs_volume(&p) == 100, "user volume read back");
    hb_prefs_parse(&p, "hs_vol=900\n");
    CHECK(p.hs_vol == 127, "volume clamped to 127");
    CHECK(hb_prefs_save(dir, addr, &q), "save with volume");
    hb_prefs_default(&p);
    CHECK(hb_prefs_load(dir, addr, &p) && hb_prefs_hs_volume(&p) == 100 && hb_prefs_gain(&p) == 250,
          "load: volume kept, gain default");
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
