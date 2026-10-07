/* Developed by X-F1REBALL-X. Class-of-Device → kind + list order. */
#ifndef HB_DEVCLASS_H
#define HB_DEVCLASS_H
#include <stdint.h>
/* Short kind for the page: "headphones", "headset", "speaker", "portable",
 * "hifi", "car", "handsfree", "tv", "av", "audio", or NULL = not audio. */
const char *hb_dev_kind(uint32_t cod, const char *name);
/* Only headphones and speakers are listed. Order (lower first): 0 headphones /
 * wearable headset, 1 portable audio / loudspeaker / hi-fi speaker, 2 hands-free
 * (all CoD major Audio/Video), 3 any
 * other device whose name hints at headphones (Buds, WF-, WH-, AirPods,
 * Pods, Ear, Head); -1 = hidden (TV, set-top box, speaker, car, phone...). */
int hb_dev_rank(uint32_t cod, const char *name);
/* 1 if the name looks like headphones / earbuds. */
int hb_dev_name_hints_headphones(const char *name);
#endif
