/* Developed by X-F1REBALL-X. Low headset battery heads-up. */
#include "alerts.h"

#include <stdio.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

int main(void)
{
    hb_batt_alert a;
    hb_batt_alert_reset(&a);
    CHECK(hb_batt_alert_step(&a, -1, 0) == 0, "unknown battery: nothing");
    CHECK(hb_batt_alert_step(&a, 60, 0) == 0, "60 %: nothing");
    CHECK(hb_batt_alert_step(&a, 20, 0) == 20, "20 %: heads-up");
    CHECK(hb_batt_alert_step(&a, 20, 0) == 0 && hb_batt_alert_step(&a, 15, 0) == 0, "20 %: only once");
    CHECK(hb_batt_alert_step(&a, 10, 0) == 10, "10 %: second heads-up");
    CHECK(hb_batt_alert_step(&a, 5, 0) == 0, "10 %: only once");
    CHECK(hb_batt_alert_step(&a, 100, 1) == 0 && hb_batt_alert_step(&a, 20, 0) == 20, "charging arms them again");
    hb_batt_alert_reset(&a);
    CHECK(hb_batt_alert_step(&a, 5, 0) == 10 && hb_batt_alert_step(&a, 20, 0) == 0,
          "straight to critical: one heads-up, not two");
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
