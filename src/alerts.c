/* Developed by X-F1REBALL-X. */
#include "alerts.h"

void hb_batt_alert_reset(hb_batt_alert *a)
{
    a->fired = 0;
}

int hb_batt_alert_step(hb_batt_alert *a, int level, int charging)
{
    if (charging) { a->fired = 0; return 0; }
    if (level < 0) return 0;
    if (level <= HB_BATT_WARN2 && !(a->fired & 2)) { a->fired |= 3; return HB_BATT_WARN2; }
    if (level <= HB_BATT_WARN1 && !(a->fired & 1)) { a->fired |= 1; return HB_BATT_WARN1; }
    return 0;
}
