/* Developed by X-F1REBALL-X. Forget: order and the forgotten list. */
#include "forgot.h"
#include <stdio.h>
static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)
int main(void)
{
    unsigned char a[6] = { 0x44, 0xD7, 0xF7, 0xDF, 0xE2, 0xD8 }, b[6] = { 1, 2, 3, 4, 5, 6 };
    int i;
    CHECK(hb_forget_action(1, 1) == HB_FORGET_AFTER_DISCONNECT,
          "forget the streaming headset: disconnect first, delete after");
    CHECK(hb_forget_action(1, 0) == HB_FORGET_NOW && hb_forget_action(0, 1) == HB_FORGET_NOW,
          "forget one not streaming: delete now");
    CHECK(!hb_forgot_has(a), "nothing forgotten at start");
    hb_forgot_add(a); hb_forgot_add(a);
    CHECK(hb_forgot_has(a) && hb_forgot_count() == 1, "forgotten once, no duplicate");
    CHECK(!hb_forgot_has(b), "other headsets untouched");
    hb_forgot_clear(a);
    CHECK(!hb_forgot_has(a), "paired again: no longer forgotten");
    for (i = 0; i < HB_FORGOT_MAX + 3; i++) { b[0] = (unsigned char)i; hb_forgot_add(b); }
    b[0] = 0;
    CHECK(hb_forgot_count() == HB_FORGOT_MAX && !hb_forgot_has(b), "list full: the oldest goes");
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
