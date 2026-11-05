/* btchip.h - controller labels: USB product vendor and HCI family.
 *
 * The vendor table names the product (the stick you pick). The family enum
 * names the controller silicon, which comes from Read Local Version, not the
 * USB VID: resellers rebadge other makers' chips. Shown on the page and in
 * diag.txt; picks the MediaTek profile. Pure (no I/O). Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_BTCHIP_H
#define HEARBRIDGE_BTCHIP_H

/* Silicon vendors (labels only; the HCI family is the real classification). */
#define BTCHIP_VID_MARVELL   0x1286
#define BTCHIP_VID_NXP       0x05a3
#define BTCHIP_VID_MEDIATEK  0x0e8d
#define BTCHIP_VID_REALTEK   0x0bda
#define BTCHIP_VID_CSR       0x0a12
#define BTCHIP_VID_BROADCOM  0x0a5c
#define BTCHIP_VID_INTEL     0x8087
#define BTCHIP_VID_QUALCOMM  0x0cf3
#define BTCHIP_VID_TI        0x0451
#define BTCHIP_VID_SILABS    0x10c4
#define BTCHIP_VID_BLUEGIGA  0x2458
#define BTCHIP_VID_NORDIC    0x1915

/* Product brands / OEMs: the label the picker shows. Chips can be another
 * maker (TP-Link 37ad carries Realtek, ASUS rebadges Broadcom/Realtek...). */
#define BTCHIP_VID_TPLINK    0x37ad
#define BTCHIP_VID_TPLINK2   0x2357
#define BTCHIP_VID_ASUS      0x0b05
#define BTCHIP_VID_APPLE     0x05ac
#define BTCHIP_VID_MICROSOFT 0x045e
#define BTCHIP_VID_SONY      0x054c
#define BTCHIP_VID_DELL      0x413c
#define BTCHIP_VID_HP        0x03f0
#define BTCHIP_VID_LENOVO    0x17ef
#define BTCHIP_VID_FOXCONN   0x0489
#define BTCHIP_VID_LITEON    0x04ca
#define BTCHIP_VID_BELKIN    0x050d
#define BTCHIP_VID_UGREEN    0x2550

/* USB *product* vendor label ("Marvell/NXP", "MediaTek", "Realtek",
 * "TP-Link", "CSR", ...), or NULL for an unknown VID (or < 0). It is for
 * picking/labelling a physical stick; it says nothing about the chip. The
 * chip family (below) comes from the HCI identity. */
const char *btchip_vendor(int vid);

/* 1 for a MediaTek controller. VID-based, so a reseller id needs its own
 * entry; the Realtek/CSR split uses the HCI identity instead. */
int btchip_is_mediatek(int vid);

/* "Marvell/NXP (1286:2059)", "CSR8510 (0a12:0001)", "abcd:1234" (unknown
 * vendor), or "" when vid < 0 (no controller yet). Always NUL-terminated
 * when cap > 0. */
void btchip_describe(int vid, int pid, char *out, int cap);

/* Controller silicon from Read Local Version manufacturer / LMP identity,
 * never from the USB VID. */
enum {
    HB_BT_FAMILY_OTHER = 0,
    HB_BT_FAMILY_REALTEK,
    HB_BT_FAMILY_CSR,
    HB_BT_FAMILY_MEDIATEK,
    HB_BT_FAMILY_BROADCOM,
    HB_BT_FAMILY_INTEL,
    HB_BT_FAMILY_QUALCOMM
};

/* Bluetooth SIG manufacturer ids (Read Local Version). */
#define BTCHIP_MFR_CSR       0x000a
#define BTCHIP_MFR_REALTEK   0x005d
#define BTCHIP_MFR_MEDIATEK  0x0046
#define BTCHIP_MFR_BROADCOM  0x000f
#define BTCHIP_MFR_INTEL     0x0002
#define BTCHIP_MFR_QUALCOMM  0x001d

/* Map a manufacturer id to a family. A known Realtek LMP subversion wins
 * over an unknown/reseller manufacturer id (pass realtek_lmp = 1). */
int btchip_family(unsigned manufacturer, int realtek_lmp);

/* Display name ("Realtek", "CSR", "MediaTek", ...); "" for OTHER. */
const char *btchip_family_name(int family);

/* Controller profile. MEDIATEK (VID 0e8d, e.g. 3603, 3605): system scan
 * paused while we page/pair/scan, ACL on the OUT pipe paired with the IN,
 * slow L2CAP config pacing. DEFAULT (Marvell/NXP and anything else): the
 * behaviour 1.1.0 had. */
enum { BTCHIP_PROFILE_DEFAULT = 0, BTCHIP_PROFILE_MEDIATEK = 1 };

/* Override from /data/hearbridge/chip: "mediatek"/"mtk" -> MEDIATEK,
 * "marvell"/"nxp"/"default" -> DEFAULT, anything else (or NULL) -> -1
 * (none). Leading/trailing blanks and case are ignored. */
int btchip_parse_override(const char *text);

/* Profile for a controller: override when >= 0, else by VID. */
int btchip_profile(int vid, int override);

/* "mediatek" or "default". */
const char *btchip_profile_name(int profile);

/* Process-wide override (set once at start-up, before the controller is
 * opened). -1 = none (the default). */
void btchip_set_override(int override);
int  btchip_get_override(void);

#endif
