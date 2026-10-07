/* Minimal HCI command helpers for HearBridge spikes (no pad host). */
#ifndef HEARBRIDGE_HCI_CMD_H
#define HEARBRIDGE_HCI_CMD_H

#include "hci.h"

#include <stdint.h>

/* Opcodes used by HCI spikes (OGF << 10 | OCF). */
#define HB_OP_INQUIRY             0x0401
#define HB_OP_INQUIRY_CANCEL      0x0402
#define HB_OP_CREATE_CONNECTION   0x0405
#define HB_OP_DISCONNECT          0x0406
#define HB_OP_LINK_KEY_REPLY      0x040B
#define HB_OP_LINK_KEY_NEG_REPLY  0x040C
#define HB_OP_PIN_CODE_REPLY      0x040D
#define HB_OP_AUTH_REQUESTED      0x0411
#define HB_OP_SET_ENCRYPTION      0x0413
#define HB_OP_REMOTE_NAME_REQ     0x0419
#define HB_OP_IO_CAP_REPLY        0x042B
#define HB_OP_USER_CONFIRM_REPLY  0x042C
#define HB_OP_SET_EVENT_MASK      0x0C01
#define HB_OP_READ_SCAN_ENABLE    0x0C19
#define HB_OP_WRITE_SCAN_ENABLE   0x0C1A
#define HB_OP_READ_SSP_MODE       0x0C55
#define HB_OP_WRITE_SSP_MODE      0x0C56
#define HB_OP_READ_BUFFER_SIZE    0x1005
#define HB_OP_READ_BD_ADDR        0x1009

/* Waits for Command Complete for `op`. Retries the command (shared radio).
 * On success copies the event (code..params) into out[0..*out_len).
 * Returns 1 if status byte is 0, else 0. */
int hci_cmd_sync(hci_t hci, unsigned op, const void *params, int plen,
                 unsigned char *out, int *out_len, int out_max);

/* Like hci_cmd_sync but waits for Command Status (0x0F) — Inquiry,
 * Create Connection, Remote Name Request, etc. */
int hci_cmd_status(hci_t hci, unsigned op, const void *params, int plen);

/* Formats a Bluetooth address (little-endian as on the wire) into buf. */
void hci_addr_str(const unsigned char addr[6], char buf[18]);

#endif
