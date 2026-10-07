#include "headset_ini.h"
#include "hci_cmd.h"
#include "log.h"
#include "utf8.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int headset_parse_addr(const char *s, unsigned char out[6])
{
    unsigned b[6];
    int i;
    if (sscanf(s, "%02x:%02x:%02x:%02x:%02x:%02x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return 0;
    /* stored as display order; wire is little-endian (reverse) */
    for (i = 0; i < 6; i++)
        out[i] = (unsigned char)b[5 - i];
    return 1;
}

static int parse_key(const char *s, unsigned char out[16])
{
    int i;
    if ((int)strlen(s) < 32) return 0;
    for (i = 0; i < 16; i++) {
        unsigned v;
        if (sscanf(s + i * 2, "%02x", &v) != 1) return 0;
        out[i] = (unsigned char)v;
    }
    return 1;
}

int headset_ini_load(headset_ini *out)
{
    FILE *f;
    char line[256];

    if (!out) return 0;
    memset(out, 0, sizeof *out);
    f = fopen(HEADSET_INI_PATH, "r");
    if (!f) {
        log_line("headset.ini: cannot open %s", HEADSET_INI_PATH);
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (!strncmp(line, "addr=", 5)) {
            if (!headset_parse_addr(line + 5, out->addr))
                log_line("headset.ini: bad addr");
            else
                out->have_addr = 1;
        } else if (!strncmp(line, "name=", 5)) {
            hb_utf8_copy(out->name, sizeof out->name, line + 5, strlen(line + 5));
        } else if (!strncmp(line, "cod=", 4)) {
            unsigned c = 0;
            sscanf(line + 4, "%x", &c);
            out->cod = c;
        } else if (!strncmp(line, "key_type=", 9)) {
            unsigned t = 0;
            sscanf(line + 9, "%u", &t);
            out->key_type = (unsigned char)t;
        } else if (!strncmp(line, "key=", 4)) {
            if (!parse_key(line + 4, out->link_key))
                log_line("headset.ini: bad key");
            else
                out->ok = 1;
        }
    }
    fclose(f);
    if (!out->have_addr) out->ok = 0;
    if (out->ok) {
        char astr[18];
        hci_addr_str(out->addr, astr);
        log_line("headset.ini: loaded %s \"%s\" key_type=%u",
                 astr, out->name[0] ? out->name : "-", out->key_type);
    }
    return out->ok;
}

int headset_ini_save(const headset_ini *in)
{
    FILE *f;
    char astr[18];
    int i;
    if (!in || !in->ok) return 0;
    f = fopen(HEADSET_INI_PATH, "w");
    if (!f) return 0;
    hci_addr_str(in->addr, astr);
    fprintf(f, "addr=%s\n", astr);
    fprintf(f, "name=%s\n", in->name);
    fprintf(f, "cod=%06x\n", (unsigned)in->cod);
    fprintf(f, "key_type=%u\n", (unsigned)in->key_type);
    fprintf(f, "key=");
    for (i = 0; i < 16; i++) fprintf(f, "%02x", in->link_key[i]);
    fprintf(f, "\n");
    fclose(f);
    return 1;
}
