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
