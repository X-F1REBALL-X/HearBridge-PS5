/* Host tests for the Realtek firmware container parser and fragmenter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hci.h"
#include "realtek_fw.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void put_le16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void put_le32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void test_identity(void)
{
    unsigned char cc[16];
    hb_chip_identity id;

    memset(cc, 0, sizeof cc);
    cc[0] = 0x0e; cc[1] = 12; cc[2] = 1; cc[3] = 0x01; cc[4] = 0x10; cc[5] = 0;
    cc[6] = 0x0a; put_le16(cc + 7, 0x000b); cc[9] = 0x0a;
    put_le16(cc + 10, 0x005d); put_le16(cc + 12, 0x8761);
    CHECK(hb_chip_identity_from_cc(cc, 14, &id), "identity parses");
    CHECK(id.hci_version == 0x0a && id.hci_revision == 0x000b &&
          id.manufacturer == 0x005d && id.lmp_subversion == 0x8761, "identity fields");
    CHECK(hb_rtl_chip_for(&id) != NULL && !strcmp(hb_rtl_chip_for(&id)->name, "RTL8761BU"),
          "RTL8761BU table entry");
    CHECK(hb_rtl_in_rom_mode(&id), "known part can be in ROM mode");
    cc[0] = 0x0f;
    CHECK(!hb_chip_identity_from_cc(cc, 14, &id), "wrong event rejected");
}

static void test_v1(void)
{
    unsigned char fw[64];
    unsigned char *out = NULL;
    size_t out_len = 0;
    char why[128];

    memset(fw, 0, sizeof fw);
    memcpy(fw, "Realtech", 8);
    fw[8] = 0x11; fw[9] = 0x22; fw[10] = 0x33; fw[11] = 0x44;
    put_le16(fw + 12, 2);
    /* table at 14: ids (4 bytes), lengths (4), offsets (8), data at 30 */
    put_le16(fw + 14, 7);            /* rom 6 -> wanted 7 (first entry, not ours) */
    put_le16(fw + 16, 6);            /* ours: rom 5 -> wanted 6 */
    put_le16(fw + 18, 3);
    put_le16(fw + 20, 5);
    put_le32(fw + 22, 34);           /* first patch */
    put_le32(fw + 26, 40);           /* our patch at 40 */
    fw[34] = 'x'; fw[35] = 'x'; fw[36] = 'x';
    memcpy(fw + 40, "PATCH", 5);

    CHECK(hb_rtl_build_image(fw, sizeof fw, NULL, 0, 5, &out, &out_len, why, sizeof why),
          "v1 image builds (%s)", why);
    CHECK(out_len == 5 && out[0] == 'P' && out[1] == 0x11 && out[2] == 0x22 &&
          out[3] == 0x33 && out[4] == 0x44,
          "v1 picks the chip's patch and stamps the file version");
    free(out);

    out = NULL;
    CHECK(!hb_rtl_build_image(fw, sizeof fw, NULL, 0, 9, &out, &out_len, why, sizeof why) &&
          strstr(why, "ROM version 9"), "v1 missing patch reports the ROM version");
}

static size_t v2_sub(unsigned char *p, unsigned eco, unsigned priority, const char *data, size_t n)
{
    p[0] = (unsigned char)eco;
    p[1] = (unsigned char)priority;
    p[2] = p[3] = 0;
    put_le32(p + 4, (unsigned)n);
    memcpy(p + 8, data, n);
    return 8 + n;
}

static void test_v2(void)
{
    unsigned char fw[512];
    unsigned char *out = NULL;
    size_t out_len = 0, pos, at;
    char why[128];

    memset(fw, 0, sizeof fw);
    memcpy(fw, "RTBTCore", 8);
    put_le32(fw + 16, 3);
    pos = 20;
    /* snippets: three subsections, only eco 6 (rom 5 + 1) is used */
    put_le32(fw + pos, 1);
    put_le32(fw + pos + 4, 0);       /* length fixed below */
    at = pos + 8;
    put_le16(fw + at, 3);
    fw[at + 2] = fw[at + 3] = 0;
    at += 4;
    at += v2_sub(fw + at, 6, 2, "BB", 2);
    at += v2_sub(fw + at, 5, 1, "XX", 2);
    at += v2_sub(fw + at, 6, 1, "AA", 2);
    put_le32(fw + pos + 4, (unsigned)(at - (pos + 8)));
    pos = at;
    /* security section: must be skipped */
    put_le32(fw + pos, 3);
    put_le32(fw + pos + 4, 4);
    memcpy(fw + pos + 8, "SEC!", 4);
    pos += 12;
    /* dummy section, priority 3 */
    put_le32(fw + pos, 2);
    put_le32(fw + pos + 4, 0);
    at = pos + 8;
    put_le16(fw + at, 1);
    fw[at + 2] = fw[at + 3] = 0;
    at += 4;
    at += v2_sub(fw + at, 6, 3, "CC", 2);
    put_le32(fw + pos + 4, (unsigned)(at - (pos + 8)));
    pos = at;
    memset(fw + pos, 0, 7);          /* extension tail */
    pos += 7;

    CHECK(hb_rtl_build_image(fw, pos, (const unsigned char *)"CFG", 3, 5, &out, &out_len,
                             why, sizeof why), "v2 image builds (%s)", why);
    CHECK(out_len == 9 && !memcmp(out, "AABBCCCFG", 9),
          "v2 orders by priority, skips security, appends config");
    free(out);
}

struct fake {
    unsigned char ev[8];
    unsigned op_seen[64];
    unsigned char frag[64][256];
    int frag_len[64];
    int frags;
    int has_ev;
    int rom;
};

static int fake_cmd(void *self, unsigned op, const void *args, int nargs)
{
    struct fake *f = self;
    if (op == 0xfc20) {
        if (f->frags < 64) {
            f->op_seen[f->frags] = op;
            f->frag_len[f->frags] = nargs;
            memcpy(f->frag[f->frags], args, (size_t)nargs);
            f->frags++;
        }
    }
    f->ev[0] = 0x0e;
    f->ev[1] = 3;
    f->ev[2] = 1;
    f->ev[3] = (unsigned char)op;
    f->ev[4] = (unsigned char)(op >> 8);
    f->ev[5] = 0;
    f->has_ev = 1;
    return 1;
}

static int fake_pump(void *self, int wait_ms)
{
    struct fake *f = self;
    (void)wait_ms;
    return f->has_ev;
}

static int fake_next(void *self, unsigned char *dst, int cap)
{
    struct fake *f = self;
    int n;
    if (!f->has_ev) return 0;
    n = 6 < cap ? 6 : cap;
    memcpy(dst, f->ev, (size_t)n);
    f->has_ev = 0;
    return n;
}

static int fake_acl(void *self, unsigned char *dst, int cap)
{
    (void)self; (void)dst; (void)cap;
    return 0;
}

static void test_fragments(void)
{
    struct fake f;
    hci_t hci;
    unsigned char image[505];
    char why[128];
    int i;

    memset(&f, 0, sizeof f);
    memset(image, 0x5a, sizeof image);
    hci.ctx = &f;
    hci.ops = &(const hci_ops){
        .next_acl = fake_acl,
        .next_event = fake_next,
        .pump = fake_pump,
        .cmd = fake_cmd,
        .acl_send = NULL,
        .diag = NULL,
        .close = NULL,
    };
    CHECK(hb_rtl_upload(hci, image, sizeof image, why, sizeof why), "upload succeeds (%s)", why);
    CHECK(f.frags == 3, "505 bytes -> 3 fragments (got %d)", f.frags);
    CHECK(f.frag[0][0] == 0 && f.frag_len[0] == 253, "first fragment header 0, 252 data");
    CHECK(f.frag[1][0] == 1 && f.frag_len[1] == 253, "second fragment header 1");
    CHECK(f.frag[2][0] == 0x82 && f.frag_len[2] == 2, "last fragment has bit 7 and the tail");
    CHECK(!memcmp(f.frag[0] + 1, image, 252) && f.frag[1][1] == image[252], "fragment data in order");
    for (i = 0; i < f.frags; i++) CHECK(f.op_seen[i] == 0xfc20, "fragment sent with 0xfc20");
}

int main(void)
{
    test_identity();
    test_v1();
    test_v2();
    test_fragments();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ok   realtek firmware: identity, v1/v2 containers, fragments\n");
    return 0;
}
