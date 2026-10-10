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

/* USB VID:PID of the opened controller (see btchip.h for the vendor).
 * Returns 1 once hci_usb_open() succeeded, else 0 with both set to -1. */
int hci_usb_chip(int *vid, int *pid);

/* Called (outside the USB completion loop) for every HCI Connection Request
 * event the controller sends, and once per pump with ev NULL. Set by main. */
extern void (*hci_usb_conn_req_hook)(hci_t hci, const unsigned char *ev, int n);

/* Idle duty cycle (#29). The system stack reads the same USB pipes as we
 * do and a packet goes to whoever has a read pending, so while our reads
 * are armed a DualSense waking up can miss its Connection Request. When
 * nothing is playing or connecting, main turns this on: reads armed for
 * listen_ms (a saved headset calling in is still seen), then all of them
 * cancelled for rest_ms, so the console's own stack gets every packet.
 * Our own HCI command or ACL frame keeps the reads armed for 4 s so its
 * answer is read. 0, 0 = off: reads always armed. */
void hci_usb_duty(int listen_ms, int rest_ms);
/* Duty cycle on: start a rest now (a pad or phone is calling the console). */
void hci_usb_yield(void);
/* 1 while resting (no reads in flight). */
int hci_usb_resting(void);

/* Diagnostics: open every /dev/ugen* node read-only, log and report (diag.h)
 * its VID:PID, names, interfaces (class/subclass/protocol) and endpoints,
 * then close it again. Nothing is sent to any device. Call before
 * hci_usb_open(). Returns the number of nodes listed. */
int hci_usb_survey(void);

#endif
