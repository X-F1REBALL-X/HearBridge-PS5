/* Developed by X-F1REBALL-X. */
#include "forgot.h"

#include <string.h>

static unsigned char g_f[HB_FORGOT_MAX][6];
static int g_n;

int hb_forgot_has(const unsigned char a[6])
{
    int i;
    for (i = 0; i < g_n; i++) if (!memcmp(g_f[i], a, 6)) return 1;
    return 0;
}

void hb_forgot_add(const unsigned char a[6])
{
    if (hb_forgot_has(a)) return;
    if (g_n == HB_FORGOT_MAX) {                 /* oldest goes */
        memmove(g_f[0], g_f[1], (size_t)(HB_FORGOT_MAX - 1) * 6);
        g_n--;
    }
    memcpy(g_f[g_n++], a, 6);
}

void hb_forgot_clear(const unsigned char a[6])
{
    int i;
    for (i = 0; i < g_n; i++)
        if (!memcmp(g_f[i], a, 6)) {
            memmove(g_f[i], g_f[i + 1], (size_t)(g_n - i - 1) * 6);
            g_n--;
            return;
        }
}

int hb_forgot_count(void) { return g_n; }
const unsigned char *hb_forgot_at(int i) { return i >= 0 && i < g_n ? g_f[i] : NULL; }

int hb_forget_action(int is_current, int streaming)
{
    return is_current && streaming ? HB_FORGET_AFTER_DISCONNECT : HB_FORGET_NOW;
}
