/* Developed by X-F1REBALL-X. Host tests for the codec switch / reconnect
 * steps (src/a2dp/cswitch.c). */
#include "cswitch.h"
#include "hsprefs.h"

#include <stdio.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

int main(void)
{
    hb_cswitch c;
    int i, n;

    hb_cs_begin(&c, HB_CODEC_SBC, 0);
    CHECK(c.step == HB_CS_INPLACE && c.attempt == 0, "switch starts in place, no reconnect yet");
    CHECK(hb_cs_next(&c, 1, 1) == HB_CS_DONE, "in-place switch worked -> done, headset never dropped");

    hb_cs_begin(&c, HB_CODEC_SBC_XQ, 0);
    CHECK(hb_cs_next(&c, 0, 1) == HB_CS_INPLACE_SBC, "XQ refused, link up -> plain SBC in place");
    CHECK(hb_cs_next(&c, 1, 1) == HB_CS_DONE, "...and SBC worked -> done");

    hb_cs_begin(&c, HB_CODEC_AUTO, 1);
    CHECK(c.no_xq == 1 && hb_cs_next(&c, 0, 1) == HB_CS_INPLACE_SBC,
          "auto XQ->SBC fallback path: auto without XQ, then plain SBC");

    hb_cs_begin(&c, HB_CODEC_SBC, 0);
    CHECK(hb_cs_next(&c, 0, 1) == HB_CS_RECONNECT && c.attempt == 1 && c.delay_ms == hb_cs_delay(0),
          "plain SBC refused in place -> reconnect (first pause)");

    hb_cs_begin(&c, HB_CODEC_SBC_HQ, 0);
    CHECK(hb_cs_next(&c, 0, 0) == HB_CS_RECONNECT, "link dropped during the switch -> reconnect, no SBC try on a dead link");
    CHECK(hb_cs_next(&c, 1, 1) == HB_CS_DONE, "reconnect worked -> done");

    hb_cs_begin(&c, HB_CODEC_SBC_XQ, 0);
    hb_cs_next(&c, 0, 1);
    hb_cs_next(&c, 0, 0);                 /* SBC in place failed too */
    for (n = 1; c.step == HB_CS_RECONNECT && n < 20; n++) hb_cs_next(&c, 0, 0);
    CHECK(c.step == HB_CS_GIVEUP && c.attempt == HB_CS_TRIES, "reconnect keeps trying HB_CS_TRIES times, then gives up");
    CHECK(hb_cs_next(&c, 1, 1) == HB_CS_GIVEUP, "given up stays given up (no surprise reconnects)");

    for (i = 1, n = 1; i < HB_CS_TRIES; i++) if (hb_cs_delay(i) <= hb_cs_delay(i - 1)) n = 0;
    CHECK(n && hb_cs_delay(0) >= 1000 && hb_cs_delay(0) <= 1500 &&
          hb_cs_delay(HB_CS_TRIES - 1) <= 3000 &&
          hb_cs_delay(99) == hb_cs_delay(HB_CS_TRIES - 1),
          "pauses stay about a second or two, none of them runs long");

    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
