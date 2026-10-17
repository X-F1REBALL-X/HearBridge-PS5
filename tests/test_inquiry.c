/* Real a2dp_inquiry + hci_cmd against a deterministic controller. No USB,
 * console, kernel helpers, or wall-clock waits. Synthetic atom is a fixture. */
#include "a2dp.h"
#include "hci_cmd.h"
#include <stdio.h>
#include <string.h>

static long tick, start, result_after;
static int masked, result_sent, active, qlen, masks, inquiries, slots;
static int setup_delay, mode, abort_now, mask_fails, lost_complete, forbidden;
static unsigned char queue[260];
static int failures;
#define CHECK(c, s) do { if (!(c)) { printf("FAIL %s\n", s); failures++; } else printf("ok   %s\n", s); } while (0)

long now_ms(void) { return tick; }
void log_line(const char *fmt, ...) { (void)fmt; }
int hb_block_has(const unsigned char *addr) { (void)addr; return 0; }
static int abort_scan(void) { return abort_now; }

static int cmd(void *ctx, unsigned op, const void *params, int len)
{
    const unsigned char *p = params;
    (void)ctx;
    memset(queue, 0, sizeof queue);
    queue[0] = 0x0e; queue[1] = 4; queue[2] = 1;
    queue[3] = (unsigned char)op; queue[4] = (unsigned char)(op >> 8);
    qlen = 6;
    switch (op) {
    case HB_OP_READ_BUFFER_SIZE:
        qlen = 13; queue[6] = 0xfd; queue[7] = 3; queue[9] = 8; break;
    case HB_OP_READ_BD_ADDR: qlen = 12; queue[6] = 1; break;
    case HB_OP_READ_SSP_MODE: qlen = 7; queue[6] = 1; break;
    case HB_OP_SET_EVENT_MASK:
        masks++;
        if (mask_fails) queue[5] = 0x0c;
        else if (len == 8 && (p[5] & 0x40)) masked = 0; /* EIR bit 46 */
        break;
    case HB_OP_READ_SCAN_ENABLE: qlen = 7; queue[6] = 2; break;
    case HB_OP_WRITE_SCAN_ENABLE:
        if (len != 1 || !(p[0] & 2)) forbidden++;
        break;
    case 0x0c44:
        tick += setup_delay; qlen = 7; queue[6] = (unsigned char)mode; break;
    case 0x0c45: mode = p[0]; break;
    case HB_OP_INQUIRY:
        inquiries++; slots = p[3]; start = tick; active = 1; result_sent = 0;
        queue[0] = 0x0f; queue[1] = 4; queue[2] = 0; queue[3] = 1;
        queue[4] = (unsigned char)op; queue[5] = (unsigned char)(op >> 8);
        break;
    case HB_OP_INQUIRY_CANCEL: active = 0; break;
    case 0x0c03: forbidden++; break; /* reset must never be used */
    }
    if (queue[0] == 0x0e) queue[1] = (unsigned char)(qlen - 2);
    return 1;
}
static int event(void *ctx, unsigned char *out, int cap)
{
    int n;
    (void)ctx;
    if (qlen) {
        n = qlen; qlen = 0;
        if (n > cap) return -1;
        memcpy(out, queue, n); return n;
    }
    if (!active) return 0;
    if (!masked && !result_sent && tick - start >= result_after && tick - start < slots * 1280L) {
        if (cap < 257) return -1;
        memset(out, 0, 257);
        out[0] = 0x2f; out[1] = 255; out[2] = 1;
        out[3] = 0xaa; out[8] = 0xbb; out[9] = 1;
        out[11] = 0x18; out[12] = 0x04; out[13] = 0x20; /* headphones */
        out[16] = (unsigned char)-35;
        out[17] = 5; out[18] = 9; memcpy(out + 19, "atom", 4);
        result_sent = 1; return 257;
    }
    if (tick - start >= slots * 1280L && !lost_complete) {
        active = 0; out[0] = 1; out[1] = 1; out[2] = 0; return 3;
    }
    return 0;
}
static int none(void *ctx, unsigned char *out, int cap) { (void)ctx; (void)out; (void)cap; return 0; }
static int pump(void *ctx, int ms) { (void)ctx; tick += ms > 0 ? ms : 1; return 1; }
static int send_acl(void *ctx, const unsigned char *p, int n) { (void)ctx; (void)p; (void)n; return 1; }
static const hci_ops ops = { none, event, pump, cmd, send_acl, NULL, NULL };
static int context;

static a2dp_session *session(int mediatek)
{
    hci_t h = { &context, &ops };
    a2dp_open_opts opts = { {0}, 3 };
    a2dp_session *s;
    tick = 100; active = qlen = inquiries = masks = setup_delay = abort_now = 0;
    masked = result_sent = mask_fails = lost_complete = forbidden = 0;
    mode = 2; result_after = 100;
    a2dp_inquiry_abort = abort_scan;
    a2dp_scan_deadline_ms = 0; a2dp_scan_evmask_sent = 0;
    hci_set_mediatek(mediatek); hci_scan_pause_enable(mediatek);
    s = a2dp_open(h, &opts);
    CHECK(s != NULL, "session opens with real command helper");
    masks = 0;
    a2dp_scan_deadline_ms = tick + 12000;
    return s;
}

int main(void)
{
    a2dp_session *s;
    a2dp_inq_dev d[4]; int n;
    s = session(1); masked = 1;
    CHECK(a2dp_inquiry(s, d, 4, &n) && n == 1, "mask changed after startup: device still discovered");
    CHECK(n == 1 && !strcmp(d[0].name, "atom") && d[0].audio_likely, "real EIR parser preserves atom name and audio class");
    CHECK(masks == 1 && !forbidden, "one mask refresh, no reset or disabling page scan");
    a2dp_scan_deadline_ms = tick + 12000;
    a2dp_inquiry(s, d, 4, &n);
    CHECK(masks == 1, "no repeated mask write within same manual scan");
    a2dp_scan_evmask_sent = 0; a2dp_scan_deadline_ms = tick + 12000;
    a2dp_inquiry(s, d, 4, &n);
    CHECK(masks == 2, "next manual scan refreshes independently"); a2dp_close(s);

    s = session(1); result_after = 6000;
    CHECK(a2dp_inquiry(s, d, 4, &n) && n == 1, "headset answering after six seconds is discovered");
    CHECK(slots == 8 && inquiries == 1, "one uninterrupted 10.24-second inquiry"); a2dp_close(s);

    s = session(0);
    CHECK(a2dp_inquiry(s, d, 4, &n) && n == 1 && slots == 2 && masks == 0, "default controller retains original duration and mask behavior"); a2dp_close(s);

    s = session(1); a2dp_scan_deadline_ms = 0;
    CHECK(a2dp_inquiry(s, d, 4, &n) && n == 1 && slots == 2 && masks == 0, "non-manual MediaTek inquiry retains existing policy"); a2dp_close(s);

    s = session(1); setup_delay = 900; a2dp_scan_deadline_ms = tick + 3300;
    a2dp_inquiry(s, d, 4, &n);
    CHECK(slots == 1 && tick <= a2dp_scan_deadline_ms + 500, "slow setup shortens inquiry using remaining budget"); a2dp_close(s);

    s = session(1); setup_delay = 900; mode = 1; a2dp_scan_deadline_ms = tick + 2000;
    CHECK(a2dp_inquiry(s, d, 4, &n) && inquiries == 0 && n == 0 && mode == 1, "expired setup skips inquiry and restores original inquiry mode"); a2dp_close(s);

    s = session(1); abort_now = 1;
    CHECK(a2dp_inquiry(s, d, 4, &n) && n == 0 && masks == 0 && inquiries == 0, "pending stop prevents scan commands"); a2dp_close(s);

    s = session(1); masked = mask_fails = lost_complete = 1;
    CHECK(a2dp_inquiry(s, d, 4, &n) && n == 0, "mask failure does not fabricate devices");
    CHECK(!active && tick <= a2dp_scan_deadline_ms + 1700 && masks == 1, "lost completion cancels inquiry with bounded recovery"); a2dp_close(s);
    printf("%d failure(s)\n", failures);
    return failures != 0;
}
