/* AVRCP absolute volume. Developed by X-F1REBALL-X. */
#include "avrcp.h"
#include "log.h"

#include <string.h>

#define AVRCP_PID      0x110E
/* AV/C command types / response codes */
#define CT_CONTROL     0x00
#define CT_STATUS      0x01
#define CT_NOTIFY      0x03
#define RSP_NOT_IMPL   0x08
#define RSP_ACCEPTED   0x09
#define RSP_REJECTED   0x0A
#define RSP_STABLE     0x0C
#define RSP_CHANGED    0x0D
#define RSP_INTERIM    0x0F
#define OP_VENDOR      0x00
#define OP_UNIT_INFO   0x30
#define OP_SUBUNIT     0x31
#define OP_PASSTHROUGH 0x7C
#define SUBUNIT_PANEL  0x48
/* AVRCP PDUs */
#define PDU_GET_CAPS   0x10
#define PDU_DISP_CHAR  0x17
#define PDU_BATTERY    0x18
#define PDU_REG_NOTIFY 0x31
#define PDU_SET_ABSVOL 0x50
#define EV_VOLUME      0x0D

void avrcp_init(avrcp_state *a, int volume)
{
    memset(a, 0, sizeof *a);
    a->volume = volume < 0 ? 0 : volume > 127 ? 127 : volume;
    a->notify_label = -1;
    a->our_label = 1;
}

static int hdr(unsigned char *o, int label, int cr, int ipid)
{
    o[0] = (unsigned char)(((label & 0x0F) << 4) | (cr ? 0x02 : 0) | (ipid ? 1 : 0));
    o[1] = (unsigned char)(AVRCP_PID >> 8);
    o[2] = (unsigned char)AVRCP_PID;
    return 3;
}

/* Vendor-dependent frame: AV/C ctype, panel, opcode 0, BT SIG company id,
 * PDU, packet type single, parameter length, params. */
static int vendor(unsigned char *o, int label, int cr, unsigned char ctype,
                  unsigned char pdu, const unsigned char *par, int np, int max)
{
    int n = hdr(o, label, cr, 0);
    if (n + 10 + np > max) return 0;
    o[n++] = ctype;
    o[n++] = SUBUNIT_PANEL;
    o[n++] = OP_VENDOR;
    o[n++] = 0x00; o[n++] = 0x19; o[n++] = 0x58;
    o[n++] = pdu;
    o[n++] = 0x00;
    o[n++] = (unsigned char)(np >> 8);
    o[n++] = (unsigned char)np;
    if (np > 0) memcpy(o + n, par, (size_t)np);
    return n + np;
}

static int next_label(avrcp_state *a)
{
    int l = a->our_label;
    a->our_label = (a->our_label + 1) & 0x0F;
    return l;
}

int avrcp_build_register_volume(avrcp_state *a, unsigned char *out, int max)
{
    unsigned char p[5] = { EV_VOLUME, 0, 0, 0, 0 };
    return vendor(out, next_label(a), 0, CT_NOTIFY, PDU_REG_NOTIFY, p, 5, max);
}

int avrcp_build_set_volume(avrcp_state *a, int vol, unsigned char *out, int max)
{
    unsigned char p[1];
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    a->volume = vol;
    p[0] = (unsigned char)vol;
    return vendor(out, next_label(a), 0, CT_CONTROL, PDU_SET_ABSVOL, p, 1, max);
}

int avrcp_build_volume_changed(avrcp_state *a, unsigned char *out, int max)
{
    unsigned char p[2];
    int lab = a->notify_label;
    if (lab < 0) return 0;
    a->notify_label = -1;             /* one CHANGED per registration */
    p[0] = EV_VOLUME;
    p[1] = (unsigned char)(a->volume & 0x7F);
    return vendor(out, lab, 1, RSP_CHANGED, PDU_REG_NOTIFY, p, 2, max);
}

/* Response from the headset to one of our commands. */
static void on_response(avrcp_state *a, const unsigned char *av, int n)
{
    unsigned char rc = av[0] & 0x0F;
    a->rx_rsps++;
    if (n < 10 || av[2] != OP_VENDOR) return;
    {
        unsigned char pdu = av[6];
        const unsigned char *par = av + 10;
        int np = n - 10;
        if (pdu == PDU_REG_NOTIFY && np >= 2 && par[0] == EV_VOLUME) {
            if (rc == RSP_INTERIM || rc == RSP_CHANGED) {
                a->remote_abs = 1;
                a->sink_renders = 1;
                a->ct_registered = (rc == RSP_INTERIM);
                if (rc == RSP_CHANGED) a->need_register = 1;
                if ((par[1] & 0x7F) != a->volume || rc == RSP_CHANGED) {
                    a->volume = par[1] & 0x7F;
                    a->changed = 1;
                }
                log_line("avrcp: headset volume %s %d/127",
                         rc == RSP_INTERIM ? "is" : "changed to", a->volume);
            } else {
                a->ct_registered = 0;
                log_line("avrcp: headset refused VOLUME_CHANGED registration (%#x)", rc);
            }
        } else if (pdu == PDU_SET_ABSVOL && np >= 1) {
            if (rc == RSP_ACCEPTED) {
                a->remote_abs = 1;
                a->sink_renders = 1;
                a->volume = par[0] & 0x7F;
                log_line("avrcp: headset set absolute volume %d/127", a->volume);
            } else {
                if (!a->ct_registered) a->sink_renders = 0;   /* software gain then */
                log_line("avrcp: headset refused SetAbsoluteVolume (%#x)", rc);
            }
        }
    }
}

int avrcp_input(avrcp_state *a, const unsigned char *in, int len,
                unsigned char *out, int max)
{
    const unsigned char *av;
    int n, label, o;
    unsigned char ctype, op;

    if (len < 3 || max < 16) return 0;
    if (((in[0] >> 2) & 0x03) != 0) {
        log_line("avrcp: fragmented AVCTP packet ignored");
        return 0;
    }
    label = in[0] >> 4;
    if ((((unsigned)in[1] << 8) | in[2]) != AVRCP_PID) {
        if (in[0] & 0x02) return 0;
        hdr(out, label, 1, 1);                 /* IPID: unknown profile */
        return 3;
    }
    av = in + 3;
    n = len - 3;
    if (n < 3) return 0;
    if (in[0] & 0x02) {                        /* a response to us */
        if ((av[0] & 0x0F) == RSP_NOT_IMPL && n >= 7 && av[2] == OP_VENDOR) {
            if (av[6] == PDU_SET_ABSVOL && !a->ct_registered) a->sink_renders = 0;
            log_line("avrcp: headset does not implement PDU %#x", av[6]);
            return 0;
        }
        on_response(a, av, n);
        return 0;
    }
    a->rx_cmds++;
    ctype = av[0] & 0x0F;
    op = av[2];

    if (op == OP_UNIT_INFO) {
        o = hdr(out, label, 1, 0);
        out[o++] = RSP_STABLE; out[o++] = 0xFF; out[o++] = OP_UNIT_INFO;
        out[o++] = 0x07; out[o++] = SUBUNIT_PANEL;
        out[o++] = 0x00; out[o++] = 0x19; out[o++] = 0x58;
        return o;
    }
    if (op == OP_SUBUNIT) {
        o = hdr(out, label, 1, 0);
        out[o++] = RSP_STABLE; out[o++] = 0xFF; out[o++] = OP_SUBUNIT;
        out[o++] = 0x07; out[o++] = SUBUNIT_PANEL;
        out[o++] = 0xFF; out[o++] = 0xFF; out[o++] = 0xFF;
        return o;
    }
    if (op == OP_PASSTHROUGH && n >= 5) {
        /* Echo with ACCEPTED; volume up/down keys step our volume. */
        unsigned char key = av[3] & 0x7F;
        int press = !(av[3] & 0x80);
        if (n > max - 3) return 0;
        o = hdr(out, label, 1, 0);
        memcpy(out + o, av, (size_t)n);
        out[o] = RSP_ACCEPTED;
        if (press && (key == 0x41 || key == 0x42)) {
            int v = a->volume + (key == 0x41 ? 8 : -8);
            a->volume = v < 0 ? 0 : v > 127 ? 127 : v;
            a->changed = 1;
            log_line("avrcp: volume key %s -> %d/127", key == 0x41 ? "up" : "down",
                     a->volume);
        }
        return o + n;
    }
    if (op == OP_VENDOR && n >= 10) {
        unsigned char pdu = av[6];
        const unsigned char *par = av + 10;
        int np = n - 10;
        unsigned char r[8];
        if (np < 0) np = 0;
        switch (pdu) {
        case PDU_GET_CAPS:
            if (np >= 1 && par[0] == 0x02) {       /* company IDs */
                r[0] = 0x02; r[1] = 1; r[2] = 0x00; r[3] = 0x19; r[4] = 0x58;
                return vendor(out, label, 1, RSP_STABLE, pdu, r, 5, max);
            }
            if (np >= 1 && par[0] == 0x03) {       /* events */
                r[0] = 0x03; r[1] = 1; r[2] = EV_VOLUME;
                log_line("avrcp: headset asked capabilities -> VOLUME_CHANGED");
                return vendor(out, label, 1, RSP_STABLE, pdu, r, 3, max);
            }
            r[0] = 0x01;                            /* invalid parameter */
            return vendor(out, label, 1, RSP_REJECTED, pdu, r, 1, max);
        case PDU_REG_NOTIFY:
            if (ctype == CT_NOTIFY && np >= 1 && par[0] == EV_VOLUME) {
                a->notify_label = label;
                a->remote_abs = 1;
                r[0] = EV_VOLUME; r[1] = (unsigned char)(a->volume & 0x7F);
                log_line("avrcp: headset registered for VOLUME_CHANGED (now %d/127)",
                         a->volume);
                return vendor(out, label, 1, RSP_INTERIM, pdu, r, 2, max);
            }
            r[0] = 0x01;
            return vendor(out, label, 1, RSP_REJECTED, pdu, r, 1, max);
        case PDU_SET_ABSVOL:
            if (np >= 1) {
                a->volume = par[0] & 0x7F;
                a->remote_abs = 1;
                a->changed = 1;
                r[0] = (unsigned char)a->volume;
                log_line("avrcp: headset SetAbsoluteVolume %d/127", a->volume);
                return vendor(out, label, 1, RSP_ACCEPTED, pdu, r, 1, max);
            }
            r[0] = 0x01;
            return vendor(out, label, 1, RSP_REJECTED, pdu, r, 1, max);
        case PDU_DISP_CHAR:
        case PDU_BATTERY:
            return vendor(out, label, 1, RSP_ACCEPTED, pdu, NULL, 0, max);
        default:
            r[0] = 0x00;                            /* invalid command */
            return vendor(out, label, 1, RSP_REJECTED, pdu, r, 1, max);
        }
    }
    /* Anything else: NOT IMPLEMENTED, frame echoed. */
    if (n > max - 3) n = max - 3;
    o = hdr(out, label, 1, 0);
    memcpy(out + o, av, (size_t)n);
    out[o] = RSP_NOT_IMPL;
    return o + n;
}
