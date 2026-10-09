/* hcidbg.h - HCI trace and raw command queue for remote debugging.
 *
 * Every command sent and every event taken from the transport is kept in a
 * small text ring (GET /api/hcilog). POST /api/hci?op=XXXX&p=HEX queues one
 * raw command; the stream loop sends it from idle_pump(), so it never races
 * the controller code. Replies show up in the trace like any other event.
 * Debug only: both endpoints answer 404 unless enabled. */
#ifndef HEARBRIDGE_HCIDBG_H
#define HEARBRIDGE_HCIDBG_H

#include "hci.h"

/* Off unless /data/hearbridge/hci_debug exists at start-up (main.c). */
void hcidbg_enable(void);
int  hcidbg_enabled(void);

void hcidbg_cmd(unsigned op, const unsigned char *p, int n);
void hcidbg_event(const unsigned char *ev, int n);

/* HTTP thread: copy trace lines with sequence > since into out. Returns
 * bytes written. */
int hcidbg_text(unsigned since, char *out, int max);

/* HTTP thread: parse "op=0c19&p=0102" and queue it. 1 = queued. */
int hcidbg_queue(const char *query);

/* Stream loop: send a queued command, if any. */
void hcidbg_service(hci_t hci);

#endif
