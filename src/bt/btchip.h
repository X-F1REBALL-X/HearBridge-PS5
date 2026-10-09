/* btchip.h - name the console's Bluetooth controller from its USB VID.
 *
 * Shown on the web page and in diag.txt, and picks the controller profile:
 * one build for every chip, the MediaTek fixes only run on a MediaTek.
 * Pure (no I/O). Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_BTCHIP_H
#define HEARBRIDGE_BTCHIP_H

#define BTCHIP_VID_MARVELL  0x1286
#define BTCHIP_VID_MEDIATEK 0x0e8d

/* "Marvell/NXP", "MediaTek", or NULL for a VID we do not know (or < 0). */
const char *btchip_vendor(int vid);

/* 1 for a MediaTek controller. */
int btchip_is_mediatek(int vid);

/* "Marvell/NXP (1286:2059)", "abcd:1234" (unknown vendor), or "" when
 * vid < 0 (no controller yet). Always NUL-terminated when cap > 0. */
void btchip_describe(int vid, int pid, char *out, int cap);

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
