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
#include "host_bdaddr.h"

/* Audio/Video major class, minor "unclassified" (0x000400). */
static const unsigned char g_cod[3] = { 0x00, 0x04, 0x00 };

/* Default / Realtek: widen the mask so mode-2 (EIR) result events arrive.
 * Value 0x7DBFFFFFFFFFFFFF little-endian. */
static const unsigned char g_evmask[8] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF, 0x7D
};

/* Proven CSR8510 mask from HearBridge-PS4 (ps4-port probe.c setup_controller):
 * 0x3DBFF807FFFBFFFF. Enables Inquiry Result + Inquiry Result with RSSI
 * (mode 1) and SSP/auth events; does NOT enable Extended Inquiry Result. */
static const unsigned char g_evmask_csr[8] = {
    0xFF, 0xFF, 0xFB, 0xFF, 0x07, 0xF8, 0xBF, 0x3D
};

/* Same page timeout PS4 uses (0x4000 * 0.625 ms = 10.24 s). */
static const unsigned char g_page_timeout[2] = { 0x00, 0x40 };

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

    {
        int known = hci_usb_chip(&vid, &pid);
        is_csr = known && btchip_is_csr(vid);
    }

    /* PS4 CSR path goes straight to Read_BD_ADDR after Reset (no settle).
     * Non-CSR sticks keep a short pause so ROM-mode Realtek is ready. */
    if (!is_csr)
        usleep(250 * 1000);

    /* Shared host BD_ADDR: tell Realtek prepare to bake it into the config
     * blob. CSR cloning happens after prepare (plain HCI, no firmware step). */
    {
        unsigned char want[6];
        if (!is_csr && btchip_is_realtek(vid) && hb_host_bdaddr_load(want))
            hb_rtl_bdaddr_override(want);
    }

    /* Event mask BEFORE inquiry mode. CSR uses the PS4-proven mask; others
     * get the wider EIR-capable mask. */
    if (is_csr) {
        if (!hci_cmd_sync(hci, HB_OP_SET_EVENT_MASK, g_evmask_csr, 8, cc, &cc_len,
                          (int)sizeof cc))
            log_line("adapter: SET_EVENT_MASK failed (inquiry results may be missing)");
        else
            log_line("adapter: event mask 0x3DBFF807FFFBFFFF (CSR/PS4)");
    } else {
        if (!hci_cmd_sync(hci, HB_OP_SET_EVENT_MASK, g_evmask, 8, cc, &cc_len,
                          (int)sizeof cc))
            log_line("adapter: SET_EVENT_MASK failed (inquiry results may be missing)");
        else
            log_line("adapter: event mask 0x7DBFFFFFFFFFFFFF");
        filt = 0;                        /* clear leftover filters (non-CSR) */
        if (!hci_cmd_sync(hci, HB_OP_SET_EVENT_FILTER, &filt, 1, cc, &cc_len,
                          (int)sizeof cc))
            log_line("adapter: SET_EVENT_FILTER clear failed");
    }

    /* Realtek ROM-mode sticks need a patch upload. CSR8510 (0a12:0001) and
     * other plain HCI / class e0:01:01 dongles do not. */
    {
        int known = (vid >= 0);
        if (known && !btchip_is_realtek(vid)) {
            char chip[64];
            btchip_describe(vid, pid, chip, (int)sizeof chip);
            log_line("adapter: %s plain HCI, no firmware download",
                     chip[0] ? chip : "USB stick");
        } else if (!hb_rtl_prepare(hci, why, why_cap)) {
            hb_rtl_bdaddr_override(NULL);
            return 0;
        }
        hb_rtl_bdaddr_override(NULL);
    }

    /* Clone the shared host BD_ADDR onto this stick (CSR BCCMD / Realtek
     * config). First stick with no saved address becomes the shared one. */
    {
        int is_rtl = (vid >= 0) && btchip_is_realtek(vid);
        hb_host_bdaddr_sync_usb(hci, is_csr, is_rtl);
        /* CSR warm reset / Realtek firmware Reset clear the event mask. */
        if (is_csr) {
            if (!hci_cmd_sync(hci, HB_OP_SET_EVENT_MASK, g_evmask_csr, 8, cc, &cc_len,
                              (int)sizeof cc))
                log_line("adapter: SET_EVENT_MASK after BD_ADDR sync failed");
        } else if (is_rtl) {
            if (!hci_cmd_sync(hci, HB_OP_SET_EVENT_MASK, g_evmask, 8, cc, &cc_len,
                              (int)sizeof cc))
                log_line("adapter: SET_EVENT_MASK after BD_ADDR sync failed");
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

    /* Inquiry mode: PS4 CSR bring-up uses mode 1 (Inquiry Result with RSSI,
     * event 0x22). Mode 2 (EIR) is accepted by some CSR clones but they never
     * emit 0x22/0x2F — live PS5 saw Inquiry Complete with only 2 HCI events.
     * Mode 0 works for plain 0x02 but PS4's proven path is mode 1. */
    one = is_csr ? 1 : 2;
    if (!hci_cmd_sync(hci, HB_OP_WRITE_INQUIRY_MODE, &one, 1, cc, &cc_len, (int)sizeof cc))
        log_line("adapter: WRITE_INQUIRY_MODE failed (names may be missing)");
    else
        log_line("adapter: inquiry mode %u%s", one,
                 is_csr ? " (CSR/PS4: RSSI results)" : "");

    /* PS4 enables SSP during controller setup (before any scan/page). */
    one = 1;
    if (!hci_cmd_sync(hci, HB_OP_WRITE_SSP_MODE, &one, 1, cc, &cc_len, (int)sizeof cc))
        log_line("adapter: WRITE_SSP_MODE failed (continuing)");
    else
        log_line("adapter: Simple Pairing enabled");

    if (is_csr) {
        if (!hci_cmd_sync(hci, HB_OP_WRITE_PAGE_TIMEOUT, g_page_timeout, 2, cc, &cc_len,
                          (int)sizeof cc))
            log_line("adapter: WRITE_PAGE_TIMEOUT failed (continuing)");
        else
            log_line("adapter: page timeout 0x4000 (CSR/PS4)");
    }

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
