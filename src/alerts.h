/* Low headset battery: one heads-up at 20 % and one at 10 %, per headset
 * connection; charging or full arms them again. Pure logic.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_ALERTS_H
#define HEARBRIDGE_ALERTS_H

#define HB_BATT_WARN1 20
#define HB_BATT_WARN2 10

typedef struct { int fired; } hb_batt_alert;   /* bit 0: 20 %, bit 1: 10 % */

void hb_batt_alert_reset(hb_batt_alert *a);
/* level 0..100, -1 unknown; charging = on external power / full.
 * Returns the threshold to tell the user about now (20 or 10), else 0.
 * A drop straight past both says 10 and marks 20 as told too. */
int  hb_batt_alert_step(hb_batt_alert *a, int level, int charging);

#endif
