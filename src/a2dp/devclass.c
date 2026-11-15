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

static int says_car(const char *n);

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
    case 0x08: return (name && name[0] && !says_car(name) && !says_tv(name)) ? "speaker" : "car";
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
    static const char *hint[] = { "buds", "wf-", "wh-", "airpods", "pods", "ear", "head",
                                  "freeclip", "freebuds",    /* #32 Huawei FreeClip / FreeBuds */
                                  "speaker", "soundbar", "soundcore", "home mini", "nest mini",
                                  "google home", "momentum" };  /* #31 speakers / odd CoD */
    unsigned i;
    for (i = 0; i < sizeof hint / sizeof *hint; i++)
        if (has(name, hint[i])) return 1;
    return 0;
}

static int dev_rank(uint32_t cod, const char *name);

/* Car-kit names: many cheap speakers (JieLi chips) report the Car audio
 * class, so a named 0x08 device is listed unless its name looks like a car. */
static int says_car(const char *n)
{
    static const char *w[] = { "car", "auto", "toyota", "mazda", "hyundai", "kia", "honda",
                               "ford", "bmw", "audi", "skoda", "nissan", "mercedes", "tesla",
                               "uconnect", "sync", "seat", "chevrolet", "peugeot", "renault" };
    unsigned i;
    for (i = 0; i < sizeof w / sizeof *w; i++)
        if (has(n, w[i])) return 1;
    return 0;
}

/* Unnamed devices are listed only with a headphone / headset class. */
/* Class only (name not known yet): would this device be listed once named? */
int hb_dev_rank_class(uint32_t cod)
{
    return dev_rank(cod, "");
}

int hb_dev_rank(uint32_t cod, const char *name)
{
    int r = dev_rank(cod, name);
    /* Unnamed: headphones / headset (0) and, since 1.4.1, a loudspeaker or
     * portable audio class (1) are listed by address until the name comes in
     * (#35: AirPods were skipped as "no name yet"). Hands-free and name-only
     * matches still need a name. */
    if ((!name || !name[0]) && r > 1) return -1;
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
        /* #35: AirPods 4 reported CoD 08243c (display + loudspeaker). A strong
         * earbud name wins over that class; anything that says TV stays hidden. */
        /* 1.4.1: car-audio and display classes need their name first
         * (4 = fetch the name, never listed unnamed). */
        if ((minor == 0x08 || minor == 0x0F) && !name[0]) return 4;
        if (minor == 0x0F && !says_tv(name) &&
            (has(name, "airpods") || has(name, "buds") || has(name, "pods") || has(name, "freeclip")))
            return 3;
        /* Cheap speakers ("Coral" and most JieLi boxes) report Car audio. */
        if (minor == 0x08 && !says_tv(name) && !says_car(name)) return 3;
        if (minor == 0x08 || minor == 0x09 || minor == 0x0B || minor == 0x0C ||
            minor == 0x0D || minor == 0x0E || minor == 0x0F || minor == 0x10)
            return -1;
    }
    /* #34 allowlist: phones, computers and game controllers are never
     * listed, whatever their name says (controllers always win). */
    if (major == 1 || major == 2) return -1;
    if (major == 5 && ((minor & 0x0F) == 0x01 || (minor & 0x0F) == 0x02)) return -1;
    if (has(name, "Wireless Controller") || has(name, "DualSense") || has(name, "DUALSHOCK"))
        return -1;
    /* Earbuds with an odd CoD (AirPods 0x001f00, Galaxy / OnePlus Buds):
     * only by a headphone-like name. */
    return hb_dev_name_hints_headphones(name) ? 3 : -1;
}
