#include "hci_cmd.h"
#include "log.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

void hci_addr_str(const unsigned char addr[6], char buf[18])
{
    snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

int hci_cmd_sync(hci_t hci, unsigned op, const void *params, int plen,
                 unsigned char *out, int *out_len, int out_max)
{
    int attempt;

    if (!hci.ops || !hci.ctx) return 0;

    for (attempt = 1; attempt <= 3; attempt++) {
        long deadline = now_ms() + (attempt == 3 ? 3000 : 1000);
        unsigned char ev[HCI_PKT_MAX];

        if (!hci.ops->cmd(hci.ctx, op, params, plen)) {
            log_line("hci_cmd: transport error sending %#06x", op);
            return 0;
        }

        while (now_ms() < deadline) {
            int n, pr;

            pr = hci.ops->pump(hci.ctx, 20);
            if (pr < 0) {
                log_line("hci_cmd: transport lost while waiting for %#06x", op);
                return 0;
            }
            while ((n = hci.ops->next_event(hci.ctx, ev, (int)sizeof ev)) > 0) {
                /* Event: code, length, parameters.
                 * Command Complete 0x0E: ncmd, opcode LE, status, … */
                if (ev[0] == 0x0E && n >= 6) {
                    unsigned rop = (unsigned)ev[3] | ((unsigned)ev[4] << 8);
                    if (rop == op) {
                        if (out && out_len && out_max > 0) {
                            int copy = n < out_max ? n : out_max;
                            memcpy(out, ev, (size_t)copy);
                            *out_len = copy;
                        }
                        if (ev[5] != 0) {
                            log_line("hci_cmd: %#06x status %#04x", op, ev[5]);
                            return 0;
                        }
                        return 1;
                    }
                }
            }
            /* Drain ACL so shared-controller queues do not stall. */
            while (hci.ops->next_acl(hci.ctx, ev, (int)sizeof ev) > 0) { }
        }
        log_line("hci_cmd: %#06x no reply (try %d)", op, attempt);
    }
    if (hci.ops->diag) hci.ops->diag(hci.ctx);
    return 0;
}

int hci_cmd_status(hci_t hci, unsigned op, const void *params, int plen)
{
    int attempt;

    if (!hci.ops || !hci.ctx) return 0;

    for (attempt = 1; attempt <= 3; attempt++) {
        long deadline = now_ms() + (attempt == 3 ? 3000 : 1000);
        unsigned char ev[HCI_PKT_MAX];

        if (!hci.ops->cmd(hci.ctx, op, params, plen)) {
            log_line("hci_cmd: transport error sending %#06x (status)", op);
            return 0;
        }

        while (now_ms() < deadline) {
            int n, pr;

            pr = hci.ops->pump(hci.ctx, 20);
            if (pr < 0) {
                log_line("hci_cmd: transport lost waiting status %#06x", op);
                return 0;
            }
            while ((n = hci.ops->next_event(hci.ctx, ev, (int)sizeof ev)) > 0) {
                /* Command Status 0x0F: status, ncmd, opcode LE */
                if (ev[0] == 0x0F && n >= 6) {
                    unsigned rop = (unsigned)ev[4] | ((unsigned)ev[5] << 8);
                    if (rop == op) {
                        if (ev[2] != 0) {
                            log_line("hci_cmd: %#06x cmd-status %#04x", op, ev[2]);
                            return 0;
                        }
                        return 1;
                    }
                }
            }
            while (hci.ops->next_acl(hci.ctx, ev, (int)sizeof ev) > 0) { }
        }
        log_line("hci_cmd: %#06x no cmd-status (try %d)", op, attempt);
    }
    if (hci.ops->diag) hci.ops->diag(hci.ctx);
    return 0;
}

static int g_scan_depth;
static int g_scan_saved = -1;          /* system Scan_Enable while paused */
static int g_scan_pause_on;            /* MediaTek only, set by hci_usb */

void hci_scan_pause_enable(int on)
{
    g_scan_pause_on = on;
}

static int g_mediatek;

void hci_set_mediatek(int on) { g_mediatek = on; }
int  hci_is_mediatek(void) { return g_mediatek; }

void hci_scan_pause(hci_t hci, const char *why)
{
    unsigned char cc[16], off = 0;
    int cc_len = 0;
    if (!g_scan_pause_on) return;       /* other controllers: untouched */
    if (g_scan_depth++ > 0) return;
    g_scan_saved = -1;
    if (hci_cmd_sync(hci, HB_OP_READ_SCAN_ENABLE, NULL, 0, cc, &cc_len, (int)sizeof cc) && cc_len >= 7)
        g_scan_saved = cc[6];
    if (g_scan_saved == 0) return;      /* nothing to pause */
    if (hci_cmd_sync(hci, HB_OP_WRITE_SCAN_ENABLE, &off, 1, NULL, NULL, 0))
        log_line("scan: system page/inquiry scan paused for %s (was %d)", why ? why : "-", g_scan_saved);
    else
        log_line("scan: could not pause the system page scan (%s)", why ? why : "-");
    if (g_scan_saved < 0) g_scan_saved = 0x02;   /* unread: PS5 default */
}

void hci_scan_resume(hci_t hci)
{
    unsigned char v;
    if (!g_scan_pause_on) return;
    if (g_scan_depth <= 0 || --g_scan_depth > 0) return;
    if (g_scan_saved <= 0) return;
    v = (unsigned char)g_scan_saved;
    if (!hci_cmd_sync(hci, HB_OP_WRITE_SCAN_ENABLE, &v, 1, NULL, NULL, 0))
        log_line("scan: WARNING could not restore Scan_Enable %d", g_scan_saved);
    else
        log_line("scan: system page scan restored (%d)", g_scan_saved);
    g_scan_saved = -1;
}

/* Stop / exit while the system scan is paused for our page or inquiry
 * (MediaTek): put it back right away. The pump is down by then, so the
 * command is fired, not waited for. Left at 0, no pad could reconnect. */
void hci_scan_release(hci_t hci)
{
    unsigned char v;
    if (g_scan_depth <= 0) return;
    g_scan_depth = 0;
    if (g_scan_saved > 0 && hci.ops && hci.ops->cmd) {
        v = (unsigned char)g_scan_saved;
        if (hci.ops->cmd(hci.ctx, HB_OP_WRITE_SCAN_ENABLE, &v, 1))
            log_line("stop: system page scan put back (%d)", g_scan_saved);
    }
    g_scan_saved = -1;
}

/* Start-up: a copy that died while the system scan was paused left page
 * scan off, so no pad or headset can connect to the console. */
void hci_scan_check(hci_t hci)
{
    unsigned char cc[16], v = 0x02;
    int cc_len = 0;
    if (!hci_cmd_sync(hci, HB_OP_READ_SCAN_ENABLE, NULL, 0, cc, &cc_len, (int)sizeof cc) || cc_len < 7)
        return;
    if (cc[6] & 0x02) return;
    if (hci_cmd_sync(hci, HB_OP_WRITE_SCAN_ENABLE, &v, 1, NULL, NULL, 0))
        log_line("scan: page scan was off (%d), probably left by an earlier copy - turned back on", cc[6]);
}
