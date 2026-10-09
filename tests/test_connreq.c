/* Developed by X-F1REBALL-X. Incoming connection requests: every case answered. */
#include "connreq.h"
#include <stdio.h>
static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)
int main(void)
{
    hb_cr_in i = { 0 };
    CHECK(hb_cod_is_av(0x24041c) && hb_cod_is_av(0x240404) && !hb_cod_is_av(0x002508) && !hb_cod_is_av(0x5a020c),
          "CoD: headsets are audio, a DualSense (002508) and a phone are not");
    i.is_av = 0;
    CHECK(hb_connreq_decide(&i) == HB_CR_LEAVE, "unknown pad / phone: left to the system");
    i.is_av = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_REJECT_UNKNOWN, "unknown audio device: rejected 0x0F at once");
    i.forgotten = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_REJECT_UNKNOWN, "forgotten headset: rejected 0x0F");
    i.is_target = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_TAKE, "the device the user pressed Connect on: accepted, even unsaved");
    i = (hb_cr_in){ 0 }; i.is_av = 1; i.saved = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_TAKE, "saved headset, idle: accepted");
    i.busy_other = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_REJECT_BUSY, "saved headset while connecting another: rejected 0x0D");
    i.bg_page = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_TAKE, "saved headset out of its case while we only re-page another in the background: taken");
    i.held = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_REJECT_BUSY, "...unless it was just disconnected by hand");
    i.held = 0; i.bg_page = 0;
    i.busy_other = 0; i.streaming_other = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_SWITCH, "saved headset B calls while A streams: switch to B");
    i.held = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_REJECT_BUSY, "B disconnected by hand: no switch, rejected 0x0D");
    i = (hb_cr_in){ 0 }; i.is_av = 1; i.forgotten = 1; i.streaming_other = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_REJECT_UNKNOWN, "forgotten headset while streaming: no switch");
    i = (hb_cr_in){ 0 }; i.is_av = 1; i.streaming_other = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_REJECT_UNKNOWN, "unsaved headset while streaming: no switch");
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
