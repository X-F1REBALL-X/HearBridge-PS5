/* hci_usb.h - HCI over the console's internal USB Bluetooth controller.
 *
 * Implements the hci_ops contract from hci.h on top of a FreeBSD ugen
 * node, following the Bluetooth Core spec Vol 4 Part B (USB transport):
 *   commands  -> class request on the default control pipe (bmRequestType 0x20)
 *   events    <- interrupt IN pipe
 *   ACL data <-> bulk IN / bulk OUT pipes */
#ifndef HEARBRIDGE_HCI_USB_H
#define HEARBRIDGE_HCI_USB_H

#include "hci.h"

/* Find and open the controller, filling *out on success.
 * Returns 1 on success, 0 on failure (out is left zeroed). */
int hci_usb_open(hci_t *out);

#endif
