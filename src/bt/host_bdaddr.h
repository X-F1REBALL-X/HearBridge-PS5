/* host_bdaddr.h - one shared host BD_ADDR for every USB stick.
 *
 * Link keys are bound to the host controller address. Each dongle ships with
 * its own BD_ADDR, so a headset paired on stick A will not reconnect on stick
 * B (or the console chip) until it is paired again. We keep one canonical
 * address in /data/hearbridge/host_bdaddr and write it onto every USB adapter
 * at bring-up (CSR BCCMD PSKEY_BDADDR + warm reset; Realtek vendor config
 * offset 0x0030 via the firmware download). The console chip is never
 * rewritten: when it is selected its address becomes the saved one.
 */
#ifndef HEARBRIDGE_HOST_BDADDR_H
#define HEARBRIDGE_HOST_BDADDR_H

#include "hci.h"

#define HB_HOST_BDADDR_PATH "/data/hearbridge/host_bdaddr"

/* 1 when the file holds a valid 6-byte address (little-endian wire order). */
int hb_host_bdaddr_load(unsigned char out[6]);

/* Persist addr (wire order). 1 on success. */
int hb_host_bdaddr_save(const unsigned char addr[6]);

/* Console chip: remember its address as the shared host address. */
void hb_host_bdaddr_learn_console(hci_t hci);

/* USB stick bring-up: clone the saved (or first-seen) address onto this
 * adapter. is_csr / is_realtek select the write path. Returns 1 always
 * (a failed write is logged; bring-up continues with the stick's own addr). */
int hb_host_bdaddr_sync_usb(hci_t hci, int is_csr, int is_realtek);

#endif
