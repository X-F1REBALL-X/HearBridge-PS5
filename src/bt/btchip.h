/* btchip.h - name the console's Bluetooth controller from its USB VID.
 *
 * Shown on the web page and in diag.txt so people can tell which build
 * they need: MediaTek controllers need the mediatek test build.
 * Pure (no I/O). Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_BTCHIP_H
#define HEARBRIDGE_BTCHIP_H

/* 1 in a build that carries the MediaTek controller fixes (the mediatek
 * test build); the regular build tells MediaTek owners to get that one. */
#ifndef HB_MTK_BUILD
#define HB_MTK_BUILD 0
#endif

#define BTCHIP_VID_MARVELL  0x1286
#define BTCHIP_VID_MEDIATEK 0x0e8d

/* "Marvell/NXP", "MediaTek", or NULL for a VID we do not know (or < 0). */
const char *btchip_vendor(int vid);

/* 1 for a MediaTek controller. */
int btchip_is_mediatek(int vid);

/* "Marvell/NXP (1286:2059)", "abcd:1234" (unknown vendor), or "" when
 * vid < 0 (no controller yet). Always NUL-terminated when cap > 0. */
void btchip_describe(int vid, int pid, char *out, int cap);

#endif
