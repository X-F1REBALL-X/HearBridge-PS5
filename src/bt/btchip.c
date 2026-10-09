/* btchip.c - see btchip.h. Developed by X-F1REBALL-X. */
#include "btchip.h"

#include <stdio.h>

const char *btchip_vendor(int vid)
{
    switch (vid) {
    case BTCHIP_VID_MARVELL:  return "Marvell/NXP";
    case BTCHIP_VID_MEDIATEK: return "MediaTek";
    default:                  return NULL;
    }
}

int btchip_is_mediatek(int vid)
{
    return vid == BTCHIP_VID_MEDIATEK;
}

void btchip_describe(int vid, int pid, char *out, int cap)
{
    const char *v = btchip_vendor(vid);
    if (!out || cap <= 0) return;
    if (vid < 0 || vid > 0xffff || pid < 0 || pid > 0xffff) { out[0] = 0; return; }
    if (v) snprintf(out, (size_t)cap, "%s (%04x:%04x)", v, vid, pid);
    else   snprintf(out, (size_t)cap, "%04x:%04x", vid, pid);
}
