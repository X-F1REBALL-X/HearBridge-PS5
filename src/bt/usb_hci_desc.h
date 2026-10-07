/* usb_hci_desc.h - locate Bluetooth HCI interfaces in a USB configuration
 * descriptor.
 *
 * Per the USB HCI transport spec an HCI interface has class 0xE0, subclass
 * 0x01, protocol 0x01 and, in alternate setting 0, one interrupt IN pipe
 * (events), one bulk IN pipe and one bulk OUT pipe (ACL). Endpoint numbers
 * are not fixed across PS5 revisions, so we read them instead of guessing. */
#ifndef HEARBRIDGE_USB_HCI_DESC_H
#define HEARBRIDGE_USB_HCI_DESC_H

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

#endif
