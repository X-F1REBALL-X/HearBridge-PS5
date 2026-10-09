/* Headsets forgotten this run: never auto-connected or accepted again until
 * they are paired again (their own call is turned down so it does not hold
 * the radio). And when a Forget may delete the saved key. Pure.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_FORGOT_H
#define HEARBRIDGE_FORGOT_H

#define HB_FORGOT_MAX 8

void hb_forgot_add(const unsigned char addr[6]);
int  hb_forgot_has(const unsigned char addr[6]);
void hb_forgot_clear(const unsigned char addr[6]);   /* paired / picked again */
int  hb_forgot_count(void);
const unsigned char *hb_forgot_at(int i);

enum { HB_FORGET_NOW = 0, HB_FORGET_AFTER_DISCONNECT = 1 };
/* The connected (streaming) headset is disconnected cleanly first; its key,
 * settings and list entry go once Disconnection Complete is in. */
int  hb_forget_action(int is_current, int streaming);

#endif
