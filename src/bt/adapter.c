/* adapter.c - see adapter.h. */
#include "adapter.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "btchip.h"
#include "hci_cmd.h"
#include "hci_usb.h"
#include "log.h"
#include "realtek_fw.h"

/* Audio/Video major class, minor "unclassified" (0x000400). */
static const unsigned char g_cod[3] = { 0x00, 0x04, 0x00 };

/* Same widened mask as a2dp_open: default + SSP + Inquiry Result with RSSI
 * + Extended Inquiry Result. Must be set BEFORE Write Inquiry Mode 2, or a
 * stick that only emits 0x22/0x2F (and not plain 0x02) returns zero devices. */
static const unsigned char g_evmask[8] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF, 0x7D
};

static void fail(char *why, int cap, const char *text)
{
    if (why && cap > 0) snprintf(why, (size_t)cap, "%s", text);
}

int bt_adapter_bringup(hci_t hci, int external, char *why, int why_cap)
{
    unsigned char cc[64], name[248], one, scan, filt;
    int cc_len = 0;
    int vid = -1, pid = -1, is_csr = 0;

    if (why && why_cap > 0) why[0] = 0;
    if (!external) return 1;             /* the console's chip is never reset */

    if (!hci_cmd_sync(hci, HB_OP_RESET, NULL, 0, cc, &cc_len, (int)sizeof cc)) {
        fail(why, why_cap, "the adapter did not answer HCI Reset");
        log_line("adapter: no answer to HCI Reset");
        return 0;
    }
    log_line("adapter: HCI Reset done, controller is ours");

    /* CSR8510 (and clones) need a short pause after Reset before further
     * commands stick; without it inquiry can complete with zero results. */
    usleep(250 * 1000);

    /* Event mask first — before inquiry mode — so mode-2 result events are
     * not filtered. Clear any leftover event filter while we are at it. */
    if (!hci_cmd_sync(hci, HB_OP_SET_EVENT_MASK, g_evmask, 8, cc, &cc_len,
                      (int)sizeof cc))
        log_line("adapter: SET_EVENT_MASK failed (inquiry results may be missing)");
    else
        log_line("adapter: event mask 0x7DBFFFFFFFFFFFFF");
    filt = 0;                            /* clear all filters */
    if (!hci_cmd_sync(hci, HB_OP_SET_EVENT_FILTER, &filt, 1, cc, &cc_len,
                      (int)sizeof cc))
        log_line("adapter: SET_EVENT_FILTER clear failed");

    /* Realtek ROM-mode sticks need a patch upload. CSR8510 (0a12:0001) and
     * other plain HCI / class e0:01:01 dongles do not: skip by USB VID when
     * known so we never touch the Realtek path on them. Unknown VID still
     * goes through prepare (it no-ops unless the LMP manufacturer is Realtek). */
    {
        int known = hci_usb_chip(&vid, &pid);
        is_csr = known && btchip_is_csr(vid);
        if (known && !btchip_is_realtek(vid)) {
            char chip[64];
            btchip_describe(vid, pid, chip, (int)sizeof chip);
            log_line("adapter: %s plain HCI, no firmware download",
                     chip[0] ? chip : "USB stick");
        } else if (!hb_rtl_prepare(hci, why, why_cap)) {
            return 0;
        }
    }

    /* A Low Energy only stick enumerates and answers HCI, but cannot do
     * A2DP; reject it instead of leaving an adapter that never streams.
     * Features byte 4, bit 5: BR/EDR not supported. */
    if (hci_cmd_sync(hci, HB_OP_READ_LOCAL_FEATURES, NULL, 0, cc, &cc_len,
                     (int)sizeof cc) && cc_len >= 14 && (cc[10] & 0x20)) {
        fail(why, why_cap, "this adapter has no Bluetooth Classic (BR/EDR); audio needs it");
        log_line("adapter: no BR/EDR (features %02x %02x %02x %02x %02x)",
                 cc[6], cc[7], cc[8], cc[9], cc[10]);
        return 0;
    }

    /* Inquiry mode 2 = RSSI + EIR. Some CSR8510 clones accept mode 2 but
     * never emit 0x22/0x2F (live: Inquiry Complete with only 2 HCI events,
     * zero devices). Mode 0 still uses plain Inquiry Result (0x02). */
    one = is_csr ? 0 : 2;
    if (!hci_cmd_sync(hci, HB_OP_WRITE_INQUIRY_MODE, &one, 1, cc, &cc_len, (int)sizeof cc))
        log_line("adapter: WRITE_INQUIRY_MODE failed (names may be missing)");
    else
        log_line("adapter: inquiry mode %u%s", one, is_csr ? " (CSR: plain results)" : "");

    if (!hci_cmd_sync(hci, HB_OP_WRITE_CLASS_OF_DEV, g_cod, 3, cc, &cc_len, (int)sizeof cc))
        log_line("adapter: WRITE_CLASS_OF_DEVICE failed");

    memset(name, 0, sizeof name);
    snprintf((char *)name, sizeof name, "HearBridge");
    if (!hci_cmd_sync(hci, HB_OP_WRITE_LOCAL_NAME, name, (int)sizeof name, cc, &cc_len,
                      (int)sizeof cc))
        log_line("adapter: WRITE_LOCAL_NAME failed");

    /* Page scan on: a saved headset can connect in by itself while idle. */
    scan = 2;
    if (!hci_cmd_sync(hci, HB_OP_WRITE_SCAN_ENABLE, &scan, 1, cc, &cc_len, (int)sizeof cc))
        log_line("adapter: WRITE_SCAN_ENABLE failed (page scan may be off)");

    log_line("adapter: configured as HearBridge (page scan on)");
    return 1;
}
