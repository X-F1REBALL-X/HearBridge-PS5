#ifndef HEARBRIDGE_HEADSET_INI_H
#define HEARBRIDGE_HEADSET_INI_H

#include <stdint.h>

#define HEADSET_INI_PATH "/data/hearbridge/headset.ini"
#define HEADSET_NAME_MAX 64

typedef struct {
    unsigned char addr[6];
    char name[HEADSET_NAME_MAX];
    uint32_t cod;
    unsigned char link_key[16];
    unsigned char key_type;
    int ok;           /* addr + link key present (reconnect possible) */
    int have_addr;    /* addr= present (also used as override when no key) */
} headset_ini;

/* "XX:XX:XX:XX:XX:XX" (display order) → little-endian wire order. 1 on success. */
int headset_parse_addr(const char *s, unsigned char out[6]);

int headset_ini_load(headset_ini *out);
int headset_ini_save(const headset_ini *in);

#endif
