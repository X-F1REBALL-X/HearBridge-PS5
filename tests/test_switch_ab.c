/* Developed by X-F1REBALL-X. Disconnect headset A, connect headset B at once:
 * btlink against a fake controller. A's handle is closed with 0x13 and its
 * Disconnection Complete waited for; nothing of A is left (no handle, no
 * Disconnect sent again) and B's page goes out straight away. Also: a handle
 * whose Disconnection Complete another reader took is never disconnected
 * again (it may be a pad's by then). */
#include "btlink.h"
#include "acl_track.h"
#include "util.h"
#include "connreq.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static void hung(int sig) { (void)sig; printf("FAIL accept: hung on a link the headset already dropped\n"); fflush(stdout); _exit(1); }

void log_line(const char *fmt, ...) { (void)fmt; }

static unsigned char evq[64][64];
static int evlen[64], evh, evt;
static unsigned ops[256]; static unsigned char opargs[256][16]; static int nops;
static unsigned next_handle = 2;
static int swallow_disc;          /* Disconnection Complete read by someone else */
static int drop_accept;           /* the headset hangs up right after our accept */

static void push(const unsigned char *e, int n)
{
    acl_track_event(e, n, now_ms());        /* hci_usb does this on receipt */
    if (swallow_disc && e[0] == 0x05) return;
    memcpy(evq[evt % 64], e, (size_t)n); evlen[evt % 64] = n; evt++;
}
static void cstatus(unsigned op) { unsigned char e[6] = { 0x0F, 4, 0, 1, (unsigned char)op, (unsigned char)(op >> 8) }; push(e, 6); }
static void ccomplete(unsigned op) { unsigned char e[6] = { 0x0E, 4, 1, (unsigned char)op, (unsigned char)(op >> 8), 0 }; push(e, 6); }

static int f_cmd(void *s, unsigned op, const void *a, int n)
{
    const unsigned char *p = a;
    (void)s;
    if (nops < 256) { ops[nops] = op; memset(opargs[nops], 0, 16); if (n > 0) memcpy(opargs[nops], p, (size_t)(n > 16 ? 16 : n)); nops++; }
    if (op == 0x0405) {                                  /* Create Connection */
        unsigned char e[13] = { 0x03, 11, 0, (unsigned char)next_handle, 0 };
        cstatus(op); memcpy(e + 5, p, 6); e[11] = 1; e[12] = 0; push(e, 13); next_handle++;
    } else if (op == 0x0411) {                           /* Authentication Requested */
        unsigned char e[5] = { 0x06, 3, 0, p[0], p[1] };
        cstatus(op); push(e, 5);
    } else if (op == 0x0413) {                           /* Set Connection Encryption */
        unsigned char e[6] = { 0x08, 4, 0, p[0], p[1], 1 };
        cstatus(op); push(e, 6);
    } else if (op == 0x0406) {                           /* Disconnect */
        unsigned char e[6] = { 0x05, 4, 0, p[0], p[1], 0x16 };
        cstatus(op); push(e, 6);
    } else if (op == 0x0409) {                           /* Accept Connection Request */
        unsigned char e[13] = { 0x03, 11, 0, (unsigned char)next_handle, 0 };
        cstatus(op); memcpy(e + 5, p, 6); e[11] = 1; e[12] = 0; push(e, 13);
        if (drop_accept) {                               /* 0x06 before encryption */
            unsigned char d[6] = { 0x05, 4, 0, (unsigned char)next_handle, 0, 0x06 };
            push(d, 6);
            drop_accept = 0;
        }
        next_handle++;
    } else if (op == 0x040A) {                           /* Reject Connection Request */
        unsigned char e[13] = { 0x03, 11, 0, 0, 0 };
        cstatus(op); e[2] = p[6]; memcpy(e + 5, p, 6); e[11] = 1; push(e, 13);
    } else if (op == 0x041F || op == 0x0419) {
        cstatus(op);
    } else ccomplete(op);
    return 1;
}
static int f_event(void *s, unsigned char *d, int cap)
{
    int n;
    (void)s;
    if (evh == evt) return 0;
    n = evlen[evh % 64]; if (n > cap) n = cap;
    memcpy(d, evq[evh % 64], (size_t)n); evh++;
    return n;
}
static int f_acl(void *s, unsigned char *d, int cap) { (void)s; (void)d; (void)cap; return 0; }
static int f_pump(void *s, int ms) { (void)s; (void)ms; return evh != evt; }
static int f_send(void *s, const unsigned char *f, int n) { (void)s; (void)f; (void)n; return 1; }
static const hci_ops OPS = { f_acl, f_event, f_pump, f_cmd, f_send, NULL, NULL };

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

static int count_op(int from, unsigned op) { int i, n = 0; for (i = from; i < nops; i++) if (ops[i] == op) n++; return n; }

int main(void)
{
    hci_t hci = { NULL, &OPS };
    unsigned char A[6] = { 0x44, 0xD7, 0xF7, 0xDF, 0xE2, 0xD8 }, B[6] = { 0x05, 0xA4, 0x8D, 0x87, 0x12, 0x08 };
    unsigned char key[16] = { 1 };
    char nm[32] = "A";
    btlink *a, *b;
    unsigned ha;
    int mark, i, first;
    long t0, dt;

    a = btlink_create(hci, 1021, 7);
    CHECK(btlink_connect(a, A, 1, 0, key, 4, nm, sizeof nm, 3000) && btlink_is_up(a), "headset A connected");
    ha = btlink_handle(a);
    mark = nops;
    btlink_disconnect(a);                                   /* manual Disconnect */
    CHECK(count_op(mark, 0x0406) == 1 && btlink_last_close_confirmed() == 1, "Disconnect A: one HCI Disconnect, Disconnection Complete confirmed");
    for (i = mark; i < nops; i++)
        if (ops[i] == 0x0406)
            CHECK((unsigned)(opargs[i][0] | (opargs[i][1] << 8)) == ha && opargs[i][2] == 0x13,
                  "Disconnect A: on A's own handle, reason 0x13");
    btlink_destroy(a);
    CHECK(!btlink_own_acl_pending() && acl_track_request_age(A, now_ms()) < 0, "after A: no ACL or pending call of ours left");

    mark = nops;
    t0 = now_ms();
    b = btlink_create(hci, 1021, 7);
    (void)btlink_drop_stale(b, B);
    strcpy(nm, "B");
    CHECK(btlink_connect(b, B, 1, 0, key, 4, nm, sizeof nm, 3000) && btlink_is_up(b), "headset B connected right after");
    dt = now_ms() - t0;
    first = -1;
    for (i = mark; i < nops; i++) if (ops[i] == 0x0405) { first = i; break; }
    CHECK(count_op(mark, 0x0406) == 0, "connect B: nothing of A disconnected again");
    CHECK(first >= 0 && !memcmp(opargs[first], B, 6), "connect B: its page is the first connection command");
    printf("info B ready %ld ms after Connect (fake controller answers at once; 800 ms is the auth wait)\n", dt);
    CHECK(dt < 1300, "connect B: ready within the 800 ms auth wait plus a little (no leftover delay)");
    btlink_disconnect(b);
    btlink_destroy(b);

    /* Stale handle: A's Disconnection Complete taken by another reader. */
    a = btlink_create(hci, 1021, 7);
    strcpy(nm, "A");
    (void)btlink_connect(a, A, 1, 0, key, 4, nm, sizeof nm, 3000);
    ha = btlink_handle(a);
    swallow_disc = 1;
    { unsigned char e[6] = { 0x05, 4, 0, (unsigned char)ha, 0, 0x08 }; push(e, 6); }   /* supervision timeout */
    swallow_disc = 0;
    mark = nops;
    b = btlink_create(hci, 1021, 7);
    (void)btlink_drop_stale(b, B);
    CHECK(count_op(mark, 0x0406) == 0, "a handle that already went away is not disconnected again (pad safety)");
    CHECK(!btlink_own_acl_pending(), "and it is not counted as ours any more");
    btlink_destroy(b);

    /* Auto switch: A streams, saved B calls in. Newest wins: A is torn
     * down on its own handle, B's waiting call is accepted, B comes up. */
    {
        unsigned char req[12] = { 0x04, 10 };
        hb_cr_in in;
        int which = -1, d;
        unsigned hb;
        unsigned char ab[1][6], kb[1][16], ktb[1] = { 4 };
        a = btlink_create(hci, 1021, 7);
        strcpy(nm, "A");
        CHECK(btlink_connect(a, A, 1, 0, key, 4, nm, sizeof nm, 3000) && btlink_is_up(a), "switch: A up (streaming)");
        ha = btlink_handle(a);
        memcpy(req + 2, B, 6); req[8] = 0x04; req[9] = 0x04; req[10] = 0x24; req[11] = 1;   /* CoD 0x240404 headset */
        mark = nops;
        push(req, 12);
        memset(&in, 0, sizeof in);
        in.is_av = hb_cod_is_av(0x240404); in.saved = 1; in.streaming_other = 1; in.busy_other = 1;
        d = hb_connreq_decide(&in);
        CHECK(d == HB_CR_SWITCH, "switch: saved B calling while A streams -> switch");
        for (i = 0; i < 5; i++) (void)btlink_pump(a, 10);
        CHECK(count_op(mark, 0x0409) == 0 && count_op(mark, 0x040A) == 0, "switch: A's link leaves B's call alone");
        CHECK(acl_track_request_age(B, now_ms()) >= 0, "switch: B's call still waiting");
        btlink_disconnect(a);                                   /* same teardown as Disconnect */
        btlink_destroy(a);
        CHECK(count_op(mark, 0x0406) == 1 && btlink_last_close_confirmed() == 1, "switch: A disconnected once, confirmed");
        for (i = mark; i < nops; i++)
            if (ops[i] == 0x0406)
                CHECK((unsigned)(opargs[i][0] | (opargs[i][1] << 8)) == ha && opargs[i][2] == 0x13,
                      "switch: A's Disconnect on A's own handle, reason 0x13");
        memcpy(ab[0], B, 6); memcpy(kb[0], key, 16);
        b = btlink_create(hci, 1021, 7);
        CHECK(btlink_accept(b, (const unsigned char (*)[6])ab, (const unsigned char (*)[16])kb, ktb, 1, 3000, &which) &&
              which == 0 && btlink_is_up(b), "switch: B's waiting call accepted, B up and encrypted");
        hb = btlink_handle(b);
        first = -1;
        for (i = mark; i < nops; i++) if (ops[i] == 0x0409) { first = i; break; }
        CHECK(first >= 0 && !memcmp(opargs[first], B, 6) && opargs[first][6] == 0x00, "switch: accepted B as central");
        CHECK(count_op(mark, 0x0405) == 0, "switch: no page needed");
        CHECK(hb != ha && hb != 0, "switch: B on its own new handle, A's handle gone");
        CHECK(acl_track_request_age(B, now_ms()) < 0 && acl_track_request_age(A, now_ms()) < 0, "switch: no call left waiting");
        CHECK(count_op(mark, 0x0406) == 1, "switch: nothing else disconnected");
        btlink_disconnect(b);
        btlink_destroy(b);
        CHECK(!btlink_own_acl_pending(), "switch: no leftover ACL of ours at the end");
    }
    /* Idle, out of the case: the headset takes our accept, hangs up before
     * encryption (build 20 log, 0x06) and calls again. The accept must give
     * up at once, not sit on the dead link until the page sends a command,
     * and the next call must be taken (as peripheral) and come up. */
    {
        unsigned char req[12] = { 0x04, 10 };
        unsigned char ab[1][6], kb[1][16], ktb[1] = { 4 };
        int which = -1, ok, again = -1;
        memcpy(ab[0], B, 6); memcpy(kb[0], key, 16);
        memcpy(req + 2, B, 6); req[8] = 0x04; req[9] = 0x04; req[10] = 0x24; req[11] = 1;
        signal(SIGALRM, hung);
        alarm(20);
        mark = nops;
        push(req, 12);
        drop_accept = 1;
        b = btlink_create(hci, 1021, 7);
        t0 = now_ms();
        ok = btlink_accept(b, (const unsigned char (*)[6])ab, (const unsigned char (*)[16])kb, ktb, 1, 3000, &which);
        dt = now_ms() - t0;
        CHECK(!ok && dt < 1500, "dropped accept: gives up at once (no dead wait)");
        btlink_destroy(b);
        push(req, 12);                                         /* it calls again */
        CHECK(acl_track_request_age(B, now_ms()) >= 0, "dropped accept: its new call is waiting to be taken");
        b = btlink_create(hci, 1021, 7);
        ok = btlink_accept(b, (const unsigned char (*)[6])ab, (const unsigned char (*)[16])kb, ktb, 1, 3000, &which);
        for (i = mark; i < nops; i++) if (ops[i] == 0x0409) again = i;
        CHECK(ok && which == 0 && btlink_is_up(b), "dropped accept: the next call is taken, link up and encrypted");
        CHECK(again >= 0 && opargs[again][6] == 0x01, "dropped accept: the next call is taken as peripheral");
        btlink_disconnect(b);
        btlink_destroy(b);
        alarm(0);
    }
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
