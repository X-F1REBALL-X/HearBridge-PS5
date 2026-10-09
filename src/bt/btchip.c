/* btchip.c - see btchip.h. Developed by X-F1REBALL-X. */
#include "btchip.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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

int btchip_parse_override(const char *text)
{
    char w[16];
    int n = 0;
    if (!text) return -1;
    while (*text && isspace((unsigned char)*text)) text++;
    while (*text && !isspace((unsigned char)*text) && n < (int)sizeof w - 1)
        w[n++] = (char)tolower((unsigned char)*text++);
    w[n] = 0;
    if (!strcmp(w, "mediatek") || !strcmp(w, "mtk")) return BTCHIP_PROFILE_MEDIATEK;
    if (!strcmp(w, "marvell") || !strcmp(w, "nxp") || !strcmp(w, "default"))
        return BTCHIP_PROFILE_DEFAULT;
    return -1;
}

int btchip_profile(int vid, int override)
{
    if (override == BTCHIP_PROFILE_DEFAULT || override == BTCHIP_PROFILE_MEDIATEK)
        return override;
    return btchip_is_mediatek(vid) ? BTCHIP_PROFILE_MEDIATEK : BTCHIP_PROFILE_DEFAULT;
}

const char *btchip_profile_name(int profile)
{
    return profile == BTCHIP_PROFILE_MEDIATEK ? "mediatek" : "default";
}

static int g_override = -1;

void btchip_set_override(int override)
{
    g_override = (override == BTCHIP_PROFILE_DEFAULT || override == BTCHIP_PROFILE_MEDIATEK) ? override : -1;
}

int btchip_get_override(void)
{
    return g_override;
}
