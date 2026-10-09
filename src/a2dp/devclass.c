/* Developed by X-F1REBALL-X. */
#include "devclass.h"

#include <string.h>

static int says_tv(const char *n)
{
    for (; n && n[0] && n[1]; n++)
        if ((n[0] | 0x20) == 't' && (n[1] | 0x20) == 'v' &&
            !((n[2] | 0x20) >= 'a' && (n[2] | 0x20) <= 'z')) return 1;
    return 0;
}

const char *hb_dev_kind(uint32_t cod, const char *name)
{
    unsigned major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;
    int audio_svc = (cod >> 21) & 1;

    if (major != 4) return hb_dev_name_hints_headphones(name) ? "headphones" : audio_svc ? "audio" : 0;
    switch (minor) {
    case 0x06: return "headphones";
    case 0x01: return "headset";
    case 0x05: return "speaker";
    case 0x07: return "portable";
    case 0x0A: return "hifi";
    case 0x08: return "car";
    case 0x02: return "handsfree";
    case 0x09: case 0x0E: case 0x0F: return "tv";   /* set-top, monitor, display+speaker */
    default:   return says_tv(name) ? "tv" : "av";
    }
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

/* Unnamed devices are listed only with a headphone / headset class. */
/* Class only (name not known yet): would this device be listed once named? */
int hb_dev_rank_class(uint32_t cod)
{
    return dev_rank(cod, "");
}

int hb_dev_rank(uint32_t cod, const char *name)
{
    int r = dev_rank(cod, name);
    if (r > 0 && (!name || !name[0])) return -1;
    return r;
}

static int dev_rank(uint32_t cod, const char *name)
{
    if (!name) name = "";
    unsigned major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;

    if (major == 4) {
        switch (minor) {
        case 0x06: case 0x01: return 0;     /* headphones, wearable headset */
        case 0x07: return 1;                /* portable audio (many earbuds, speakers) */
        case 0x05: case 0x0A: return 1;     /* loudspeaker, hi-fi speaker system */
        case 0x02: return 2;                /* hands-free */
        default: break;
        }
        /* Car kits, set-top boxes, TVs and other A/V gear stay hidden. */
        if (minor == 0x08 || minor == 0x09 || minor == 0x0B || minor == 0x0C ||
            minor == 0x0D || minor == 0x0E || minor == 0x0F || minor == 0x10)
            return -1;
    }
    /* Phones and computers are never audio sinks for us. */
    if (major == 1 || major == 2)
        return -1;
    /* Earbuds with odd CoD values: trust a headphone-like name. */
    return hb_dev_name_hints_headphones(name) ? 3 : -1;
}
