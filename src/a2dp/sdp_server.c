/* SDP server (Core Vol 3 Part B). Developed by X-F1REBALL-X. */
#include "sdp_server.h"

#include <string.h>

#define SDP_ERROR_RSP        0x01
#define SDP_SS_REQ           0x02
#define SDP_SS_RSP           0x03
#define SDP_SA_REQ           0x04
#define SDP_SA_RSP           0x05
#define SDP_SSA_REQ          0x06
#define SDP_SSA_RSP          0x07

typedef struct {
    unsigned id;
    const unsigned char *v;
    int n;
} sdp_attr;

typedef struct {
    unsigned handle;
    const sdp_attr *attrs;
    int nattrs;
    const unsigned *uuids;   /* every UUID the record contains (16-bit) */
    int nuuids;
} sdp_record;

/* Data elements: 0x35 = DES (1-byte len), 0x19 = UUID16, 0x09 = uint16,
 * 0x0A = uint32, 0x25 = text (1-byte len). */
#define U16(x) 0x09, (unsigned char)((x) >> 8), (unsigned char)(x)
#define UU(x)  0x19, (unsigned char)((x) >> 8), (unsigned char)(x)
#define H32(x) 0x0A, (unsigned char)((x) >> 24), (unsigned char)((x) >> 16), \
               (unsigned char)((x) >> 8), (unsigned char)(x)

static const unsigned char browse[] = { 0x35, 3, UU(0x1002) };

/* A2DP Source */
static const unsigned char src_h[]  = { H32(0x00010001) };
static const unsigned char src_cl[] = { 0x35, 3, UU(0x110A) };
static const unsigned char src_pd[] = { 0x35, 16, 0x35, 6, UU(0x0100), U16(0x0019),
                                        0x35, 6, UU(0x0019), U16(0x0103) };
static const unsigned char src_pf[] = { 0x35, 8, 0x35, 6, UU(0x110D), U16(0x0103) };
static const unsigned char src_ft[] = { U16(0x0001) };            /* player */
static const unsigned char src_nm[] = { 0x25, 10, 'H','e','a','r','B','r','i','d','g','e' };
static const sdp_attr src_a[] = {
    { 0x0000, src_h, sizeof src_h }, { 0x0001, src_cl, sizeof src_cl },
    { 0x0004, src_pd, sizeof src_pd }, { 0x0005, browse, sizeof browse },
    { 0x0009, src_pf, sizeof src_pf }, { 0x0100, src_nm, sizeof src_nm },
    { 0x0311, src_ft, sizeof src_ft },
};
static const unsigned src_u[] = { 0x110A, 0x0100, 0x0019, 0x110D, 0x1002 };

/* AVRCP Target: AVRCP 1.5 over AVCTP 1.4, Category 2 (Monitor/Amplifier),
 * which is the category carrying absolute volume. */
static const unsigned char tg_h[]  = { H32(0x00010002) };
static const unsigned char tg_cl[] = { 0x35, 3, UU(0x110C) };
static const unsigned char av_pd[] = { 0x35, 16, 0x35, 6, UU(0x0100), U16(0x0017),
                                       0x35, 6, UU(0x0017), U16(0x0104) };
static const unsigned char av_pf[] = { 0x35, 8, 0x35, 6, UU(0x110E), U16(0x0105) };
static const unsigned char tg_ft[] = { U16(0x0002) };             /* Category 2 */
static const sdp_attr tg_a[] = {
    { 0x0000, tg_h, sizeof tg_h }, { 0x0001, tg_cl, sizeof tg_cl },
    { 0x0004, av_pd, sizeof av_pd }, { 0x0005, browse, sizeof browse },
    { 0x0009, av_pf, sizeof av_pf }, { 0x0311, tg_ft, sizeof tg_ft },
};
static const unsigned tg_u[] = { 0x110C, 0x0100, 0x0017, 0x110E, 0x1002 };

/* AVRCP Controller (we send SetAbsoluteVolume / register for changes). */
static const unsigned char ct_h[]  = { H32(0x00010003) };
static const unsigned char ct_cl[] = { 0x35, 6, UU(0x110E), UU(0x110F) };
static const unsigned char ct_ft[] = { U16(0x0002) };             /* Category 2 */
static const sdp_attr ct_a[] = {
    { 0x0000, ct_h, sizeof ct_h }, { 0x0001, ct_cl, sizeof ct_cl },
    { 0x0004, av_pd, sizeof av_pd }, { 0x0005, browse, sizeof browse },
    { 0x0009, av_pf, sizeof av_pf }, { 0x0311, ct_ft, sizeof ct_ft },
};
static const unsigned ct_u[] = { 0x110E, 0x110F, 0x0100, 0x0017, 0x1002 };

/* Device ID (PnP Information 0x1200, DI 1.3). Some headsets (the Xbox one)
 * look it up first and hang up on an empty answer. Generic values:
 * vendor id source 0x0002 (USB-IF), vendor 0x1D6B (Linux Foundation),
 * product 0x0001, version 1.0.0, primary record. */
static const unsigned char di_h[]   = { H32(0x00010004) };
static const unsigned char di_cl[]  = { 0x35, 3, UU(0x1200) };
static const unsigned char di_pf[]  = { 0x35, 8, 0x35, 6, UU(0x1200), U16(0x0103) };
static const unsigned char di_spec[] = { U16(0x0103) };
static const unsigned char di_vid[]  = { U16(0x1D6B) };
static const unsigned char di_pid[]  = { U16(0x0001) };
static const unsigned char di_ver[]  = { U16(0x0100) };
static const unsigned char di_prim[] = { 0x28, 0x01 };            /* boolean true */
static const unsigned char di_src[]  = { U16(0x0002) };           /* USB-IF */
static const sdp_attr di_a[] = {
    { 0x0000, di_h, sizeof di_h }, { 0x0001, di_cl, sizeof di_cl },
    { 0x0005, browse, sizeof browse }, { 0x0009, di_pf, sizeof di_pf },
    { 0x0200, di_spec, sizeof di_spec }, { 0x0201, di_vid, sizeof di_vid },
    { 0x0202, di_pid, sizeof di_pid }, { 0x0203, di_ver, sizeof di_ver },
    { 0x0204, di_prim, sizeof di_prim }, { 0x0205, di_src, sizeof di_src },
};
static const unsigned di_u[] = { 0x1200, 0x1002 };

static const sdp_record records[] = {
    { 0x00010001, src_a, 7, src_u, 5 },
    { 0x00010002, tg_a, 6, tg_u, 5 },
    { 0x00010003, ct_a, 6, ct_u, 5 },
    { 0x00010004, di_a, 10, di_u, 2 },
};
#define NREC ((int)(sizeof records / sizeof records[0]))

static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned be32(const unsigned char *p)
{
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}

/* Parse a data element header at p: returns header size, sets type/len. */
static int de_hdr(const unsigned char *p, int avail, int *type, int *dlen)
{
    int sz;
    if (avail < 1) return -1;
    *type = p[0] >> 3;
    sz = p[0] & 7;
    if (*type == 0) { *dlen = 0; return 1; }
    if (sz <= 4) {
        static const int fixed[] = { 1, 2, 4, 8, 16 };
        *dlen = fixed[sz];
        return 1;
    }
    if (sz == 5) { if (avail < 2) return -1; *dlen = p[1]; return 2; }
    if (sz == 6) { if (avail < 3) return -1; *dlen = (int)be16(p + 1); return 3; }
    if (avail < 5) return -1;
    *dlen = (int)be32(p + 1);
    return 5;
}

static const unsigned char base_uuid_tail[12] = {
    0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB };

/* Parses a DES of UUIDs into 32-bit values (128-bit off the base → -1). */
static int parse_uuid_list(const unsigned char *p, int avail, unsigned *out,
                           int max, int *used)
{
    int t, dl, h = de_hdr(p, avail, &t, &dl), off, n = 0;
    if (h < 0 || t != 6 || h + dl > avail) return -1;
    *used = h + dl;
    for (off = h; off < h + dl;) {
        int t2, l2, h2 = de_hdr(p + off, h + dl - off, &t2, &l2);
        if (h2 < 0 || t2 != 3 || off + h2 + l2 > h + dl) return -1;
        if (n < max) {
            const unsigned char *u = p + off + h2;
            if (l2 == 2) out[n++] = be16(u);
            else if (l2 == 4) out[n++] = be32(u);
            else if (l2 == 16)
                out[n++] = memcmp(u + 4, base_uuid_tail, 12) ? 0xFFFFFFFFu : be32(u);
        }
        off += h2 + l2;
    }
    return n;
}

/* Attribute ID list: DES of uint16 ids or uint32 ranges. */
typedef struct { unsigned lo, hi; } range;

static int parse_attr_list(const unsigned char *p, int avail, range *out, int max,
                           int *used)
{
    int t, dl, h = de_hdr(p, avail, &t, &dl), off, n = 0;
    if (h < 0 || t != 6 || h + dl > avail) return -1;
    *used = h + dl;
    for (off = h; off < h + dl;) {
        int t2, l2, h2 = de_hdr(p + off, h + dl - off, &t2, &l2);
        if (h2 < 0 || t2 != 1 || off + h2 + l2 > h + dl) return -1;
        if (n < max) {
            if (l2 == 2) { out[n].lo = out[n].hi = be16(p + off + h2); n++; }
            else if (l2 == 4) {
                out[n].lo = be16(p + off + h2);
                out[n].hi = be16(p + off + h2 + 2);
                n++;
            }
        }
        off += h2 + l2;
    }
    return n;
}

static int rec_matches(const sdp_record *r, const unsigned *pat, int np)
{
    int i, j;
    for (i = 0; i < np; i++) {      /* every pattern UUID must be present */
        int hit = 0;
        for (j = 0; j < r->nuuids; j++)
            if (r->uuids[j] == pat[i]) hit = 1;
        if (!hit) return 0;
    }
    return np > 0;
}

/* Encode one record's attributes in [ranges] as a DES (3-byte length). */
static int rec_attrs(const sdp_record *r, const range *rg, int nr,
                     unsigned char *o, int max)
{
    int i, j, n = 3;
    if (max < 3) return -1;
    for (i = 0; i < r->nattrs; i++) {
        int want = 0;
        for (j = 0; j < nr; j++)
            if (r->attrs[i].id >= rg[j].lo && r->attrs[i].id <= rg[j].hi) want = 1;
        if (!want) continue;
        if (n + 3 + r->attrs[i].n > max) return -1;
        o[n++] = 0x09;
        o[n++] = (unsigned char)(r->attrs[i].id >> 8);
        o[n++] = (unsigned char)r->attrs[i].id;
        memcpy(o + n, r->attrs[i].v, (size_t)r->attrs[i].n);
        n += r->attrs[i].n;
    }
    o[0] = 0x36;
    o[1] = (unsigned char)((n - 3) >> 8);
    o[2] = (unsigned char)(n - 3);
    return n;
}

static int error_rsp(unsigned tid, unsigned code, unsigned char *rsp)
{
    rsp[0] = SDP_ERROR_RSP;
    rsp[1] = (unsigned char)(tid >> 8); rsp[2] = (unsigned char)tid;
    rsp[3] = 0; rsp[4] = 2;
    rsp[5] = (unsigned char)(code >> 8); rsp[6] = (unsigned char)code;
    return 7;
}

/* Continuation: 2-byte offset into the full attribute byte stream. */
static int finish_attr_rsp(unsigned char pdu, unsigned tid, const unsigned char *all,
                           int total, unsigned max_bytes, const unsigned char *cont,
                           int contlen, unsigned char *rsp, int rsp_max)
{
    int off = 0, chunk, n;
    if (contlen == 2) off = (int)be16(cont);
    else if (contlen != 0) return error_rsp(tid, 0x0005, rsp);
    if (off > total) return error_rsp(tid, 0x0005, rsp);
    chunk = total - off;
    if (max_bytes < 7) max_bytes = 7;
    if (chunk > (int)max_bytes) chunk = (int)max_bytes;
    if (chunk > rsp_max - 5 - 2 - 3) chunk = rsp_max - 5 - 2 - 3;
    if (chunk < 0) return 0;
    rsp[0] = pdu;
    rsp[1] = (unsigned char)(tid >> 8); rsp[2] = (unsigned char)tid;
    rsp[5] = (unsigned char)(chunk >> 8); rsp[6] = (unsigned char)chunk;
    memcpy(rsp + 7, all + off, (size_t)chunk);
    n = 7 + chunk;
    if (off + chunk < total) {
        rsp[n++] = 2;
        rsp[n++] = (unsigned char)((off + chunk) >> 8);
        rsp[n++] = (unsigned char)(off + chunk);
    } else {
        rsp[n++] = 0;
    }
    rsp[3] = (unsigned char)((n - 5) >> 8); rsp[4] = (unsigned char)(n - 5);
    return n;
}

int sdp_server_handle(const unsigned char *req, int len,
                      unsigned char *rsp, int rsp_max)
{
    unsigned tid, plen;
    const unsigned char *p;
    int avail, used, i;
    unsigned pat[12];
    range rg[16];
    unsigned char all[512];

    if (len < 5 || rsp_max < 48) return 0;
    tid = be16(req + 1);
    plen = be16(req + 3);
    p = req + 5;
    avail = len - 5;
    if ((int)plen > avail) return error_rsp(tid, 0x0004, rsp);
    avail = (int)plen;

    switch (req[0]) {
    case SDP_SS_REQ: {
        int np = parse_uuid_list(p, avail, pat, 12, &used), n = 0, maxc;
        if (np < 0 || used + 3 > avail) return error_rsp(tid, 0x0003, rsp);
        maxc = (int)be16(p + used);
        rsp[0] = SDP_SS_RSP;
        rsp[1] = (unsigned char)(tid >> 8); rsp[2] = (unsigned char)tid;
        for (i = 0; i < NREC && n < maxc; i++) {
            if (!rec_matches(&records[i], pat, np)) continue;
            rsp[9 + 4 * n] = (unsigned char)(records[i].handle >> 24);
            rsp[10 + 4 * n] = (unsigned char)(records[i].handle >> 16);
            rsp[11 + 4 * n] = (unsigned char)(records[i].handle >> 8);
            rsp[12 + 4 * n] = (unsigned char)records[i].handle;
            n++;
        }
        rsp[5] = 0; rsp[6] = (unsigned char)n;     /* total */
        rsp[7] = 0; rsp[8] = (unsigned char)n;     /* current */
        rsp[9 + 4 * n] = 0;                        /* no continuation */
        rsp[3] = 0; rsp[4] = (unsigned char)(5 + 4 * n);
        return 10 + 4 * n;
    }
    case SDP_SA_REQ: {
        unsigned h, maxb;
        int nr, total;
        const sdp_record *r = NULL;
        if (avail < 7) return error_rsp(tid, 0x0003, rsp);
        h = be32(p);
        maxb = be16(p + 4);
        nr = parse_attr_list(p + 6, avail - 6, rg, 16, &used);
        if (nr < 0 || 6 + used + 1 > avail) return error_rsp(tid, 0x0003, rsp);
        for (i = 0; i < NREC; i++) if (records[i].handle == h) r = &records[i];
        if (!r) return error_rsp(tid, 0x0002, rsp);
        total = rec_attrs(r, rg, nr, all, (int)sizeof all);
        if (total < 0) return error_rsp(tid, 0x0006, rsp);
        return finish_attr_rsp(SDP_SA_RSP, tid, all, total, maxb, p + 6 + used + 1,
                               p[6 + used], rsp, rsp_max);
    }
    case SDP_SSA_REQ: {
        int np = parse_uuid_list(p, avail, pat, 12, &used), nr, used2, total = 3;
        unsigned maxb;
        if (np < 0 || used + 2 > avail) return error_rsp(tid, 0x0003, rsp);
        maxb = be16(p + used);
        nr = parse_attr_list(p + used + 2, avail - used - 2, rg, 16, &used2);
        if (nr < 0 || used + 2 + used2 + 1 > avail) return error_rsp(tid, 0x0003, rsp);
        for (i = 0; i < NREC; i++) {
            int k;
            if (!rec_matches(&records[i], pat, np)) continue;
            k = rec_attrs(&records[i], rg, nr, all + total, (int)sizeof all - total);
            if (k < 0) return error_rsp(tid, 0x0006, rsp);
            total += k;
        }
        all[0] = 0x36;
        all[1] = (unsigned char)((total - 3) >> 8);
        all[2] = (unsigned char)(total - 3);
        return finish_attr_rsp(SDP_SSA_RSP, tid, all, total, maxb,
                               p + used + 2 + used2 + 1, p[used + 2 + used2],
                               rsp, rsp_max);
    }
    default:
        return error_rsp(tid, 0x0003, rsp);
    }
}
