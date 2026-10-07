/* Developed by X-F1REBALL-X.
 * The list of paired headsets (/data/hearbridge/paired.ini), newest first.
 * headset.ini keeps holding the one in use, so older files keep working. */
#ifndef HB_PAIRED_H
#define HB_PAIRED_H

#include <stddef.h>
#include "headset_ini.h"

#define PAIRED_MAX 8

/* Reads up to max entries with an address and a link key. Returns count. */
int paired_load(const char *path, headset_ini *list, int max);
/* Writes the list atomically. 1 on success. */
int paired_save(const char *path, const headset_ini *list, int n);
/* Puts d first (replacing an entry with the same address). */
void paired_put(headset_ini *list, int *n, int max, const headset_ini *d);
/* Removes the entry with this wire-order address. 1 if it was there. */
int paired_drop(headset_ini *list, int *n, const unsigned char addr[6]);
/* Index of the address, or -1. */
int paired_find(const headset_ini *list, int n, const unsigned char addr[6]);
/* JSON for the page (no keys): {"devices":[{"addr","name","kind","current"}]}.
 * current may be NULL. Returns length or -1. */
int paired_json(const headset_ini *list, int n, const unsigned char *current,
                char *out, size_t cap);
/* "XX:XX:..." display text from a wire-order address (18 bytes). */
void paired_addr_text(const unsigned char addr[6], char out[18]);

#endif
