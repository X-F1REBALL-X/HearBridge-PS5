/* hci_usb.c - USB HCI transport (Core spec Vol 4 Part B) over ugen(4).
 *
 * Design notes
 *  - The ugen "fs" interface gives us a table of transfer slots. Each slot
 *    is bound to one endpoint and carries one transfer at a time, so several
 *    slots on the same endpoint give several reads in flight.
 *  - Commands use the synchronous USB_DO_REQUEST ioctl on endpoint 0.
 *  - Received packets land in fixed rings; when a ring is full the oldest
 *    entry is overwritten so fresh traffic is never lost to stale traffic.
 *  - Outgoing ACL frames wait in a ring as well and are handed to the bulk
 *    OUT slot one at a time. A frame that makes no progress for
 *    STALL_SWITCH_MS moves the transmitter onto the spare bulk OUT pipe. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <dirent.h>

#include <ps5/kernel.h>

#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>

#include "acl_track.h"
#include "hci_usb.h"
#include "hcidbg.h"
#include "usb_hci_desc.h"
#include "hci_cmd.h"
#include "log.h"
#include "diag.h"
#include "util.h"
#include "stop.h"
#include "btchip.h"

enum {
    RING_DEPTH      = 64,
    /* The system stack reads the same endpoints; whoever has a transfer
     * pending gets the packet. Few reads in flight (1.0.0-1.0.2 used 4/4)
     * let it take our L2CAP signalling (e.g. the peer's CFG_REQ). Earlier
     * working builds kept 40 event / 20 ACL reads in flight. Pending
     * transfers are served in order, so the system still takes about
     * 1/(ours + 1) of the events (~2.7% measured on a MediaTek 0e8d:3603;
     * lost Number Of Completed Packets are covered by the timed refill).
     * 62 slots is the ceiling: on fw 13.60 the 63rd FS_OPEN fails with
     * ENOMEM, so more event reads mean fewer ACL reads. */
    READS_EVT       = 40,
    READS_ACL       = 20,
    STALL_SWITCH_MS = 500,
    OP_DISCONNECT   = 0x0406,
    DEF_EVT_EP      = 0x82,
    DEF_ACLIN_EP    = 0x81,
    DEF_ACLOUT_EP   = 0x01,
    DEF_MPS         = 16,
    EVT_BUF         = 260,           /* 2-byte header + 255 parameters */
};

/* Slot table layout: event reads, then ACL reads, then the two writers. */
#define SLOT_EVT0   0
#define SLOT_ACL0   (SLOT_EVT0 + READS_EVT)
#define SLOT_OUT    (SLOT_ACL0 + READS_ACL)
#define SLOT_SPARE  (SLOT_OUT + 1)
#define SLOT_COUNT  (SLOT_SPARE + 1)

struct pkt { uint16_t len; unsigned char data[HCI_PKT_MAX]; };

struct ring {
    struct pkt item[RING_DEPTH];
    unsigned   head, count;   /* head = oldest */
    unsigned   overwritten;
};

struct slot {
    int       open, busy;
    uint8_t   addr;
    void     *bufp;
    uint32_t  len;
    unsigned char buf[HCI_PKT_MAX];
};

void (*hci_usb_conn_req_hook)(hci_t hci, const unsigned char *ev, int n);

/* Connection Requests seen in one reap, answered after it (commands are not
 * sent from inside the completion loop). */
#define CREQ_MAX 4
static unsigned char g_creq[CREQ_MAX][16];
static int g_ncreq;

struct usb_hci {
    int    fd;
    int    dead;
    char   node[32];
    struct usbhci_iface ifc;
    struct usb_fs_endpoint fsep[SLOT_COUNT];
    struct slot sl[SLOT_COUNT];
    int    tx_slot;              /* SLOT_OUT or SLOT_SPARE */
    long   tx_since;             /* when the in-flight frame was started */
    struct ring evq, aclq, txq;
    /* counters for diag */
    unsigned long n_evt, n_acl_in, n_acl_out, n_cmd, n_err, n_switch, n_rearm;
    int    last_status;
};

/* ---- rings ------------------------------------------------------------ */

static void ring_put(struct ring *r, const unsigned char *p, int n)
{
    struct pkt *dst;
    if (n <= 0) return;
    if (n > HCI_PKT_MAX) n = HCI_PKT_MAX;
    if (r->count == RING_DEPTH) {            /* evict the oldest */
        r->head = (r->head + 1) % RING_DEPTH;
        r->count--;
        r->overwritten++;
    }
    dst = &r->item[(r->head + r->count) % RING_DEPTH];
    dst->len = (uint16_t)n;
    memcpy(dst->data, p, (size_t)n);
    r->count++;
}

static const struct pkt *ring_peek(const struct ring *r)
{
    return r->count ? &r->item[r->head] : NULL;
}

static void ring_drop(struct ring *r)
{
    if (!r->count) return;
    r->head = (r->head + 1) % RING_DEPTH;
    r->count--;
}

static int ring_take(struct ring *r, unsigned char *dst, int cap)
{
    const struct pkt *p = ring_peek(r);
    int n;
    if (!p || !dst || cap <= 0) return 0;
    n = p->len < cap ? p->len : cap;
    memcpy(dst, p->data, (size_t)n);
    ring_drop(r);
    return n;
}

/* ---- ugen fs plumbing --------------------------------------------------- */

static int slot_bind(struct usb_hci *u, int idx, uint8_t addr)
{
    struct usb_fs_open op;
    memset(&op, 0, sizeof op);
    op.ep_index   = (uint8_t)idx;
    op.ep_no      = addr;
    op.max_bufsize = HCI_PKT_MAX;
    op.max_frames = 1;
    if (ioctl(u->fd, USB_FS_OPEN, &op) < 0) {
        log_line("hci_usb: FS_OPEN slot %d ep 0x%02x: %s", idx, addr,
                 strerror(errno));
        return 0;
    }
    u->sl[idx].open = 1;
    u->sl[idx].addr = addr;
    u->sl[idx].bufp = u->sl[idx].buf;
    u->fsep[idx].ppBuffer = &u->sl[idx].bufp;
    u->fsep[idx].pLength  = &u->sl[idx].len;
    u->fsep[idx].nFrames  = 1;
    return 1;
}

static int slot_go(struct usb_hci *u, int idx, uint32_t len, uint16_t flags)
{
    struct usb_fs_start st;
    struct slot *s = &u->sl[idx];
    if (!s->open || s->busy) return 0;
    s->len = len;
    u->fsep[idx].nFrames = 1;
    u->fsep[idx].aFrames = 0;
    u->fsep[idx].flags   = flags;
    u->fsep[idx].timeout = USB_FS_TIMEOUT_NONE;
    u->fsep[idx].status  = 0;
    st.ep_index = (uint8_t)idx;
    if (ioctl(u->fd, USB_FS_START, &st) < 0) {
        u->n_err++;
        if (errno == ENXIO || errno == ENODEV) u->dead = 1;
        return 0;
    }
    s->busy = 1;
    return 1;
}

static void slot_abort(struct usb_hci *u, int idx)
{
    struct usb_fs_stop sp;
    if (!u->sl[idx].busy) return;
    sp.ep_index = (uint8_t)idx;
    (void)ioctl(u->fd, USB_FS_STOP, &sp);
    u->sl[idx].busy = 0;
}

static void arm_read(struct usb_hci *u, int idx)
{
    (void)slot_go(u, idx, HCI_PKT_MAX, USB_FS_FLAG_SINGLE_SHORT_OK);
}

/* Hand the oldest queued ACL frame to the active writer, if it is idle. */
static void tx_kick(struct usb_hci *u)
{
    const struct pkt *p;
    struct slot *w = &u->sl[u->tx_slot];
    if (w->busy || !(p = ring_peek(&u->txq))) return;
    memcpy(w->buf, p->data, p->len);
    if (slot_go(u, u->tx_slot, p->len, USB_FS_FLAG_FORCE_SHORT))
        u->tx_since = now_ms();
}

static void tx_watchdog(struct usb_hci *u)
{
    if (!u->sl[u->tx_slot].busy) return;
    if (now_ms() - u->tx_since < STALL_SWITCH_MS) return;
    if (u->tx_slot != SLOT_OUT || !u->sl[SLOT_SPARE].open) return;
    log_line("hci_usb: bulk OUT 0x%02x stuck %d ms, moving to 0x%02x",
             u->sl[SLOT_OUT].addr, STALL_SWITCH_MS, u->sl[SLOT_SPARE].addr);
    slot_abort(u, SLOT_OUT);
    u->tx_slot = SLOT_SPARE;
    u->n_switch++;
    tx_kick(u);   /* the same frame is still at the head of txq */
}

/* Drain every finished transfer. Returns 1 if new input was queued. */
static int reap(struct usb_hci *u)
{
    struct usb_fs_complete c;
    int got = 0;
    for (;;) {
        int idx, st;
        struct slot *s;
        memset(&c, 0, sizeof c);
        if (ioctl(u->fd, USB_FS_COMPLETE, &c) < 0) {
            if (errno == ENXIO || errno == ENODEV) u->dead = 1;
            break;                          /* EBUSY: nothing more */
        }
        idx = c.ep_index;
        if (idx >= SLOT_COUNT) continue;
        s = &u->sl[idx];
        s->busy = 0;
        st = u->fsep[idx].status;
        if (st) { u->n_err++; u->last_status = st; }

        if (idx >= SLOT_EVT0 && idx < SLOT_ACL0) {
            if (!st && s->len >= 2) {
                acl_track_event(s->buf, (int)s->len, now_ms());   /* every link, any owner */
                if (s->buf[0] == 0x04 && s->len >= 12 && g_ncreq < CREQ_MAX)
                    memcpy(g_creq[g_ncreq++], s->buf, 12);
                ring_put(&u->evq, s->buf, (int)s->len); u->n_evt++; got = 1;
            }
            arm_read(u, idx);
        } else if (idx >= SLOT_ACL0 && idx < SLOT_OUT) {
            if (!st && s->len >= 4) { ring_put(&u->aclq, s->buf, (int)s->len); u->n_acl_in++; got = 1; }
            arm_read(u, idx);
        } else if (idx == u->tx_slot) {
            if (!st) { ring_drop(&u->txq); u->n_acl_out++; }
            else ring_drop(&u->txq);       /* do not spin on a bad frame */
            tx_kick(u);
        }
    }
    return got;
}

/* ---- hci_ops ------------------------------------------------------------ */

static int op_next_event(void *self, unsigned char *dst, int cap)
{
    int n = ring_take(&((struct usb_hci *)self)->evq, dst, cap);
    if (n > 0) hcidbg_event(dst, n);
    return n;
}

static int op_next_acl(void *self, unsigned char *dst, int cap)
{
    return ring_take(&((struct usb_hci *)self)->aclq, dst, cap);
}

static const hci_ops usb_ops;

/* Answer (or hand over) every Connection Request read so far, and give the
 * hook its periodic tick (ev NULL). */
static void creq_flush(struct usb_hci *u)
{
    hci_t h;
    int i, n = g_ncreq;
    static int busy;
    if (!hci_usb_conn_req_hook || busy) return;   /* no recursion from the hook's own pumps */
    h.ctx = u;
    h.ops = &usb_ops;
    busy = 1;
    g_ncreq = 0;
    for (i = 0; i < n; i++) hci_usb_conn_req_hook(h, g_creq[i], 12);
    hci_usb_conn_req_hook(h, NULL, 0);
    busy = 0;
}

static int op_pump(void *self, int wait_ms)
{
    struct usb_hci *u = self;
    long until = now_ms() + (wait_ms > 0 ? wait_ms : 0);
    if (u->dead) return -1;
    /* Stop file / signal: report "transport gone" so every loop unwinds
     * to its cleanup path (links closed, USB released). */
    if (hb_stop_requested()) return -1;
    for (;;) {
        struct pollfd pf;
        long left;
        int got = reap(u), k;
        creq_flush(u);
        /* A read whose re-arm failed (EBUSY/transient error) would leave
         * the event or ACL pipe silent for good: re-arm idle readers. */
        for (k = SLOT_EVT0; k < SLOT_OUT; k++)
            if (u->sl[k].open && !u->sl[k].busy) {
                arm_read(u, k);
                if (u->sl[k].busy && !(u->n_rearm++ % 50))
                    log_line("hci_usb: re-armed idle %s read slot %d (%lu so far)",
                             k < SLOT_ACL0 ? "event" : "ACL", k, u->n_rearm);
            }
        tx_watchdog(u);
        tx_kick(u);
        if (u->dead) return -1;
        if (got || u->evq.count || u->aclq.count) return 1;
        left = until - now_ms();
        if (left <= 0) return 0;
        if (left > 50) left = 50;         /* keep the watchdog ticking */
        pf.fd = u->fd; pf.events = POLLIN | POLLOUT | POLLRDNORM; pf.revents = 0;
        if (poll(&pf, 1, (int)left) < 0 && errno != EINTR) { u->dead = 1; return -1; }
        if (pf.revents & (POLLHUP | POLLERR | POLLNVAL)) { u->dead = 1; return -1; }
    }
}

static int op_cmd(void *self, unsigned op, const void *args, int nargs)
{
    struct usb_hci *u = self;
    unsigned char pkt[3 + 255];
    struct usb_ctl_request rq;
    if (u->dead || nargs < 0 || nargs > 255) return 0;
    hcidbg_cmd(op, args, nargs);
    put16(pkt, op);
    pkt[2] = (unsigned char)nargs;
    if (nargs) memcpy(pkt + 3, args, (size_t)nargs);

    /* Setup stage per Vol 4 Part B 2.2: class, host-to-device, request 0. */
    memset(&rq, 0, sizeof rq);
    {
        unsigned char *setup = (unsigned char *)&rq.ucr_request;
        setup[0] = UT_WRITE_CLASS_DEVICE;            /* 0x20 */
        put16(setup + 4, (unsigned)u->ifc.number);   /* wIndex */
        put16(setup + 6, (unsigned)(3 + nargs));     /* wLength */
    }
    rq.ucr_data = pkt;
    if (ioctl(u->fd, USB_DO_REQUEST, &rq) < 0) {
        int e = errno;
        u->n_err++;
        /* Disconnect of a link that already went away often times out on
         * the control pipe; the controller is fine, so carry on. */
        if (op == OP_DISCONNECT && (e == EIO || e == ETIMEDOUT)) {
            log_line("hci_usb: disconnect request: %s (ignored)", strerror(e));
            return 1;
        }
        log_line("hci_usb: command 0x%04x failed: %s", op, strerror(e));
        if (e == ENXIO || e == ENODEV || e == EIO) u->dead = 1;
        return 0;
    }
    u->n_cmd++;
    return 1;
}

static int op_acl_send(void *self, const unsigned char *frame, int nbytes)
{
    struct usb_hci *u = self;
    if (u->dead || !frame || nbytes < 4 || nbytes > HCI_PKT_MAX) return 0;
    ring_put(&u->txq, frame, nbytes);
    tx_kick(u);
    return 1;
}

static void op_diag(void *self)
{
    const struct usb_hci *u = self;
    log_line("hci_usb diag: %s if=%d evt=0x%02x in=0x%02x out=0x%02x spare=0x%02x tx=%s%s",
             u->node, u->ifc.number, u->ifc.evt_ep, u->ifc.in_ep, u->ifc.out_ep,
             u->ifc.spare_out_ep, u->tx_slot == SLOT_SPARE ? "spare" : "primary",
             u->dead ? " DEAD" : "");
    log_line("hci_usb diag: cmd=%lu evt=%lu acl_in=%lu acl_out=%lu err=%lu last=%d switch=%lu",
             u->n_cmd, u->n_evt, u->n_acl_in, u->n_acl_out, u->n_err,
             u->last_status, u->n_switch);
    log_line("hci_usb diag: queued evt=%u acl=%u tx=%u lost evt=%u acl=%u tx=%u",
             u->evq.count, u->aclq.count, u->txq.count,
             u->evq.overwritten, u->aclq.overwritten, u->txq.overwritten);
}

/* The open transport, so it is released on any exit path (atexit). */
static struct usb_hci *g_open;

static void op_close(void *self)
{
    struct usb_hci *u = self;
    struct usb_fs_uninit un;
    int i;
    if (!u) return;
    if (g_open == u) g_open = NULL;
    if (u->fd >= 0) {
        for (i = 0; i < SLOT_COUNT; i++) {
            struct usb_fs_close cl;
            slot_abort(u, i);
            if (!u->sl[i].open) continue;
            cl.ep_index = (uint8_t)i;
            (void)ioctl(u->fd, USB_FS_CLOSE, &cl);
        }
        memset(&un, 0, sizeof un);
        (void)ioctl(u->fd, USB_FS_UNINIT, &un);
        close(u->fd);
    }
    log_line("hci_usb: released %s", u->node);
    free(u);
}

static void release_at_exit(void)
{
    if (g_open) op_close(g_open);
}

static const hci_ops usb_ops = {
    .next_acl   = op_next_acl,
    .next_event = op_next_event,
    .pump       = op_pump,
    .cmd        = op_cmd,
    .acl_send   = op_acl_send,
    .diag       = op_diag,
    .close      = op_close,
};

/* ---- discovery ------------------------------------------------------------ */

static int read_config(int fd, uint8_t *buf, int cap)
{
    struct usb_gen_descriptor gd;
    memset(&gd, 0, sizeof gd);
    gd.ugd_data = buf;
    gd.ugd_maxlen = (uint16_t)cap;
    gd.ugd_config_index = 0;
    if (ioctl(fd, USB_GET_FULL_DESC, &gd) < 0) return 0;
    return gd.ugd_actlen;
}

static void pick_endpoints(int fd, struct usbhci_iface *ifc)
{
    static uint8_t cfg[1024];
    struct usbhci_iface found[USBHCI_MAX_IFACES];
    int n = read_config(fd, cfg, (int)sizeof cfg);
    int k = n > 0 ? usbhci_scan(cfg, n, found) : 0;

    memset(ifc, 0, sizeof *ifc);
    if (k > 0) *ifc = found[0];
    if (!ifc->evt_ep) { ifc->evt_ep = DEF_EVT_EP;    ifc->evt_mps = DEF_MPS; }
    if (!ifc->in_ep)  { ifc->in_ep  = DEF_ACLIN_EP;  ifc->in_mps  = DEF_MPS; }
    if (!ifc->out_ep) { ifc->out_ep = DEF_ACLOUT_EP; ifc->out_mps = DEF_MPS; }
    if (!ifc->evt_mps) ifc->evt_mps = DEF_MPS;
    if (!ifc->in_mps)  ifc->in_mps  = DEF_MPS;
    if (!ifc->out_mps) ifc->out_mps = DEF_MPS;
    log_line("hci_usb: %s descriptor (%d HCI iface), evt 0x%02x in 0x%02x out 0x%02x",
             k > 0 ? "using" : "fallback, no", k, ifc->evt_ep, ifc->in_ep, ifc->out_ep);
}

/* VID:PID of the controller hci_usb_open() picked (-1 until then). */
static int g_chip_vid = -1, g_chip_pid = -1;

int hci_usb_chip(int *vid, int *pid)
{
    if (vid) *vid = g_chip_vid;
    if (pid) *pid = g_chip_pid;
    return g_chip_vid >= 0;
}

/* "1286:2059 \"product\" by \"vendor\"" for an open ugen fd (read-only
 * ioctls: nothing is sent to the device). vid/pid (may be NULL) get the
 * IDs, or -1 when the device descriptor cannot be read. */
static void device_id(int fd, char *out, size_t cap, int *vid, int *pid)
{
    struct usb_device_descriptor dd;
    struct usb_device_info di;
    int n = 0;

    memset(&dd, 0, sizeof dd);
    memset(&di, 0, sizeof di);
    if (vid) *vid = -1;
    if (pid) *pid = -1;
    if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) == 0) {
        n = snprintf(out, cap, "%04x:%04x class %02x", UGETW(dd.idVendor),
                     UGETW(dd.idProduct), dd.bDeviceClass);
        if (vid) *vid = UGETW(dd.idVendor);
        if (pid) *pid = UGETW(dd.idProduct);
    } else
        n = snprintf(out, cap, "????:???? (device descriptor errno %d)", errno);
    if (n > 0 && (size_t)n < cap && ioctl(fd, USB_GET_DEVICEINFO, &di) == 0)
        snprintf(out + n, cap - (size_t)n, " \"%.40s\" by \"%.40s\"",
                 di.udi_product, di.udi_vendor);
}

static int try_node(struct usb_hci *u, const char *path)
{
    struct usb_fs_init in;
    char id[128], chip[64];
    int i, vid, pid, mtk;
    u->fd = open(path, O_RDWR);
    if (u->fd < 0) {
        log_line("hci_usb: open %s: errno %d", path, errno);
        return 0;
    }
    snprintf(u->node, sizeof u->node, "%s", path);
    device_id(u->fd, id, sizeof id, &vid, &pid);
    log_line("usb: %s is %s", path, id);
    pick_endpoints(u->fd, &u->ifc);
    mtk = vid == 0x0E8D;                  /* MediaTek (PS5 Pro) */
    if (mtk) {
        usbhci_pair_out_with_in(&u->ifc);
        log_line("hci_usb: MediaTek controller: ACL out 0x%02x, system scan paused during page/scan",
                 u->ifc.out_ep);
    }

    memset(&in, 0, sizeof in);
    in.pEndpoints = u->fsep;
    in.ep_index_max = SLOT_COUNT;
    if (ioctl(u->fd, USB_FS_INIT, &in) < 0) goto fail;

    for (i = 0; i < READS_EVT; i++)
        if (!slot_bind(u, SLOT_EVT0 + i, u->ifc.evt_ep)) goto fail;
    for (i = 0; i < READS_ACL; i++)
        if (!slot_bind(u, SLOT_ACL0 + i, u->ifc.in_ep)) goto fail;
    if (!slot_bind(u, SLOT_OUT, u->ifc.out_ep)) goto fail;
    if (u->ifc.spare_out_ep) (void)slot_bind(u, SLOT_SPARE, u->ifc.spare_out_ep);

    for (i = SLOT_EVT0; i < SLOT_OUT; i++) arm_read(u, i);
    u->tx_slot = SLOT_OUT;
    hci_scan_pause_enable(mtk);
    hci_set_mediatek(mtk);
    log_line("hci_usb: opened %s (%d event / %d ACL reads in flight)", path,
             READS_EVT, READS_ACL);
    diag_set("bt controller", "%s %s; HCI iface %d evt 0x%02x in 0x%02x out 0x%02x", path, id,
             u->ifc.number, u->ifc.evt_ep, u->ifc.in_ep, u->ifc.out_ep);
    g_chip_vid = vid;
    g_chip_pid = pid;
    btchip_describe(vid, pid, chip, (int)sizeof chip);
    diag_set("bt chip", "%s", chip[0] ? chip : "unknown (no device descriptor)");
    log_line("hci_usb: chip %s", chip[0] ? chip : "unknown");
    return 1;
fail:
    log_line("hci_usb: %s unusable: %s", path, strerror(errno));
    diag_set("bt controller", "%s %s unusable (errno %d)", path, id, errno);
    close(u->fd);
    u->fd = -1;
    memset(u->sl, 0, sizeof u->sl);
    memset(u->fsep, 0, sizeof u->fsep);
    return 0;
}

int hci_usb_open(hci_t *out)
{
    static const char *const nodes[] = {
        "/dev/ugen0.2", "/dev/ugen0.3", "/dev/ugen1.2", "/dev/ugen0.1",
    };
    struct usb_hci *u;
    size_t i;
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    u = calloc(1, sizeof *u);
    if (!u) return 0;
    u->fd = -1;
    for (i = 0; i < sizeof nodes / sizeof nodes[0]; i++) {
        if (try_node(u, nodes[i])) {
            static int hooked;
            out->ctx = u;
            out->ops = &usb_ops;
            g_open = u;
            if (!hooked) { atexit(release_at_exit); hooked = 1; }
            return 1;
        }
    }
    free(u);
    diag_set("bt controller", "NOT FOUND: none of /dev/ugen0.2, 0.3, 1.2, 0.1 could be used");
    return 0;
}

/* ---- diagnostics: list every ugen device ---------------------------------- */

static void survey_one(const char *path, int *count)
{
    static uint8_t cfg[1024];
    char id[128], desc[DIAG_VAL_MAX - 160], key[40];
    struct usb_gen_descriptor gd;
    int fd = open(path, O_RDONLY), n;

    if (fd < 0) fd = open(path, O_RDWR);
    snprintf(key, sizeof key, "usb %s", path);
    if (fd < 0) {
        log_line("usb: %s: open errno %d", path, errno);
        diag_set(key, "cannot open (errno %d)", errno);
        (*count)++;
        return;
    }
    device_id(fd, id, sizeof id, NULL, NULL);
    memset(&gd, 0, sizeof gd);
    gd.ugd_data = cfg;
    gd.ugd_maxlen = (uint16_t)sizeof cfg;
    gd.ugd_config_index = 0xFF;                 /* the current configuration */
    n = ioctl(fd, USB_GET_FULL_DESC, &gd) == 0 ? gd.ugd_actlen : 0;
    if (n > (int)sizeof cfg) n = (int)sizeof cfg;
    if (n > 0) usbhci_describe(cfg, n, desc, sizeof desc);
    else snprintf(desc, sizeof desc, "configuration descriptor unavailable (errno %d)", errno);
    close(fd);
    log_line("usb: %s is %s: %s", path, id, desc);
    diag_set(key, "%s: %s", id, desc);
    (*count)++;
}

/* opendir/readdir/closedir were not imported by 1.0.2. Every import is
 * bound by the payload runtime before main() runs, and one that does not
 * bind ends the payload silently (no notification, no log). So these are
 * looked up here, at run time, and the survey falls back to known node
 * names when they are missing. */
typedef DIR *(*opendir_fn)(const char *);
typedef struct dirent *(*readdir_fn)(DIR *);
typedef int (*closedir_fn)(DIR *);

static intptr_t libc_sym(const char *name)
{
    static const char *const libs[] = { "libSceLibcInternal.sprx", "libc.sprx" };
    size_t i;

    for (i = 0; i < sizeof libs / sizeof libs[0]; i++) {
        uint32_t h = 0;
        intptr_t a;
        if (kernel_dynlib_handle(-1, libs[i], &h) != 0 || !h) continue;
        if ((a = kernel_dynlib_dlsym(-1, h, name))) return a;
    }
    return 0;
}

static int list_ugen(char names[][32], int max)
{
    opendir_fn od = (opendir_fn)libc_sym("opendir");
    readdir_fn rd = (readdir_fn)libc_sym("readdir");
    closedir_fn cd = (closedir_fn)libc_sym("closedir");
    struct dirent *e;
    DIR *dir;
    int n = 0;

    if (!od || !rd || !cd) {
        log_line("usb: directory listing unavailable (opendir %p readdir %p closedir %p)",
                 (void *)od, (void *)rd, (void *)cd);
        return 0;
    }
    if (!(dir = od("/dev"))) return 0;
    while ((e = rd(dir)) && n < max)
        if (!strncmp(e->d_name, "ugen", 4))
            snprintf(names[n++], 32, "/dev/%.24s", e->d_name);
    cd(dir);
    return n;
}

int hci_usb_survey(void)
{
    static const char *const fallback[] = {
        "/dev/ugen0.1", "/dev/ugen0.2", "/dev/ugen0.3", "/dev/ugen1.1", "/dev/ugen1.2",
    };
    char names[24][32];
    int n = list_ugen(names, 24), count = 0, i, j;

    if (!n) {
        log_line("usb: no ugen entries listed in /dev (errno %d); trying known names", errno);
        for (i = 0; i < (int)(sizeof fallback / sizeof fallback[0]); i++)
            snprintf(names[n++], sizeof names[0], "%s", fallback[i]);
    }
    for (i = 1; i < n; i++)                       /* sort: stable report order */
        for (j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char t[32];
            memcpy(t, names[j], sizeof t);
            memcpy(names[j], names[j - 1], sizeof t);
            memcpy(names[j - 1], t, sizeof t);
        }
    for (i = 0; i < n; i++) survey_one(names[i], &count);
    diag_set("usb devices", "%d ugen node(s)", count);
    return count;
}
