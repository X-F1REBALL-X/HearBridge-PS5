/* Developed by X-F1REBALL-X. HFP AG battery: RFCOMM framing + AT (host test). */
#include "hfp.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

/* Frames the AG sent, in order. */
static unsigned char out[64][200];
static int outn[64], nout;
static char logbuf[1024];

static void cap(void *ud, const unsigned char *p, int n)
{
    (void)ud;
    if (nout < 64 && n <= 200) { memcpy(out[nout], p, (size_t)n); outn[nout++] = n; }
}
static void lg(const char *m) { strncat(logbuf, m, sizeof logbuf - strlen(logbuf) - 2); strcat(logbuf, "\n"); }

/* Headset (initiator) frames: commands C/R=1. */
static void hs(hfp_state *h, int dlci, int type, int pf, int credits, const void *info, int len)
{
    unsigned char f[200];
    int n = rfc_build(f, (int)sizeof f, dlci, 1, type, pf, credits, info, len);
    hfp_input(h, f, n);
}
static void hs_at(hfp_state *h, const char *s, int credits)
{
    hs(h, 2, RFC_UIH, credits >= 0, credits, s, (int)strlen(s));
}

/* All AT text the AG sent on DLCI 2 since frame `from`. */
static int at_text(hfp_state *h, int from, char *t, int max)
{
    int i, n = 0;
    t[0] = 0;
    for (i = from; i < nout; i++) {
        rfc_frame f;
        if (rfc_parse(out[i], outn[i], h->cfc, &f) || f.dlci != 2 || f.type != RFC_UIH) continue;
        if (n + f.len < max) { memcpy(t + n, f.info, (size_t)f.len); n += f.len; t[n] = 0; }
    }
    return n;
}

int main(void)
{
    hfp_state h;
    rfc_frame f;
    unsigned char b[64];
    char t[1024], r[400];
    int n, k;

    /* FCS / framing against the TS 07.10 / RFCOMM spec example frames. */
    n = rfc_build(b, sizeof b, 0, 1, RFC_SABM, 1, -1, NULL, 0);
    CHECK(n == 4 && b[0] == 0x03 && b[1] == 0x3F && b[2] == 0x01 && b[3] == 0x1C, "rfcomm: SABM on DLCI 0 is 03 3f 01 1c");
    n = rfc_build(b, sizeof b, 0, 1, RFC_UA, 1, -1, NULL, 0);
    CHECK(n == 4 && b[3] == 0xD7, "rfcomm: UA on DLCI 0 FCS d7");
    CHECK(rfc_parse(b, n, 0, &f) == 0 && f.type == RFC_UA && f.pf && f.dlci == 0 && f.len == 0, "rfcomm: parse UA");
    b[3] ^= 1;
    CHECK(rfc_parse(b, n, 0, &f) != 0, "rfcomm: bad FCS refused");
    {
        static unsigned char big[300], fr[320];
        memset(big, 'x', sizeof big);
        n = rfc_build(fr, sizeof fr, 2, 0, RFC_UIH, 1, 5, big, 200);
        CHECK(n == 2 + 2 + 1 + 200 + 1 && rfc_parse(fr, n, 1, &f) == 0 && f.len == 200 && f.credits == 5 &&
              f.info[0] == 'x', "rfcomm: 2-byte length + credit byte round trip");
        CHECK(rfc_parse(fr, n - 1, 1, &f) != 0, "rfcomm: short frame refused");
        CHECK(rfc_fcs(fr, 2) == fr[n - 1], "rfcomm: UIH FCS covers address + control only");
    }

    /* Session: SABM 0, PN with credit flow, SABM on channel 1, SLC. */
    hfp_init(&h, cap, NULL, lg);
    hs(&h, 0, RFC_SABM, 1, -1, NULL, 0);
    CHECK(nout == 1 && rfc_parse(out[0], outn[0], 0, &f) == 0 && f.type == RFC_UA && f.dlci == 0 && f.cr == 1,
          "session: mux opened (UA, responder C/R)");
    {
        unsigned char pn[10] = { 0x83, 0x11, 2, 0xF0, 7, 0, 0x7F, 0x03, 0, 3 };
        nout = 0;
        hs(&h, 0, RFC_UIH, 0, -1, pn, 10);
        CHECK(nout == 1 && rfc_parse(out[0], outn[0], 0, &f) == 0 && f.len == 10 && f.info[0] == 0x81 &&
              f.info[3] == 0xE0 && f.info[9] == 7 && (f.info[6] | f.info[7] << 8) == 127,
              "session: PN answered with credit flow (0xE), 7 credits, frame size 127");
        CHECK(h.cfc && h.tx_credits == 3, "session: headset gave us 3 credits");
    }
    nout = 0;
    hs(&h, 2, RFC_SABM, 1, -1, NULL, 0);
    CHECK(nout == 2 && rfc_parse(out[0], outn[0], 1, &f) == 0 && f.type == RFC_UA && f.dlci == 2, "session: DLCI 2 opened");
    CHECK(rfc_parse(out[1], outn[1], 1, &f) == 0 && f.dlci == 0 && f.info[0] == 0xE3 && f.info[2] == 0x0B,
          "session: our MSC command for DLCI 2");
    nout = 0;
    hs(&h, 4, RFC_SABM, 1, -1, NULL, 0);
    CHECK(nout == 1 && rfc_parse(out[0], outn[0], 1, &f) == 0 && f.type == RFC_DM, "session: other channel refused (DM)");
    {
        unsigned char msc[4] = { 0xE3, 0x05, 0x0B, 0x8D };
        nout = 0;
        hs(&h, 0, RFC_UIH, 0, -1, msc, 4);
        CHECK(nout == 1 && rfc_parse(out[0], outn[0], 1, &f) == 0 && f.info[0] == 0xE1, "session: headset MSC answered");
    }

    nout = 0;
    hs_at(&h, "AT+BRSF=959\r", 4);
    at_text(&h, 0, t, sizeof t);
    CHECK(!strcmp(t, "\r\n+BRSF: 1024\r\n\r\nOK\r\n") && h.hf_features == 959, "at: BRSF -> HF indicators only");
    CHECK(h.tx_credits == 3 + 4 - 1, "credits: added from the headset, one spent");
    nout = 0;
    hs_at(&h, "AT+CIND=?\r", -1);
    at_text(&h, 0, t, sizeof t);
    CHECK(strstr(t, "\"battchg\",(0-5)") && strstr(t, "OK"), "at: CIND=? lists indicators");
    nout = 0;
    hs_at(&h, "AT+CIND?\rAT+CMER=3,0,0,1\r", -1);
    at_text(&h, 0, t, sizeof t);
    CHECK(strstr(t, "+CIND: 1,0,0,0,5,0,5") && h.slc, "at: CIND? + CMER, SLC up");
    CHECK(h.tx_credits >= 0, "credits: never negative");

    /* Out of credits: replies wait, then go when credits arrive. */
    h.tx_credits = 0;
    nout = 0;
    hs_at(&h, "AT+BIND=1,2\r", -1);
    CHECK(at_text(&h, 0, t, sizeof t) == 0 && h.outq_n > 0 && h.bind_batt, "credits: reply queued without credits");
    nout = 0;
    hs(&h, 2, RFC_UIH, 1, 2, NULL, 0);
    at_text(&h, 0, t, sizeof t);
    CHECK(!strcmp(t, "\r\nOK\r\n") && h.outq_n == 0, "credits: queued reply sent on new credits");
    h.tx_credits = 20;
    CHECK(h.rx_credits > 0 && h.rx_credits <= 7, "credits: headset always has credits from us");

    hfp_at(&h, "AT+BIND=?", r, sizeof r);
    CHECK(!strcmp(r, "\r\n+BIND: (2)\r\n\r\nOK\r\n"), "at: BIND=? battery indicator");
    hfp_at(&h, "AT+BIND?", r, sizeof r);
    CHECK(!strcmp(r, "\r\n+BIND: 2,1\r\n\r\nOK\r\n"), "at: BIND? battery enabled");

    /* Battery. */
    CHECK(h.battery == -1, "battery: unknown at first");
    hs_at(&h, "AT+BIEV=2,85\r", -1);
    CHECK(h.battery == 85 && h.battery_src == 1, "battery: BIEV=2,85 -> 85%");
    k = h.battery_seq;
    hs_at(&h, "AT+BIEV=1,1\r", -1);
    CHECK(h.battery == 85 && h.battery_seq == k, "battery: other HF indicator ignored");
    hs_at(&h, "AT+BIEV=2,140\r", -1);
    CHECK(h.battery == 100, "battery: clamped to 100");
    nout = 0;
    hs_at(&h, "AT+XAPL=054C-0E0E-0100,10\r", -1);
    at_text(&h, 0, t, sizeof t);
    CHECK(!strcmp(t, "\r\n+XAPL=iPhone,2\r\n\r\nOK\r\n"), "at: XAPL answered (battery reporting)");
    hs_at(&h, "AT+IPHONEACCEV=2,1,6,2,0\r", -1);
    CHECK(h.battery == 100 && h.battery_src == 1, "battery: exact HF indicator kept over a 10 % step Apple report");
    h.battery_src = 0;     /* a headset that only reports the Apple way */
    hs_at(&h, "AT+IPHONEACCEV=2,1,6,2,0\r", -1);
    CHECK(h.battery == 70 && h.battery_src == 2, "battery: IPHONEACCEV 6 -> 70%");
    hs_at(&h, "at+iphoneaccev=1,1,9\r", -1);
    CHECK(h.battery == 100, "battery: lower case, 9 -> 100%");
    CHECK(strstr(logbuf, "hfp: headset battery 70%") != NULL, "log: battery line");

    /* No calls, no audio. */
    hfp_at(&h, "ATD123;", r, sizeof r);
    CHECK(strstr(r, "ERROR") != NULL, "at: dial refused");
    hfp_at(&h, "ATA", r, sizeof r);
    CHECK(strstr(r, "ERROR") != NULL, "at: answer refused");
    hfp_at(&h, "AT+BCC", r, sizeof r);
    CHECK(strstr(r, "ERROR") != NULL, "at: codec connection refused");
    hfp_at(&h, "AT+NREC=0", r, sizeof r);
    CHECK(!strcmp(r, "\r\nOK\r\n"), "at: anything else OK");
    hfp_at(&h, "AT+VGS=9", r, sizeof r);
    CHECK(!strcmp(r, "\r\nOK\r\n"), "at: volume OK");

    /* Garbage and a long line do not break it. */
    {
        unsigned char junk[3] = { 0xFF, 0x01, 0x00 };
        hfp_input(&h, junk, 3);
        CHECK(h.dlci_up, "rfcomm: garbage ignored");
        memset(t, 'A', 600); t[600] = 0;
        hs_at(&h, t, -1);
        CHECK(h.line_n < (int)sizeof h.line, "at: long line capped");
        hs_at(&h, "\r", -1);
    }

    /* DISC keeps the last battery value. */
    hs(&h, 2, RFC_DISC, 1, -1, NULL, 0);
    CHECK(!h.dlci_up && h.mux_up && h.battery == 100, "session: DLCI closed, battery kept");
    hs(&h, 0, RFC_DISC, 1, -1, NULL, 0);
    CHECK(!h.mux_up && h.battery == 100, "session: mux closed, battery kept");

    /* Our clean close: DISC on the DLC, UA, DISC on DLCI 0, UA. */
    {
        hfp_state g;
        hfp_init(&g, cap, NULL, lg);
        CHECK(hfp_close(&g) == 0 && hfp_closed(&g), "close: nothing open, nothing sent");
        hs(&g, 0, RFC_SABM, 1, -1, NULL, 0);
        hs(&g, 2, RFC_SABM, 1, -1, NULL, 0);
        nout = 0;
        CHECK(hfp_close(&g) == 1 && nout == 1 && rfc_parse(out[0], outn[0], 0, &f) == 0 && f.type == RFC_DISC &&
              f.dlci == 2 && f.cr == 0 && f.pf, "close: DISC on the hands-free DLC first (our command C/R 0)");
        nout = 0;
        { unsigned char ua[8]; int m = rfc_build(ua, 8, 2, 0, RFC_UA, 1, -1, NULL, 0); hfp_input(&g, ua, m); }
        CHECK(nout == 1 && rfc_parse(out[0], outn[0], 0, &f) == 0 && f.type == RFC_DISC && f.dlci == 0 && !g.dlci_up,
              "close: its UA, then DISC on DLCI 0");
        CHECK(!hfp_closed(&g), "close: not done before the last UA");
        { unsigned char ua[8]; int m = rfc_build(ua, 8, 0, 0, RFC_UA, 1, -1, NULL, 0); hfp_input(&g, ua, m); }
        CHECK(hfp_closed(&g) && g.closing == 0, "close: done after the UA on DLCI 0");
        hs(&g, 0, RFC_SABM, 1, -1, NULL, 0);
        nout = 0;
        CHECK(hfp_close(&g) == 1 && rfc_parse(out[0], outn[0], 0, &f) == 0 && f.dlci == 0 && f.type == RFC_DISC,
              "close: only the mux open: DISC on DLCI 0 at once");
    }

    printf("%s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails != 0;
}
