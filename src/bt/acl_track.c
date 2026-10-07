/* Developed by X-F1REBALL-X. */
#include "acl_track.h"
#include "log.h"

#include <string.h>

#define TRACK_MAX 12

static struct { unsigned char addr[6]; unsigned handle; long req_ms; int used; unsigned char psrm; unsigned clock; } g_t[TRACK_MAX];

static int slot(const unsigned char a[6], int create)
{
    int i, free_i = -1;
    for (i = 0; i < TRACK_MAX; i++) {
        if (g_t[i].used && !memcmp(g_t[i].addr, a, 6)) return i;
        if (!g_t[i].used && free_i < 0) free_i = i;
    }
    if (!create) return -1;
    if (free_i < 0) free_i = 0;
    memset(&g_t[free_i], 0, sizeof g_t[free_i]);
    memcpy(g_t[free_i].addr, a, 6);
    g_t[free_i].used = 1;
    g_t[free_i].req_ms = -1;
    return free_i;
}

void acl_track_event(const unsigned char *ev, int n, long now)
{
    int i;
    if (!ev || n < 2) return;
    if (ev[0] == 0x04 && n >= 12 && ev[11] == 0x01) {          /* Connection Request, ACL */
        i = slot(ev + 2, 1);
        g_t[i].req_ms = now;
    } else if (ev[0] == 0x03 && n >= 13 && ev[12] == 0x01) {   /* Connection Complete, ACL */
        i = slot(ev + 5, ev[2] == 0);
        if (i < 0) return;
        g_t[i].req_ms = -1;
        if (ev[2] == 0) {
            g_t[i].handle = ((unsigned)ev[3] | ((unsigned)ev[4] << 8)) & 0x0FFF;
            log_line("acl: link up handle %#05x (tracked)", g_t[i].handle);
        }
    } else if (ev[0] == 0x05 && n >= 6 && ev[2] == 0) {        /* Disconnection Complete */
        unsigned h = ((unsigned)ev[3] | ((unsigned)ev[4] << 8)) & 0x0FFF;
        for (i = 0; i < TRACK_MAX; i++)          /* keep page params for the reconnect */
            if (g_t[i].used && g_t[i].handle == h) g_t[i].handle = 0;
    } else if (ev[0] == 0x1C && n >= 7 && ev[2] == 0) {        /* Read Clock Offset Complete */
        unsigned h = ((unsigned)ev[3] | ((unsigned)ev[4] << 8)) & 0x0FFF;
        for (i = 0; i < TRACK_MAX; i++)
            if (g_t[i].used && g_t[i].handle == h) {
                g_t[i].clock = (((unsigned)ev[5] | ((unsigned)ev[6] << 8)) & 0x7FFF) | 0x8000;
                if (!g_t[i].psrm) g_t[i].psrm = 0x01;
            }
    } else if (ev[0] == 0x02 && n >= 3) {                      /* Inquiry Result */
        int k, m = ev[2];
        if (n < 3 + m * 14) return;
        for (k = 0; k < m; k++) {
            const unsigned char *a = ev + 3 + 6 * k;
            const unsigned char *ck = ev + 3 + m * 12 + 2 * k;
            i = slot(a, 1);
            g_t[i].psrm = ev[3 + m * 6 + k];
            g_t[i].clock = ((unsigned)ck[0] | ((unsigned)ck[1] << 8)) | 0x8000;
        }
    } else if ((ev[0] == 0x22 || ev[0] == 0x2F) && n >= 17) {  /* with RSSI / extended */
        int k, m = ev[0] == 0x2F ? 1 : ev[2];
        for (k = 0; k < m && n >= 3 + (k + 1) * 14; k++) {
            const unsigned char *r = ev + 3 + 14 * k;
            i = slot(r, 1);
            g_t[i].psrm = r[6];
            g_t[i].clock = ((unsigned)r[11] | ((unsigned)r[12] << 8)) | 0x8000;
        }
    }
}

int acl_track_page_params(const unsigned char addr[6], unsigned char *psrm, unsigned *clock)
{
    int i = slot(addr, 0);
    if (i < 0 || !(g_t[i].clock & 0x8000)) return 0;
    if (psrm) *psrm = g_t[i].psrm <= 2 ? (g_t[i].psrm ? g_t[i].psrm : 0x01) : 0x01;
    if (clock) *clock = g_t[i].clock;
    return 1;
}

unsigned acl_track_handle(const unsigned char addr[6])
{
    int i = slot(addr, 0);
    return i >= 0 ? g_t[i].handle : 0;
}

long acl_track_request_age(const unsigned char addr[6], long now)
{
    int i = slot(addr, 0);
    if (i < 0 || g_t[i].req_ms < 0) return -1;
    return now - g_t[i].req_ms;
}
