/* Developed by X-F1REBALL-X. */
#include "acl_track.h"
#include "log.h"

#include <string.h>

#define TRACK_MAX 12

/* Bumped on every Connection Complete (success) and Disconnection Complete
 * seen for a handle, whoever reads the event afterwards. */
static unsigned g_epoch[0x1000];

static struct { unsigned char addr[6]; unsigned handle; long req_ms; int used; unsigned char psrm; unsigned clock;
                 unsigned any_handle, any_epoch; } g_t[TRACK_MAX];

static int slot(const unsigned char a[6], int create)
{
    int i, free_i = -1;
    for (i = 0; i < TRACK_MAX; i++) {
        if (g_t[i].used && !memcmp(g_t[i].addr, a, 6)) return i;
        if (!g_t[i].used && free_i < 0) free_i = i;
    }
    if (!create) return -1;
    if (free_i < 0) {
        if (create == 2) return -1;   /* only into a free slot: never evict one */
        free_i = 0;
    }
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
    if (ev[0] == 0x03 && n >= 13 && ev[2] == 0 && ev[11] == 0x01)
        g_epoch[((unsigned)ev[3] | ((unsigned)ev[4] << 8)) & 0x0FFF]++;
    else if (ev[0] == 0x05 && n >= 6 && ev[2] == 0)
        g_epoch[((unsigned)ev[3] | ((unsigned)ev[4] << 8)) & 0x0FFF]++;
    if (ev[0] == 0x04 && n >= 12 && ev[11] == 0x01) {          /* Connection Request, ACL */
        i = slot(ev + 2, 1);
        if (i < 0) return;
        if (g_t[i].req_ms < 0 || now - g_t[i].req_ms > 3000)
            log_line("acl: connection request from %02X:%02X:%02X:%02X:%02X:%02X",
                     ev[7], ev[6], ev[5], ev[4], ev[3], ev[2]);
        g_t[i].req_ms = now;
    } else if (ev[0] == 0x03 && n >= 13 && ev[11] == 0x01 && ev[12] != 0x01) {
        /* Connection Complete, ACL (byte 11 = link type). The handle block
         * below only ever ran for byte 12 (encryption) == 1, i.e. almost
         * never; it stays that way (handle tracking also drives drops and a
         * stale handle could be a pad's). Here only the pending call ends,
         * unless it is our own page failing 0x0b. */
        i = slot(ev + 5, ev[2] == 0 ? 2 : 0);
        if (i >= 0 && ev[2] != 0x0B) g_t[i].req_ms = -1;
        if (i >= 0 && ev[2] == 0) {
            /* Any owner (the console's own stack too): only for
             * acl_track_any_handle(), never for acl_track_handle(). */
            g_t[i].any_handle = ((unsigned)ev[3] | ((unsigned)ev[4] << 8)) & 0x0FFF;
            g_t[i].any_epoch = g_epoch[g_t[i].any_handle];
        }
    } else if (ev[0] == 0x03 && n >= 13 && ev[12] == 0x01) {   /* Connection Complete, ACL */
        i = slot(ev + 5, ev[2] == 0);
        if (i < 0) return;
        /* 0x0b on OUR page to this address does not end the headset's own
         * request: that one is still waiting at the controller. */
        if (ev[2] != 0x0B) g_t[i].req_ms = -1;
        if (ev[2] == 0) {
            g_t[i].handle = ((unsigned)ev[3] | ((unsigned)ev[4] << 8)) & 0x0FFF;
            g_t[i].any_handle = g_t[i].handle;
            g_t[i].any_epoch = g_epoch[g_t[i].handle];
            log_line("acl: link up handle %#05x (tracked)", g_t[i].handle);
        }
    } else if (ev[0] == 0x05 && n >= 6 && ev[2] == 0) {        /* Disconnection Complete */
        unsigned h = ((unsigned)ev[3] | ((unsigned)ev[4] << 8)) & 0x0FFF;
        for (i = 0; i < TRACK_MAX; i++) {        /* keep page params for the reconnect */
            if (g_t[i].used && g_t[i].handle == h) g_t[i].handle = 0;
            if (g_t[i].used && g_t[i].any_handle == h) g_t[i].any_handle = 0;
        }
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

unsigned acl_track_any_handle(const unsigned char addr[6])
{
    int i = slot(addr, 0);
    if (i < 0 || !g_t[i].any_handle) return 0;
    if (g_epoch[g_t[i].any_handle] != g_t[i].any_epoch) return 0;   /* that handle changed since */
    return g_t[i].any_handle;
}

long acl_track_request_age(const unsigned char addr[6], long now)
{
    int i = slot(addr, 0);
    if (i < 0 || g_t[i].req_ms < 0) return -1;
    return now - g_t[i].req_ms;
}

void acl_track_request_clear(const unsigned char addr[6])
{
    int i = slot(addr, 0);
    if (i >= 0) g_t[i].req_ms = -1;
}

unsigned acl_track_handle_epoch(unsigned handle)
{
    return g_epoch[handle & 0x0FFF];
}
