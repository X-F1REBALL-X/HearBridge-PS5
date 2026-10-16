/* Developed by X-F1REBALL-X. */
#include "devclass.h"

#include <string.h>

const char *hb_dev_kind(uint32_t cod, const char *name)
{
    unsigned major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;

    if (major == 4) {
        switch (minor) {
        case 0x06: return "headphones";
        case 0x01: return "headset";
        case 0x05: return "speaker";
        case 0x07: return "portable";
        case 0x0A: return "hifi";
        case 0x08: return "car";
        case 0x02: return "handsfree";
        case 0x09: case 0x0E: case 0x0F: return "speaker";
        default:   return "speaker";
        }
    }
    return hb_dev_name_hints_headphones(name) ? "headphones" : "speaker";
}

/* Case-insensitive substring. */
static int has(const char *h, const char *n)
{
    size_t i, k;
    if (!h) return 0;
    for (i = 0; h[i]; i++) {
        for (k = 0; n[k]; k++) {
            char c = h[i + k];
            if (!c || (c | 0x20) != (n[k] | 0x20)) break;
        }
        if (!n[k]) return 1;
    }
    return 0;
}

int hb_dev_name_hints_headphones(const char *name)
{
    static const char *hint[] = { "buds", "wf-", "wh-", "airpods", "pods", "ear", "head" };
    unsigned i;
    for (i = 0; i < sizeof hint / sizeof *hint; i++)
        if (has(name, hint[i])) return 1;
    return 0;
}

static int dev_rank(uint32_t cod, const char *name);

/* Class only (name not known yet): would this device be listed once named? */
int hb_dev_rank_class(uint32_t cod)
{
    return dev_rank(cod, "");
}

int hb_dev_rank(uint32_t cod, const char *name)
{
    return dev_rank(cod, name);
}

static int dev_rank(uint32_t cod, const char *name)
{
    if (!name) name = "";
    unsigned major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;

    if (hb_dev_name_hints_headphones(name)) return 0;
    if (major == 4) {
        switch (minor) {
        case 0x06: case 0x01: return 0;     /* headphones, wearable headset */
        case 0x07: case 0x05: case 0x0A: return 1; /* portable, loudspeaker, hi-fi speaker */
        case 0x02: return 2;                /* hands-free */
        default: return 1;                  /* soundbars, A/V, TV displays, car kits */
        }
    }
    return 2; /* any other Bluetooth device / odd CoD */
}
