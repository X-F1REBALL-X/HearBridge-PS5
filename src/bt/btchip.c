/* btchip.c - see btchip.h. Developed by X-F1REBALL-X. */
#include "btchip.h"

#include <stdio.h>
#include <string.h>

const char *btchip_vendor(int vid)
{
    switch (vid) {
    /* silicon vendors */
    case BTCHIP_VID_MARVELL:   return "Marvell/NXP";
    case BTCHIP_VID_NXP:       return "Marvell/NXP";
    case BTCHIP_VID_MEDIATEK:  return "MediaTek";
    case BTCHIP_VID_REALTEK:   return "Realtek";
    case BTCHIP_VID_CSR:       return "CSR";
    case BTCHIP_VID_BROADCOM:  return "Broadcom";
    case BTCHIP_VID_INTEL:     return "Intel";
    case BTCHIP_VID_QUALCOMM:  return "Qualcomm/Atheros";
    case BTCHIP_VID_TI:        return "Texas Instruments";
    case BTCHIP_VID_SILABS:    return "Silicon Labs";
    case BTCHIP_VID_BLUEGIGA:  return "Bluegiga";
    case BTCHIP_VID_NORDIC:    return "Nordic";
    /* product brands / OEMs */
    case BTCHIP_VID_TPLINK:    return "TP-Link";
    case BTCHIP_VID_TPLINK2:   return "TP-Link";
    case BTCHIP_VID_ASUS:      return "ASUS";
    case BTCHIP_VID_APPLE:     return "Apple";
    case BTCHIP_VID_MICROSOFT: return "Microsoft";
    case BTCHIP_VID_SONY:      return "Sony";
    case BTCHIP_VID_DELL:      return "Dell";
    case BTCHIP_VID_HP:        return "HP";
    case BTCHIP_VID_LENOVO:    return "Lenovo";
    case BTCHIP_VID_FOXCONN:   return "Foxconn";
    case BTCHIP_VID_LITEON:    return "Lite-On";
    case BTCHIP_VID_BELKIN:    return "Belkin";
    case BTCHIP_VID_UGREEN:    return "UGREEN";
    default:                   return NULL;
    }
}

int btchip_is_mediatek(int vid)
{
    return vid == BTCHIP_VID_MEDIATEK;
}



int btchip_family(unsigned manufacturer, int realtek_lmp)
{
    switch (manufacturer) {
    case BTCHIP_MFR_REALTEK:  return HB_BT_FAMILY_REALTEK;
    case BTCHIP_MFR_CSR:      return HB_BT_FAMILY_CSR;
    case BTCHIP_MFR_MEDIATEK: return HB_BT_FAMILY_MEDIATEK;
    case BTCHIP_MFR_BROADCOM: return HB_BT_FAMILY_BROADCOM;
    case BTCHIP_MFR_INTEL:    return HB_BT_FAMILY_INTEL;
    case BTCHIP_MFR_QUALCOMM: return HB_BT_FAMILY_QUALCOMM;
    default:                  return realtek_lmp ? HB_BT_FAMILY_REALTEK
                                                 : HB_BT_FAMILY_OTHER;
    }
}

const char *btchip_family_name(int family)
{
    switch (family) {
    case HB_BT_FAMILY_REALTEK:  return "Realtek";
    case HB_BT_FAMILY_CSR:      return "CSR";
    case HB_BT_FAMILY_MEDIATEK: return "MediaTek";
    case HB_BT_FAMILY_BROADCOM: return "Broadcom";
    case HB_BT_FAMILY_INTEL:    return "Intel";
    case HB_BT_FAMILY_QUALCOMM: return "Qualcomm";
    default:                    return "";
    }
}

void btchip_describe(int vid, int pid, char *out, int cap)
{
    const char *v = btchip_vendor(vid);
    if (!out || cap <= 0) return;
    if (vid < 0 || vid > 0xffff || pid < 0 || pid > 0xffff) { out[0] = 0; return; }
    /* Common Cambridge Silicon Radio Bluetooth 4.0 stick (plain HCI). */
    if (vid == BTCHIP_VID_CSR && pid == 0x0001)
        snprintf(out, (size_t)cap, "CSR8510 (%04x:%04x)", vid, pid);
    else if (v) snprintf(out, (size_t)cap, "%s (%04x:%04x)", v, vid, pid);
    else        snprintf(out, (size_t)cap, "%04x:%04x", vid, pid);
}

/* Own blank/lower-case helpers: the payload may only import what 1.0.2
 * imported (scripts/check_imports.sh), and ctype is not in that list. */
static int is_blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

int btchip_parse_override(const char *text)
{
    char w[16];
    int n = 0;
    if (!text) return -1;
    while (*text && is_blank(*text)) text++;
    while (*text && !is_blank(*text) && n < (int)sizeof w - 1)
        w[n++] = lower(*text++);
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
