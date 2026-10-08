/* usb_hci_desc.h - locate Bluetooth HCI interfaces in a USB configuration
 * descriptor.
 *
 * Per the USB HCI transport spec an HCI interface has class 0xE0, subclass
 * 0x01, protocol 0x01 and, in alternate setting 0, one interrupt IN pipe
 * (events), one bulk IN pipe and one bulk OUT pipe (ACL). Endpoint numbers
 * are not fixed across PS5 revisions, so we read them instead of guessing. */
#ifndef HEARBRIDGE_USB_HCI_DESC_H
#define HEARBRIDGE_USB_HCI_DESC_H

#include <stddef.h>
#include <stdint.h>

#define USBHCI_MAX_IFACES 4

struct usbhci_iface {
    int     number;                /* bInterfaceNumber */
    uint8_t evt_ep, in_ep, out_ep; /* endpoint addresses (0 = absent) */
    uint16_t evt_mps, in_mps, out_mps;
    uint8_t spare_out_ep;          /* an extra bulk OUT, if one exists */
};

/* Scan `len` bytes of descriptor `d`. Up to USBHCI_MAX_IFACES complete HCI
 * interfaces are written to `found` in the order they appear. Returns the
 * number written. */
int usbhci_scan(const uint8_t *d, int len, struct usbhci_iface *found);

/* Human-readable summary of every interface and endpoint, for logs and the
 * diagnostics report, e.g.
 *   "if0.0 e0/01/01 (BT HCI) ep81 int/16 ep82 bulk/64 ep02 bulk/64; if1.0 ..."
 * Returns the length written (out is always NUL-terminated if cap > 0). */
int usbhci_describe(const uint8_t *d, int len, char *out, size_t cap);

#endif
