/* hci_evasm.h - rebuild HCI event packets from interrupt-endpoint chunks.
 *
 * An HCI event is: event code (1 byte), parameter length (1 byte), then up
 * to 255 parameter bytes (Core spec Vol 4 Part E 5.4.4). Over USB it arrives
 * split into wMaxPacketSize chunks, and with the OS reading the same pipe a
 * chunk can go missing. A partial event older than HCI_EVASM_STALE_MS is
 * therefore abandoned so it cannot block later events. */
#ifndef HEARBRIDGE_HCI_EVASM_H
#define HEARBRIDGE_HCI_EVASM_H

#include <stdint.h>

#define HCI_EVASM_STALE_MS 100

struct hci_evasm {
    uint8_t  acc[2 + 255 + 64];
    int      fill;
    long     last_ms;
    unsigned abandoned;
};

typedef void (*hci_evasm_cb)(void *user, const uint8_t *evt, int len);

void hci_evasm_reset(struct hci_evasm *a);

/* Append a chunk received at time `t`; `cb` runs once per finished event. */
void hci_evasm_push(struct hci_evasm *a, const uint8_t *chunk, int n, long t,
                    hci_evasm_cb cb, void *user);

#endif
