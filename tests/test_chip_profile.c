/* Developed by X-F1REBALL-X. One build for every chip: which controller
 * gets the MediaTek profile (VID, override file), the system scan pause
 * only on that profile, the ACL pipe pairing and the AVDTP PSM fallback. */
#include "btchip.h"
#include "hci_cmd.h"
#include "sdp_a2dp.h"
#include "usb_hci_desc.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

void log_line(const char *fmt, ...) { va_list ap; va_start(ap, fmt); va_end(ap); (void)fmt; }

/* Fake controller: answers every command with Command Complete. */
static unsigned ops[32]; static unsigned char args0[32]; static int nops;
static unsigned char scan_val = 2;      /* the system's Scan_Enable */
static int read_fails;
static unsigned char evq[8][16]; static int evlen[8], evh, evt;

static int f_cmd(void *s, unsigned op, const void *a, int n)
{
    unsigned char *e = evq[evt % 8];
    (void)s;
    if (nops < 32) { ops[nops] = op; args0[nops] = n > 0 ? ((const unsigned char *)a)[0] : 0; nops++; }
    e[0] = 0x0E; e[2] = 1; e[3] = (unsigned char)op; e[4] = (unsigned char)(op >> 8); e[5] = 0;
    if (op == HB_OP_READ_SCAN_ENABLE) {
        e[1] = 5; e[6] = scan_val; evlen[evt % 8] = 7;
        if (read_fails) e[5] = 0x0C;
    } else {
        if (op == HB_OP_WRITE_SCAN_ENABLE && n > 0) scan_val = ((const unsigned char *)a)[0];
        e[1] = 4; evlen[evt % 8] = 6;
    }
    evt++;
    return 1;
}
static int f_event(void *s, unsigned char *d, int cap)
{
    int n;
    (void)s;
    if (evh == evt) return 0;
    n = evlen[evh % 8] < cap ? evlen[evh % 8] : cap;
    memcpy(d, evq[evh % 8], (size_t)n);
    evh++;
    return n;
}
static int f_none(void *s, unsigned char *d, int cap) { (void)s; (void)d; (void)cap; return 0; }
static int f_pump(void *s, int ms) { (void)s; (void)ms; return evh != evt; }
static int f_send(void *s, const unsigned char *f, int n) { (void)s; (void)f; (void)n; return 1; }
static const hci_ops k_ops = { f_none, f_event, f_pump, f_cmd, f_send, NULL, NULL };
static int dummy;

static void reset(unsigned char sys)
{
    nops = 0; scan_val = sys; read_fails = 0; evh = evt = 0;
}

int main(void)
{
    hci_t h = { &dummy, &k_ops };

    /* profile by VID */
    CHECK(btchip_profile(0x1286, -1) == BTCHIP_PROFILE_DEFAULT, "profile: Marvell/NXP 1286 gets the default profile");
    CHECK(btchip_profile(0x0e8d, -1) == BTCHIP_PROFILE_MEDIATEK, "profile: MediaTek 0e8d gets the mediatek profile");
    CHECK(btchip_profile(0xabcd, -1) == BTCHIP_PROFILE_DEFAULT && btchip_profile(-1, -1) == BTCHIP_PROFILE_DEFAULT,
          "profile: unknown or missing VID keeps the default profile");
    CHECK(btchip_profile(0x1286, BTCHIP_PROFILE_MEDIATEK) == BTCHIP_PROFILE_MEDIATEK &&
          btchip_profile(0x0e8d, BTCHIP_PROFILE_DEFAULT) == BTCHIP_PROFILE_DEFAULT, "profile: the override wins");
    CHECK(!strcmp(btchip_profile_name(BTCHIP_PROFILE_MEDIATEK), "mediatek") &&
          !strcmp(btchip_profile_name(BTCHIP_PROFILE_DEFAULT), "default"), "profile: names");

    /* override file text */
    CHECK(btchip_parse_override("mediatek\n") == BTCHIP_PROFILE_MEDIATEK &&
          btchip_parse_override("  MTK ") == BTCHIP_PROFILE_MEDIATEK, "override: mediatek / mtk");
    CHECK(btchip_parse_override("marvell") == BTCHIP_PROFILE_DEFAULT && btchip_parse_override("NXP\r\n") == BTCHIP_PROFILE_DEFAULT &&
          btchip_parse_override("default") == BTCHIP_PROFILE_DEFAULT, "override: marvell / nxp / default");
    CHECK(btchip_parse_override("") == -1 && btchip_parse_override(NULL) == -1 &&
          btchip_parse_override("intel") == -1 && btchip_parse_override("mediatekx") == -1, "override: anything else is ignored");
    CHECK(btchip_get_override() == -1, "override: none by default");
    btchip_set_override(BTCHIP_PROFILE_MEDIATEK);
    CHECK(btchip_get_override() == BTCHIP_PROFILE_MEDIATEK, "override: set");
    btchip_set_override(7);
    CHECK(btchip_get_override() == -1, "override: bad value clears it");

    /* scan pause: default profile sends nothing */
    reset(2);
    hci_scan_pause_enable(0);
    hci_scan_pause(h, "connect");
    hci_scan_resume(h);
    CHECK(nops == 0 && scan_val == 2, "scan: default profile never touches Scan_Enable");

    /* mediatek: read, off, restore; nested pauses restore once */
    reset(2);
    hci_scan_pause_enable(1);
    hci_scan_pause(h, "pairing");
    CHECK(nops == 2 && ops[0] == HB_OP_READ_SCAN_ENABLE && ops[1] == HB_OP_WRITE_SCAN_ENABLE && scan_val == 0,
          "scan: mediatek reads Scan_Enable, then turns scanning off");
    hci_scan_pause(h, "connect");
    CHECK(nops == 2, "scan: a nested pause sends nothing");
    hci_scan_resume(h);
    CHECK(nops == 2 && scan_val == 0, "scan: inner resume keeps scanning off");
    hci_scan_resume(h);
    CHECK(nops == 3 && ops[2] == HB_OP_WRITE_SCAN_ENABLE && scan_val == 2, "scan: outer resume puts the system value back");
    hci_scan_resume(h);
    CHECK(nops == 3, "scan: an extra resume does nothing");

    reset(0);
    hci_scan_pause(h, "scan");
    hci_scan_resume(h);
    CHECK(nops == 1 && scan_val == 0, "scan: nothing to pause when the system scan is already off");

    reset(3);
    read_fails = 1;
    hci_scan_pause(h, "scan");
    hci_scan_resume(h);
    CHECK(scan_val == 2 && ops[nops - 1] == HB_OP_WRITE_SCAN_ENABLE, "scan: unread value restores the PS5 default (page scan)");
    hci_scan_pause_enable(0);

    /* ACL pipe: MediaTek 0e8d:3603 if0 (ep81 int, ep01 + ep02 bulk out, ep82 bulk in) */
    {
        static const unsigned char mtk[] = {
            9, 4, 0, 0, 4, 0xE0, 1, 1, 0,
            7, 5, 0x81, 3, 16, 0, 1,
            7, 5, 0x01, 2, 0x00, 2, 0,
            7, 5, 0x82, 2, 0x00, 2, 0,
            7, 5, 0x02, 2, 0x00, 2, 0,
        };
        static const unsigned char mrv[] = {
            9, 4, 0, 0, 3, 0xE0, 1, 1, 0,
            7, 5, 0x81, 3, 16, 0, 1,
            7, 5, 0x82, 2, 64, 0, 0,
            7, 5, 0x02, 2, 64, 0, 0,
        };
        struct usbhci_iface f[4];
        CHECK(usbhci_scan(mtk, (int)sizeof mtk, f) == 1 && f[0].out_ep == 0x01 && f[0].spare_out_ep == 0x02,
              "pipe: MediaTek descriptor read as is");
        usbhci_pair_out_with_in(&f[0]);
        CHECK(f[0].out_ep == 0x02 && f[0].spare_out_ep == 0x01 && f[0].in_ep == 0x82, "pipe: MediaTek ACL goes on 0x02 (paired with 0x82)");
        usbhci_pair_out_with_in(&f[0]);
        CHECK(f[0].out_ep == 0x02, "pipe: pairing twice changes nothing");
        CHECK(usbhci_scan(mrv, (int)sizeof mrv, f) == 1 && f[0].out_ep == 0x02 && !f[0].spare_out_ep,
              "pipe: Marvell descriptor has one OUT");
        usbhci_pair_out_with_in(&f[0]);
        CHECK(f[0].out_ep == 0x02 && !f[0].spare_out_ep, "pipe: Marvell pipe untouched");
    }

    /* AVDTP PSM when SDP gave nothing */
    CHECK(sdp_avdtp_psm_or_default(0) == 0x0019, "psm: no SDP answer falls back to 0x0019, never 0");
    CHECK(sdp_avdtp_psm_or_default(0x0019) == 0x0019 && sdp_avdtp_psm_or_default(0x1001) == 0x1001,
          "psm: a PSM from SDP is kept");

    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails ? 1 : 0;
}
