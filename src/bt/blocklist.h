/* Manual Bluetooth address blocklist (#34). A blocked address is always left
 * to the system: never paged, accepted, paired or listed by HearBridge, and
 * its Connection Request makes us stop reading the shared pipes for a while
 * (hci_usb_yield). Saved in /data/hearbridge/blocked.txt, one
 * "XX:XX:XX:XX:XX:XX manual|auto|allow" per line. Thread safe (page thread + stream loop).
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_BLOCKLIST_H
#define HEARBRIDGE_BLOCKLIST_H

#define HB_BLOCK_MAX  16
#define HB_BLOCK_PATH "/data/hearbridge/blocked.txt"

/* Load from path (NULL: HB_BLOCK_PATH). Returns the count loaded. */
int  hb_block_load(const char *path);
/* 1 if addr is blocked. */
int  hb_block_has(const unsigned char addr[6]);
/* Add / remove and save. 1 = done, 0 = bad, full or not saved. */
int  hb_block_add(const unsigned char addr[6]);
int  hb_block_remove(const unsigned char addr[6]);
/* "AA:BB:..." or "aa-bb-..." -> addr. 1 = ok. */
int  hb_block_parse(const char *s, unsigned char out[6]);
/* A game controller: CoD Peripheral with joystick / gamepad minor, or a
 * Sony DualSense / DS4 name. */
int  hb_block_is_controller(unsigned cod, const char *name);
/* Auto-block addr if it is a controller and the user has no entry for it
 * (an unblocked auto entry stays allowed). 1 = newly added. */
int  hb_block_auto(const unsigned char addr[6], unsigned cod, const char *name);
/* {"blocked":[{"addr":"AA:..","auto":0|1},...]} into out; length or -1. */
int  hb_block_json(char *out, int cap);

#endif
