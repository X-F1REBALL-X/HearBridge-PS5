/* Developed by X-F1REBALL-X. Link tracking from raw HCI events. */
#include "acl_track.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
void log_line(const char *fmt, ...) { (void)fmt; }
int main(void)
{
    static const unsigned char a[6] = { 0x7C, 0x3B, 0x63, 0x62, 0x18, 0x58 };
    unsigned char req[12] = { 0x04, 10 }, cc[13] = { 0x03, 11 }, dc[6] = { 0x05, 4 };
    int fails = 0;
    memcpy(req + 2, a, 6); req[11] = 1;
    acl_track_event(req, 12, 1000);
    if (acl_track_request_age(a, 1500) != 500) { puts("FAIL request age"); fails++; }
    cc[2] = 0; cc[3] = 0x0B; cc[4] = 0x00; memcpy(cc + 5, a, 6); cc[11] = 1; cc[12] = 1;
    acl_track_event(cc, 13, 1600);
    if (acl_track_handle(a) != 0x0B) { puts("FAIL inbound handle not tracked"); fails++; }
    if (acl_track_request_age(a, 1700) != -1) { puts("FAIL request not cleared"); fails++; }
    dc[2] = 0; dc[3] = 0x0B; dc[4] = 0; dc[5] = 0x13;
    acl_track_event(dc, 6, 2000);
    if (acl_track_handle(a)) { puts("FAIL handle kept after disconnect"); fails++; }
    {
        unsigned char ir[17] = { 0x22, 15, 1 }, ps = 0; unsigned ck = 0;
        memcpy(ir + 3, a, 6); ir[9] = 0x02; ir[14] = 0x34; ir[15] = 0x12;
        acl_track_event(ir, 17, 3000);
        if (!acl_track_page_params(a, &ps, &ck) || ps != 2 || ck != 0x9234) {
            printf("FAIL page params %u %04x\n", ps, ck); fails++;
        }
        { unsigned char rc[7] = { 0x1C, 5, 0, 0x0B, 0, 0x11, 0x01 };
          cc[3] = 0x0B; acl_track_event(cc, 13, 3100); acl_track_event(rc, 7, 3200);
          acl_track_event(dc, 6, 3300);
          if (!acl_track_page_params(a, &ps, &ck) || ck != 0x8111) { printf("FAIL clock kept %04x\n", ck); fails++; } }
    }
    {
        /* Our own page failing 0x0b must not hide the headset's pending call;
         * an accept timeout (0x10) or an explicit clear ends it. */
        static const unsigned char x[6] = { 0x44, 0xD7, 0xF7, 0xDF, 0xE2, 0xD8 };
        unsigned char r2[12] = { 0x04, 10 }, c2[13] = { 0x03, 11 };
        memcpy(r2 + 2, x, 6); r2[11] = 1;
        acl_track_event(r2, 12, 10000);
        c2[2] = 0x0B; memcpy(c2 + 5, x, 6); c2[11] = 1;
        acl_track_event(c2, 13, 18000);
        if (acl_track_request_age(x, 18000) != 8000) { puts("FAIL 0x0b cleared the pending call"); fails++; }
        acl_track_request_clear(x);
        if (acl_track_request_age(x, 18100) != -1) { puts("FAIL clear"); fails++; }
        acl_track_event(r2, 12, 20000);
        c2[2] = 0x10;
        acl_track_event(c2, 13, 45000);
        if (acl_track_request_age(x, 45000) != -1) { puts("FAIL accept timeout kept the call"); fails++; }
    }
    {
        /* A handle that disconnected (seen by any reader) is not ours any more. */
        unsigned char c3[13] = { 0x03, 11, 0, 0x02, 0x00 }, d3[6] = { 0x05, 4, 0, 0x02, 0x00, 0x08 };
        unsigned e0;
        memcpy(c3 + 5, a, 6); c3[11] = 1; c3[12] = 0;
        acl_track_event(c3, 13, 50000);
        e0 = acl_track_handle_epoch(0x002);
        if (acl_track_handle_epoch(0x002) != e0) { puts("FAIL epoch stable"); fails++; }
        acl_track_event(d3, 6, 51000);
        if (acl_track_handle_epoch(0x002) == e0) { puts("FAIL epoch after drop (0x08)"); fails++; }
        e0 = acl_track_handle_epoch(0x002);
        acl_track_event(c3, 13, 52000);
        if (acl_track_handle_epoch(0x002) == e0) { puts("FAIL epoch after reuse"); fails++; }
    }
    puts(fails ? "FAIL acl tracking" : "ok   inbound ACL tracked by handle, cleared on disconnect, kept over our 0x0b, stale handle seen");
    return fails != 0;
}
