/* Host test: USB transmit path after a link loss (hb9.log) and ACL credits
 * on Disconnection Complete. */
#include <stdio.h>
#include <string.h>
#include "txpath.h"
#include "acl_pool.h"

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

int main(void)
{
    static const unsigned char disc08[] = { 0x05, 0x04, 0x00, 0x02, 0x00, 0x08 };
    static const unsigned char discfail[] = { 0x05, 0x04, 0x0C, 0x02, 0x00, 0x08 };
    static const unsigned char nocp[] = { 0x13, 0x05, 0x01, 0x02, 0x00, 0x01, 0x00 };
    static const unsigned char nocp0[] = { 0x13, 0x05, 0x01, 0x02, 0x00, 0x00, 0x00 };
    static const unsigned char acl[] = { 0x02, 0x20, 0x04, 0x00, 0, 0, 0, 0 };
    hb_txpath t;
    acl_pool p;

    CHECK(hb_evt_disc_handle(disc08, sizeof disc08) == 2, "Disconnection Complete (reason 0x08) gives its handle");
    CHECK(hb_evt_disc_handle(discfail, sizeof discfail) < 0 && hb_evt_disc_handle(nocp, sizeof nocp) < 0,
          "failed disconnect / other events are not a disconnect");
    CHECK(hb_evt_is_nocp(nocp, sizeof nocp) && !hb_evt_is_nocp(nocp0, sizeof nocp0) && !hb_evt_is_nocp(disc08, sizeof disc08),
          "Number Of Completed Packets recognised (a zero count is not a completion)");
    CHECK(hb_acl_frame_handle(acl, sizeof acl) == 2 && hb_acl_frame_handle(acl, 3) < 0, "ACL frame handle (PB flags masked)");

    /* hb9.log: credits stop, bulk OUT 0x01 stalls, transmitter moves to 0x02,
     * which on this Marvell chip completes nothing: every later link dead. */
    hb_txpath_init(&t);
    CHECK(!hb_txpath_stall(&t, 0, 1000), "no spare pipe: stay on the primary");
    CHECK(hb_txpath_stall(&t, 1, 4340300) && t.on_spare, "primary stuck: try the spare");
    hb_txpath_sent(&t); hb_txpath_sent(&t); hb_txpath_sent(&t);
    CHECK(!hb_txpath_check(&t, 4340300 + 500), "spare gets its 1 s to prove itself");
    CHECK(hb_txpath_check(&t, 4340300 + HB_SPARE_PROVE_MS) && !t.on_spare && t.spare_bad,
          "spare completed nothing in 1 s: back to the primary, spare marked bad");
    CHECK(!hb_txpath_stall(&t, 1, 4350000) && !t.on_spare, "a bad spare is never used again");

    /* a spare that works (completions arrive) is kept */
    hb_txpath_init(&t);
    hb_txpath_stall(&t, 1, 0);
    hb_txpath_sent(&t); hb_txpath_nocp(&t);
    CHECK(!hb_txpath_check(&t, 5000) && t.on_spare, "a spare that gets completions stays in use");
    /* ...but a disconnect puts the next link back on the primary */
    CHECK(hb_txpath_disc(&t) && !t.on_spare && !t.spare_bad, "disconnect: next link starts on the primary");
    CHECK(!hb_txpath_disc(&t), "disconnect on the primary: nothing to do");
    CHECK(!hb_txpath_check(&t, 9000), "idle spare check does nothing on the primary");

    /* credits: 0x08 drop with packets the controller never completed */
    memset(&p, 0, sizeof p);
    p.limit = 6;
    {
        int i;
        for (i = 0; i < 6; i++) acl_pool_sent(&p, 4339800 + i);
    }
    CHECK(p.outstanding == 6, "link dying: 6/6 credits outstanding, no completions");
    CHECK(acl_pool_disconnected(&p, 4345491) == 6 && p.outstanding == 0,
          "Disconnection Complete returns every outstanding credit");
    acl_pool_sent(&p, 4345600);
    CHECK(p.outstanding == 1 && p.outstanding < p.limit, "the next packet can go out at once");
    CHECK(acl_pool_disconnected(&p, 4345700) == 1 && acl_pool_disconnected(&p, 4345800) == 0, "a second disconnect is harmless");

    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
