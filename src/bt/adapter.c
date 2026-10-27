/* adapter.c - see adapter.h. */
#include "adapter.h"

#include <stdio.h>
#include <string.h>

#include "hci_cmd.h"
#include "log.h"
#include "realtek_fw.h"

/* Audio/Video major class, minor "unclassified" (0x000400). */
static const unsigned char g_cod[3] = { 0x00, 0x04, 0x00 };

static void fail(char *why, int cap, const char *text)
{
    if (why && cap > 0) snprintf(why, (size_t)cap, "%s", text);
}

int bt_adapter_bringup(hci_t hci, int external, char *why, int why_cap)
{
    unsigned char cc[64], name[248], one, scan;
    int cc_len = 0;

    if (why && why_cap > 0) why[0] = 0;
    if (!external) return 1;             /* the console's chip is never reset */

    if (!hci_cmd_sync(hci, HB_OP_RESET, NULL, 0, cc, &cc_len, (int)sizeof cc)) {
        fail(why, why_cap, "the adapter did not answer HCI Reset");
        log_line("adapter: no answer to HCI Reset");
        return 0;
    }
    log_line("adapter: HCI Reset done, controller is ours");

    if (!hb_rtl_prepare(hci, why, why_cap)) return 0;

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

    one = 2;                             /* inquiry results with RSSI + EIR name */
    if (!hci_cmd_sync(hci, HB_OP_WRITE_INQUIRY_MODE, &one, 1, cc, &cc_len, (int)sizeof cc))
        log_line("adapter: WRITE_INQUIRY_MODE failed (names may be missing)");

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
