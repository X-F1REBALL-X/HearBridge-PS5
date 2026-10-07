/* Developed by X-F1REBALL-X.
 * Simulated link: media produced at the stream rate, paced, sent against
 * 6 controller credits; completions come back as Number Of Completed
 * Packets events. Throughput must keep up with the stream. */
#include "acl_pool.h"
#include <stdio.h>
#include <string.h>

#define QCAP 40
#define MAXF 4096

static int fails;

/* evt_every: NOCP batch interval (ms), 0 = never. done_ms: controller
 * time per packet. Returns packets/s sent. */
static int lose_every;   /* ms: one completion batch is lost this often (0 = never) */

static double run(const char *name, int evt_every, int done_ms, int secs, int per_pkt,
                  int expect_ok)
{
    acl_pool p;
    long clock = 0, now, next_prod = 0;
    long q_due[QCAP]; int qn = 0;
    long fin[MAXF]; int nfin = 0;      /* completion times of sent packets */
    int completed_unreported = 0, i;
    long radio_free = 0;              /* the radio sends one packet at a time */
    unsigned long drops = 0;
    double pkt_ms = per_pkt * 128 * 1000.0 / 48000.0, prod_t = 0;
    long dur = (long)(pkt_ms);         /* as btlink_set_media_pace: truncated */
    unsigned long produced = 0;
    long stall_from = 0, max_stall = 0;
    double pps;

    memset(&p, 0, sizeof p);
    p.limit = 6;
    for (now = 0; now < secs * 1000L; now++) {
        /* produce */
        while (prod_t <= now) {
            long due = acl_pace_next(&clock, dur, now);
            if (qn == QCAP) { memmove(q_due, q_due + 1, (QCAP - 1) * sizeof *q_due); qn--; drops++; }
            q_due[qn++] = due;
            produced++;
            prod_t += pkt_ms;
        }
        (void)next_prod;
        /* controller completes */
        for (i = 0; i < nfin; ) {
            if (fin[i] <= now) { completed_unreported++; fin[i] = fin[--nfin]; } else i++;
        }
        if (evt_every && now % evt_every == 0 && completed_unreported) {
            if (lose_every && now % lose_every < evt_every && now > 1000)
                ;                       /* this event never reaches us */
            else
                acl_pool_credit(&p, completed_unreported, 1, now);
            completed_unreported = 0;
        }
        if (qn && p.outstanding >= p.limit) { if (!stall_from) stall_from = now; }
        else if (stall_from) { if (now - stall_from > max_stall) max_stall = now - stall_from; stall_from = 0; }
        acl_pool_fallback(&p, now);
        if (p.outstanding < 0) { printf("FAIL %s: outstanding < 0\n", name); fails++; return 0; }
        /* send */
        while (qn && p.outstanding < p.limit && q_due[0] <= now && nfin < MAXF) {
            acl_pool_sent(&p, now);
            radio_free = (radio_free > now ? radio_free : now) + done_ms;
            fin[nfin++] = radio_free;
            memmove(q_due, q_due + 1, (size_t)(qn - 1) * sizeof *q_due);
            qn--;
        }
    }
    pps = p.sent / (double)secs;
    {
        double need = 1000.0 / pkt_ms;
        int ok = pps >= need * 0.98 && drops == 0 && (!lose_every || max_stall <= 200);
        printf("%s %-36s sent %.1f/s need %.1f/s drops %lu evt %lu fb %lu longest stall %ld ms\n",
               ok == expect_ok ? "ok  " : "FAIL", name, pps, need, drops, p.cred_evt, p.cred_fb, max_stall);
        if (ok != expect_ok) fails++;
    }
    return pps;
}

int main(void)
{
    run("NOCP every 10 ms, 8 ms/packet", 10, 8, 20, 11, 1);
    run("NOCP every 50 ms batches", 50, 8, 20, 11, 1);
    run("NOCP every 100 ms (sniff-like)", 100, 5, 20, 11, 1);
    run("NOCP every 50 ms, 7 frames/packet", 50, 8, 20, 7, 1);
    run("slow radio 40 ms/packet (cannot keep up)", 10, 40, 20, 11, 0);
    /* A completion event lost every 3 s (another reader of the event pipe):
     * the refill must step in after this link's own rhythm, not 400 ms. */
    lose_every = 3000;
    run("NOCP every 20 ms, one lost every 3 s", 20, 8, 30, 15, 1);
    run("NOCP every 50 ms, one lost every 3 s", 50, 8, 30, 15, 1);
    lose_every = 0;
    /* No events at all: the timer must keep it moving (not full rate). */
    {
        acl_pool p; memset(&p, 0, sizeof p); p.limit = 6;
        long t; for (t = 0; t < 6; t++) acl_pool_sent(&p, 0);
        for (t = 0; t < 2000; t++) acl_pool_fallback(&p, t);
        if (p.outstanding != 0) { printf("FAIL fallback did not refill (%d)\n", p.outstanding); fails++; }
        else printf("ok   fallback refills when no NOCP arrives\n");
        /* Late real events after fallback must never go negative or be swallowed. */
        for (t = 0; t < 3; t++) acl_pool_sent(&p, 2000);
        acl_pool_credit(&p, 3, 1, 2001);
        if (p.outstanding != 0) { printf("FAIL real completions not freed (%d)\n", p.outstanding); fails++; }
        else printf("ok   real completions always free credits\n");
    }
    return fails ? 1 : 0;
}
