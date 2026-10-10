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
    /* Xbox Wireless Headset: switched away from, it calls back 560 ms later
     * while we page the new one. A busy rejection made it stop answering
     * pages until power cycled: accept it and close it cleanly instead. */
    i = (hb_cr_in){ 0 }; i.is_av = 1; i.saved = 1; i.busy_other = 1; i.held = 1; i.just_dropped = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_ACCEPT_DROP, "just-left headset calling back while paging another: accept + clean close, not busy");
    i.busy_other = 0; i.streaming_other = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_ACCEPT_DROP, "...also while streaming the new one (no switch back)");
    i.is_target = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_TAKE, "...unless the user picked it again: taken");
    i = (hb_cr_in){ 0 }; i.is_av = 1; i.forgotten = 1; i.just_dropped = 1;
    CHECK(hb_connreq_decide(&i) == HB_CR_REJECT_UNKNOWN, "forgotten (not saved) stays rejected 0x0F");
    {
        hb_dropped d = { { 0 }, 0, 0 };
        static const unsigned char x[6] = { 0xD8, 0xE2, 0xDF, 0xF7, 0xD7, 0x44 }, y[6] = { 0x58, 0x18, 0x62, 0x63, 0x3B, 0x7C };
        CHECK(!hb_dropped_recent(&d, x, 1000, HB_DROPPED_MS), "dropped: nothing noted");
        hb_dropped_note(&d, x, 1000);
        CHECK(hb_dropped_recent(&d, x, 1560, HB_DROPPED_MS), "dropped: callback 560 ms later is recent");
        CHECK(!hb_dropped_recent(&d, y, 1560, HB_DROPPED_MS), "dropped: another headset is not");
        CHECK(!hb_dropped_recent(&d, x, 1000 + HB_DROPPED_MS, HB_DROPPED_MS), "dropped: 10 s later it is not");
        CHECK(hb_dropped_recent(&d, x, 30000, HB_DROPPED_PAGE_MS), "dropped: page window is longer (60 s)");
    }
    CHECK(!hb_drop_is_away(0x08), "drop: supervision timeout (0x08) is link loss, the normal rejoin runs");
    CHECK(hb_drop_is_away(0x13), "drop: remote user (0x13, case / power button) stays idle");
    CHECK(!hb_drop_is_away(0x16) && !hb_drop_is_away(0x22), "drop: other reasons rejoin too");
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
