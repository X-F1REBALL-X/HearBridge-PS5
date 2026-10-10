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
#define PDU_PLAY_STAT  0x30
#define EV_PLAYBACK    0x01
#define EV_VOLUME      0x0D
#define EV_BATT        0x06

int avrcp_reported(const avrcp_state *a)
{
    if (!a) return 0;
    /* notify_label stays set while the headset's VOLUME_CHANGED
     * registration is outstanding; the other flags stick after it has
     * shown absolute volume (SetAbsoluteVolume or our registration). */
    return a->remote_abs || a->ct_registered || a->notify_label >= 0 || a->sink_renders;
}

const char *avrcp_key_name(int key)
{
    switch (key) {
    case 0x41: return "volume up";
    case 0x42: return "volume down";
    case 0x43: return "mute";
    case 0x44: return "play";
    case 0x45: return "stop";
    case 0x46: return "pause";
    case 0x4B: return "forward";
    case 0x4C: return "backward";
    case 0x48: return "rewind";
    case 0x49: return "fast forward";
    default:   return "";
    }
}

void avrcp_init(avrcp_state *a, int volume)
{
    memset(a, 0, sizeof *a);
    a->volume = volume < 0 ? 0 : volume > 127 ? 127 : volume;
    a->notify_label = -1;
    a->our_label = 1;
    a->battery = -1;
    a->batt_label = -1;
    a->our_set_ms = -100000;
    a->seek_vol = 1;
}

/* A volume report right after our own SetAbsoluteVolume is that change
 * coming back, not someone turning the headset. */
static int our_echo(const avrcp_state *a)
{
    long d = a->now_ms - a->our_set_ms;
    return d >= 0 && d < AVRCP_ECHO_MS;
}

const char *avrcp_battery_key(int s)
{
    switch (s) {
    case AVRCP_BATT_NORMAL:   return "ok";
    case AVRCP_BATT_WARNING:  return "low";
    case AVRCP_BATT_CRITICAL: return "critical";
    case AVRCP_BATT_EXTERNAL: return "charging";
    case AVRCP_BATT_FULL:     return "full";
    default:                  return "";
    }
}

int avrcp_battery_level(int s)
{
    static const int lv[5] = { 60, 20, 5, -1, 100 };
    if (s < 0 || s > 4) return -1;
    return lv[s];
}

static void set_battery(avrcp_state *a, int s, const char *how)
{
    if (s < 0 || s > 4) return;
    if (s != a->battery)
        log_line("avrcp: headset battery %s (%s)", avrcp_battery_key(s), how);
    a->battery = s;
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
    if (l == a->batt_label) {              /* still waiting on battery: skip it */
        l = a->our_label;
        a->our_label = (a->our_label + 1) & 0x0F;
    }
    return l;
}

int avrcp_build_register_volume(avrcp_state *a, unsigned char *out, int max)
{
    unsigned char p[5] = { EV_VOLUME, 0, 0, 0, 0 };
    return vendor(out, next_label(a), 0, CT_NOTIFY, PDU_REG_NOTIFY, p, 5, max);
}

int avrcp_build_register_battery(avrcp_state *a, unsigned char *out, int max)
{
    unsigned char p[5] = { EV_BATT, 0, 0, 0, 0 };
    a->batt_tried = 1;
    a->need_batt = 0;
    a->batt_label = next_label(a);
    return vendor(out, a->batt_label, 0, CT_NOTIFY, PDU_REG_NOTIFY, p, 5, max);
}

int avrcp_build_set_volume(avrcp_state *a, int vol, unsigned char *out, int max)
{
    unsigned char p[1];
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    a->volume = vol;
    a->our_set_ms = a->now_ms;
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
static void on_response(avrcp_state *a, int label, const unsigned char *av, int n)
{
    unsigned char rc = av[0] & 0x0F;
    a->rx_rsps++;
    if (n < 10 || av[2] != OP_VENDOR) return;
    if (av[6] == PDU_REG_NOTIFY && label == a->batt_label && av[10 < n ? 10 : 0] != EV_VOLUME) {
        /* Battery registration: INTERIM/CHANGED carry the status; anything
         * else (rejected) leaves it unknown and we do not ask again. */
        const unsigned char *par = av + 10;
        int np = n - 10;
        if ((rc == RSP_INTERIM || rc == RSP_CHANGED) && np >= 2 && par[0] == EV_BATT) {
            set_battery(a, par[1], rc == RSP_INTERIM ? "registered" : "changed");
            if (rc == RSP_CHANGED) a->need_batt = 1;      /* one CHANGED per registration */
            else return;
        } else {
            log_line("avrcp: headset does not report battery (%#x)", rc);
        }
        a->batt_label = -1;
        return;
    }
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
                if (rc == RSP_INTERIM && !a->batt_tried) a->need_batt = 1;
                a->vol_reports++;
                a->vol_refused = 0;
                if ((par[1] & 0x7F) != a->volume || rc == RSP_CHANGED) {
                    int moved = (par[1] & 0x7F) != a->volume;
                    a->volume = par[1] & 0x7F;
                    a->changed = 1;
                    if (rc == RSP_CHANGED && moved && !our_echo(a)) a->vol_from_headset++;
                }
                log_line("avrcp: headset volume %s %d/127%s",
                         rc == RSP_INTERIM ? "is" : "changed to", a->volume,
                         rc == RSP_CHANGED && our_echo(a) ? " (our own change)" : "");
            } else {
                a->ct_registered = 0;
                a->vol_refused = 1;
                log_line("avrcp: headset refused VOLUME_CHANGED registration (%#x)", rc);
            }
        } else if (pdu == PDU_SET_ABSVOL && np >= 1) {
            if (rc == RSP_ACCEPTED) {
                a->remote_abs = 1;
                a->sink_renders = 1;
                a->volume = par[0] & 0x7F;
                a->vol_reports++;
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
            if (av[6] == PDU_REG_NOTIFY && label == a->batt_label) a->batt_label = -1;
            else if (av[6] == PDU_REG_NOTIFY) a->vol_refused = 1;
            log_line("avrcp: headset does not implement PDU %#x", av[6]);
            return 0;
        }
        on_response(a, label, av, n);
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
        int seek = a->seek_vol && (key == 0x4B || key == 0x4C);
        if (press && (key == 0x41 || key == 0x42 || seek)) {
            /* Earbuds without volume keys send next / previous track:
             * with the setting on, those step the volume as well. */
            int up = key == 0x41 || key == 0x4B;
            int v = a->volume + (up ? 8 : -8);
            a->volume = v < 0 ? 0 : v > 127 ? 127 : v;
            a->changed = 1;
            a->vol_from_headset++;
            a->vol_reports++;
            log_line("avrcp: %s key %s -> %d/127", seek ? (up ? "next" : "previous") : "volume",
                     up ? "up" : "down", a->volume);
        } else if (press) {
            const char *nm = avrcp_key_name(key);
            log_line("avrcp: headset key %#x%s%s%s (not used)", key,
                     nm[0] ? " (" : "", nm, nm[0] ? ")" : "");
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
                /* As target we are a plain player: playback status only.
                 * Absolute volume (VOLUME_CHANGED) belongs to the headset
                 * side, so it is not offered here; a headset that offers
                 * it to us as target sends volume reports and keys. */
                r[0] = 0x03; r[1] = 1; r[2] = EV_PLAYBACK;
                log_line("avrcp: headset asked capabilities -> PLAYBACK_STATUS_CHANGED");
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
            if (ctype == CT_NOTIFY && np >= 1 && par[0] == EV_PLAYBACK) {
                r[0] = EV_PLAYBACK; r[1] = 0x01;    /* playing */
                return vendor(out, label, 1, RSP_INTERIM, pdu, r, 2, max);
            }
            r[0] = 0x01;
            return vendor(out, label, 1, RSP_REJECTED, pdu, r, 1, max);
        case PDU_PLAY_STAT: {
            /* length and position unknown (0xFFFFFFFF), playing */
            unsigned char ps[9] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
            return vendor(out, label, 1, RSP_STABLE, pdu, ps, 9, max);
        }
        case PDU_SET_ABSVOL:
            if (np >= 1) {
                if ((par[0] & 0x7F) != a->volume && !our_echo(a)) a->vol_from_headset++;
                a->volume = par[0] & 0x7F;
                a->remote_abs = 1;
                a->changed = 1;
                a->vol_reports++;
                r[0] = (unsigned char)a->volume;
                log_line("avrcp: headset SetAbsoluteVolume %d/127", a->volume);
                return vendor(out, label, 1, RSP_ACCEPTED, pdu, r, 1, max);
            }
            r[0] = 0x01;
            return vendor(out, label, 1, RSP_REJECTED, pdu, r, 1, max);
        case PDU_BATTERY:
            /* InformBatteryStatusOfCT: the headset (as controller) tells us
             * its own battery state. */
            if (np >= 1) set_battery(a, par[0], "headset told us");
            return vendor(out, label, 1, RSP_ACCEPTED, pdu, NULL, 0, max);
        case PDU_DISP_CHAR:
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
