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
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
