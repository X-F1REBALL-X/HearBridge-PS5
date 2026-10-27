/* realtek_fw.c - see realtek_fw.h.
 *
 * The container formats and the fragment layout follow Linux's btrtl driver
 * (drivers/bluetooth/btrtl.c), which is what the chips accept. The file is
 * looked up by the Read Local Version identity, exactly like btrtl does. */
#include "realtek_fw.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hci_cmd.h"
#include "log.h"
#include "util.h"

#define OP_READ_ROM_VERSION 0xfc6d
#define OP_DOWNLOAD_PATCH   0xfc20

#define REALTEK_MANUFACTURER 0x005d
#define FRAGMENT_SIZE        252
#define MAX_FIRMWARE_FILE    (512u * 1024u)

#define EPATCH_HEADER        14u  /* signature(8) + version(4) + patches(2) */
#define EPATCH_HEADER_V2     20u  /* signature(8) + version(8) + sections(4) */
#define V2_TAIL              7u   /* extension section after the last section */

#define V2_SECTION_SNIPPETS  1u
#define V2_SECTION_DUMMY     2u
#define V2_SECTION_SECURITY  3u

/* The USB parts btrtl knows, with the firmware file for each. UART variants
 * and the 8723A (older download format) are left out. */
static const hb_rtl_chip g_chips[] = {
    { 0x8723, 0x000b, 0x06, "RTL8723BU", "rtl8723b_fw.bin", "rtl8723b_config.bin", 0 },
    { 0x8723, 0x000d, 0x08, "RTL8723DU", "rtl8723d_fw.bin", "rtl8723d_config.bin", 1 },
    { 0x8821, 0x000a, 0x06, "RTL8821AU", "rtl8821a_fw.bin", "rtl8821a_config.bin", 0 },
    { 0x8821, 0x000c, 0x08, "RTL8821CU", "rtl8821c_fw.bin", "rtl8821c_config.bin", 0 },
    { 0x8761, 0x000a, 0x06, "RTL8761AU", "rtl8761a_fw.bin", "rtl8761a_config.bin", 0 },
    { 0x8761, 0x000b, 0x0a, "RTL8761BU", "rtl8761bu_fw.bin", "rtl8761bu_config.bin", 0 },
    { 0x8761, 0x000e, 0x00, "RTL8761CU", "rtl8761cu_fw.bin", "rtl8761cu_config.bin", 0 },
    { 0x8822, 0x000b, 0x07, "RTL8822BU", "rtl8822b_fw.bin", "rtl8822b_config.bin", 1 },
    { 0x8822, 0x000c, 0x0a, "RTL8822CU", "rtl8822cu_fw.bin", "rtl8822cu_config.bin", 0 },
    { 0x8852, 0x000a, 0x0b, "RTL8852AU", "rtl8852au_fw.bin", "rtl8852au_config.bin", 0 },
    { 0x8852, 0x000b, 0x0b, "RTL8852BU", "rtl8852bu_fw.bin", "rtl8852bu_config.bin", 0 },
    { 0x8852, 0x000c, 0x0c, "RTL8852CU", "rtl8852cu_fw.bin", "rtl8852cu_config.bin", 0 },
    { 0x8851, 0x000b, 0x0c, "RTL8851BU", "rtl8851bu_fw.bin", "rtl8851bu_config.bin", 0 },
    { 0x8922, 0x000a, 0x0c, "RTL8922AU", "rtl8922au_fw.bin", "rtl8922au_config.bin", 0 },
    { 0x8852, 0x0087, 0x0c, "RTL8852BTU", "rtl8852btu_fw.bin", "rtl8852btu_config.bin", 0 },
};

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)le16(p) | ((uint32_t)le16(p + 2) << 16);
}

static void set_why(char *why, int cap, const char *text)
{
    if (why && cap > 0) snprintf(why, (size_t)cap, "%s", text);
}

static void set_no_patch(char *why, int cap, unsigned char rom_version)
{
    if (why && cap > 0)
        snprintf(why, (size_t)cap, "it has no patch for ROM version %u", (unsigned)rom_version);
}

int hb_chip_identity_from_cc(const unsigned char *ev, int len, hb_chip_identity *out)
{
    if (!ev || !out || len < 14 || ev[0] != 0x0e || ev[5] != 0 || le16(ev + 3) != HB_OP_READ_LOCAL_VERSION)
        return 0;
    out->hci_version    = ev[6];
    out->hci_revision   = (unsigned short)le16(ev + 7);
    out->lmp_version    = ev[9];
    out->manufacturer   = (unsigned short)le16(ev + 10);
    out->lmp_subversion = (unsigned short)le16(ev + 12);
    return 1;
}

const hb_rtl_chip *hb_rtl_chip_for(const hb_chip_identity *id)
{
    size_t i;
    if (!id) return NULL;
    for (i = 0; i < sizeof g_chips / sizeof g_chips[0]; i++)
        if (g_chips[i].lmp_subversion == id->lmp_subversion &&
            g_chips[i].hci_revision == id->hci_revision &&
            g_chips[i].hci_version == id->hci_version)
            return &g_chips[i];
    return NULL;
}

int hb_rtl_in_rom_mode(const hb_chip_identity *id)
{
    size_t i;
    if (!id) return 0;
    for (i = 0; i < sizeof g_chips / sizeof g_chips[0]; i++)
        if (g_chips[i].lmp_subversion == id->lmp_subversion) return 1;
    return 0;
}

/* ---- container parsers ------------------------------------------------- */

/* v1 ("Realtech"): header, then three arrays of per-patch fields (chip id,
 * length, offset), then the patches. The patch for this chip is copied and
 * stamped with the file's version in its last four bytes. */
static int build_v1(const unsigned char *fw, size_t fw_len, unsigned char rom_version,
                    unsigned char **out, size_t *out_len, char *why, int why_cap)
{
    unsigned short patches, wanted;
    size_t table, i;
    const unsigned char *ids, *lengths, *offsets;

    if (fw_len < EPATCH_HEADER) {
        set_why(why, why_cap, "the file is too short");
        return 0;
    }
    patches = (unsigned short)le16(fw + 12);
    if (!patches || patches > 256) {
        set_why(why, why_cap, "it has no patch table");
        return 0;
    }
    table = EPATCH_HEADER + (size_t)patches * 8u;
    if (fw_len < table) {
        set_why(why, why_cap, "the patch table is cut off");
        return 0;
    }
    ids     = fw + EPATCH_HEADER;
    lengths = ids + (size_t)patches * 2u;
    offsets = lengths + (size_t)patches * 2u;
    wanted  = (unsigned short)(rom_version + 1u);

    for (i = 0; i < patches; i++) {
        unsigned short length;
        uint32_t offset;

        if (le16(ids + i * 2u) != wanted) continue;
        length = (unsigned short)le16(lengths + i * 2u);
        offset = le32(offsets + i * 4u);
        if (!offset || length < 4u || offset > fw_len || length > fw_len - offset) {
            set_why(why, why_cap, "its patch for this chip is out of range");
            return 0;
        }
        *out = malloc(length);
        if (!*out) {
            set_why(why, why_cap, "out of memory");
            return 0;
        }
        memcpy(*out, fw + offset, length);
        memcpy(*out + length - 4u, fw + 8, 4);   /* the file's version */
        *out_len = length;
        return 1;
    }
    set_no_patch(why, why_cap, rom_version);
    return 0;
}

struct piece {
    unsigned char priority;
    const unsigned char *data;
    size_t len;
};

/* v2 ("RTBTCore"): a section list. Each section carries subsections for the
 * chip's ROM version (the "eco"); they are applied in priority order.
 * Security sections are only used by chips with a key id, which this loader
 * does not read, so they are skipped. */
static int build_v2(const unsigned char *fw, size_t fw_len, unsigned char rom_version,
                    unsigned char **out, size_t *out_len, char *why, int why_cap)
{
    const unsigned char *sec;
    size_t end, pos, total = 0;
    uint32_t sections;
    struct piece *pieces;
    size_t n = 0, i;
    unsigned char *dst;

    if (fw_len < EPATCH_HEADER_V2 + V2_TAIL) {
        set_why(why, why_cap, "the file is too short");
        return 0;
    }
    end = fw_len - V2_TAIL;
    sections = le32(fw + 16);
    pieces = malloc((fw_len / 8u + 1u) * sizeof *pieces);
    if (!pieces) {
        set_why(why, why_cap, "out of memory");
        return 0;
    }

    pos = EPATCH_HEADER_V2;
    for (uint32_t s = 0; s < sections; s++) {
        uint32_t type, length;
        if (pos + 8 > end) {
            free(pieces);
            set_why(why, why_cap, "the section header is truncated");
            return 0;
        }
        type = le32(fw + pos);
        length = le32(fw + pos + 4);
        pos += 8;
        if (length > end - pos) {
            free(pieces);
            set_why(why, why_cap, "a section is cut off");
            return 0;
        }
        sec = fw + pos;
        if (type == V2_SECTION_SNIPPETS || type == V2_SECTION_DUMMY) {
            unsigned short count;
            size_t at;
            if (length < 4) {
                free(pieces);
                set_why(why, why_cap, "a section is cut off");
                return 0;
            }
            count = (unsigned short)le16(sec);
            at = 4;
            for (unsigned short k = 0; k < count; k++) {
                unsigned char eco, priority;
                uint32_t size;
                if (at + 8 > length) {
                    free(pieces);
                    set_why(why, why_cap, "a subsection is cut off");
                    return 0;
                }
                eco = sec[at];
                priority = sec[at + 1];
                size = le32(sec + at + 4);
                at += 8;
                if (size > length - at) {
                    free(pieces);
                    set_why(why, why_cap, "a subsection is cut off");
                    return 0;
                }
                if (eco == (unsigned char)(rom_version + 1u)) {
                    pieces[n].priority = priority;
                    pieces[n].data = sec + at;
                    pieces[n].len = size;
                    n++;
                    total += size;
                }
                at += size;
            }
        } else if (type == V2_SECTION_SECURITY) {
            /* needs the chip's key id, which this loader does not read */
        }
        pos += length;
    }
    if (!n) {
        free(pieces);
        set_no_patch(why, why_cap, rom_version);
        return 0;
    }
    /* stable insertion sort by priority (files are small) */
    for (i = 1; i < n; i++) {
        struct piece p = pieces[i];
        size_t j = i;
        while (j > 0 && pieces[j - 1].priority > p.priority) {
            pieces[j] = pieces[j - 1];
            j--;
        }
        pieces[j] = p;
    }
    dst = malloc(total ? total : 1u);
    if (!dst) {
        free(pieces);
        set_why(why, why_cap, "out of memory");
        return 0;
    }
    total = 0;
    for (i = 0; i < n; i++) {
        memcpy(dst + total, pieces[i].data, pieces[i].len);
        total += pieces[i].len;
    }
    free(pieces);
    *out = dst;
    *out_len = total;
    return 1;
}

int hb_rtl_build_image(const unsigned char *fw, size_t fw_len,
                       const unsigned char *cfg, size_t cfg_len,
                       unsigned char rom_version,
                       unsigned char **out, size_t *out_len,
                       char *why, int why_cap)
{
    unsigned char *patch = NULL;
    size_t patch_len = 0;
    int ok;

    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!fw || !out || !out_len) {
        set_why(why, why_cap, "no firmware data");
        return 0;
    }
    if (fw_len >= 8 && !memcmp(fw, "Realtech", 8))
        ok = build_v1(fw, fw_len, rom_version, &patch, &patch_len, why, why_cap);
    else if (fw_len >= 8 && !memcmp(fw, "RTBTCore", 8))
        ok = build_v2(fw, fw_len, rom_version, &patch, &patch_len, why, why_cap);
    else {
        set_why(why, why_cap, "it is not a Realtek firmware file");
        return 0;
    }
    if (!ok) return 0;
    if (patch_len > SIZE_MAX - cfg_len) {
        free(patch);
        set_why(why, why_cap, "the firmware is too large");
        return 0;
    }
    *out = malloc(patch_len + cfg_len);
    if (!*out) {
        free(patch);
        set_why(why, why_cap, "out of memory");
        return 0;
    }
    memcpy(*out, patch, patch_len);
    if (cfg_len) memcpy(*out + patch_len, cfg, cfg_len);
    *out_len = patch_len + cfg_len;
    free(patch);
    return 1;
}

/* ---- firmware files and upload ----------------------------------------- */

/* Read at most cap bytes from dir/name. Optional files (required == 0) that
 * do not exist are reported as empty. */
static int read_file(const char *dir, const char *name, int required,
                     unsigned char *buf, size_t cap, size_t *len,
                     char *why, int why_cap)
{
    char path[256];
    FILE *f;
    size_t n = 0;

    *len = 0;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f) {
        if (!required) return 1;
        if (why && why_cap > 0)
            snprintf(why, (size_t)why_cap,
                     "this adapter needs firmware: put %s in %s/", name, dir);
        return 0;
    }
    while (n < cap) {
        size_t r = fread(buf + n, 1, cap - n, f);
        n += r;
        if (r == 0) break;
    }
    fclose(f);
    if (n == cap) {
        if (why && why_cap > 0)
            snprintf(why, (size_t)why_cap, "%s is too large (limit %u bytes)", name, (unsigned)cap);
        return 0;
    }
    *len = n;
    return 1;
}

int hb_rtl_upload(hci_t hci, const unsigned char *image, size_t image_len,
                  char *why, int why_cap)
{
    unsigned char cc[64], frag[FRAGMENT_SIZE + 1];
    int cc_len = 0;
    size_t count, i, index = 0;

    if (!image || !image_len) {
        set_why(why, why_cap, "the firmware image is empty");
        return 0;
    }
    count = image_len / FRAGMENT_SIZE + 1u;
    for (i = 0; i < count; i++) {
        unsigned char header = (unsigned char)index++;
        size_t length = FRAGMENT_SIZE;
        size_t at = i * FRAGMENT_SIZE;

        if (header == 0x7f) index = 1;     /* 0x80 marks the last fragment */
        if (i == count - 1) {
            header |= 0x80;
            length = image_len % FRAGMENT_SIZE;
        }
        frag[0] = header;
        if (length) memcpy(frag + 1, image + at, length);
        if (!hci_cmd_sync_once(hci, OP_DOWNLOAD_PATCH, frag, (int)(length + 1u),
                               cc, &cc_len, (int)sizeof cc)) {
            if (why && why_cap > 0)
                snprintf(why, (size_t)why_cap,
                         "the adapter stopped accepting the firmware (fragment %u of %u)",
                         (unsigned)(i + 1u), (unsigned)count);
            return 0;
        }
    }
    if (!hci_cmd_sync(hci, HB_OP_RESET, NULL, 0, cc, &cc_len, (int)sizeof cc)) {
        set_why(why, why_cap, "the adapter did not answer the reset after the firmware upload");
        return 0;
    }
    return 1;
}

static int read_rom_version(hci_t hci, unsigned char *rom_version, char *why, int why_cap)
{
    unsigned char cc[64];
    int cc_len = 0;

    if (!hci_cmd_sync(hci, OP_READ_ROM_VERSION, NULL, 0, cc, &cc_len, (int)sizeof cc) ||
        cc_len < 7 || cc[5] != 0) {
        set_why(why, why_cap, "the adapter did not report its Realtek ROM version");
        return 0;
    }
    *rom_version = cc[6];
    return 1;
}

static int install_chip(hci_t hci, const hb_rtl_chip *chip, char *why, int why_cap)
{
    unsigned char *fw = NULL, *cfg = NULL, *image = NULL;
    unsigned char rom_version;
    size_t fw_len = 0, cfg_len = 0, image_len = 0;
    int ok = 0, have_cfg;

    if (!read_rom_version(hci, &rom_version, why, why_cap)) return 0;
    fw = malloc(MAX_FIRMWARE_FILE);
    if (!fw) {
        set_why(why, why_cap, "out of memory");
        return 0;
    }
    cfg = malloc(MAX_FIRMWARE_FILE);
    if (!cfg) {
        free(fw);
        set_why(why, why_cap, "out of memory");
        return 0;
    }
    if (!read_file(HB_RTL_FW_DIR, chip->firmware_file, 1, fw, MAX_FIRMWARE_FILE,
                   &fw_len, why, why_cap))
        goto out;
    have_cfg = read_file(HB_RTL_FW_DIR, chip->config_file, chip->config_required, cfg,
                         MAX_FIRMWARE_FILE, &cfg_len, why, why_cap);
    if (!have_cfg) goto out;
    if (!hb_rtl_build_image(fw, fw_len, cfg, cfg_len, rom_version, &image, &image_len,
                            why, why_cap))
        goto out;
    if (!hb_rtl_upload(hci, image, image_len, why, why_cap)) goto out;
    log_line("adapter: %s firmware %s uploaded (%u bytes in %u fragments)", chip->name,
             chip->firmware_file, (unsigned)image_len,
             (unsigned)(image_len / FRAGMENT_SIZE + 1u));
    ok = 1;
out:
    free(image);
    free(cfg);
    free(fw);
    return ok;
}

int hb_rtl_prepare(hci_t hci, char *why, int why_cap)
{
    unsigned char cc[64];
    int cc_len = 0;
    hb_chip_identity id;
    const hb_rtl_chip *chip;

    if (why && why_cap > 0) why[0] = 0;
    if (!hci_cmd_sync(hci, HB_OP_READ_LOCAL_VERSION, NULL, 0, cc, &cc_len, (int)sizeof cc)) {
        log_line("adapter: local version not answered, no firmware step");
        return 1;
    }
    if (!hb_chip_identity_from_cc(cc, cc_len, &id)) return 1;
    log_line("adapter: HCI %#04x, LMP %#04x, manufacturer %#06x, revision %#04x/%#04x",
             (unsigned)id.hci_version, (unsigned)id.lmp_version, (unsigned)id.manufacturer,
             (unsigned)id.hci_revision, (unsigned)id.lmp_subversion);
    if (id.manufacturer != REALTEK_MANUFACTURER) return 1;

    chip = hb_rtl_chip_for(&id);
    if (!chip) {
        if (!hb_rtl_in_rom_mode(&id)) return 1;
        log_line("adapter: Realtek %#06x/%#06x/%#04x is not a chip with a known patch, starting as it is",
                 (unsigned)id.lmp_subversion, (unsigned)id.hci_revision, (unsigned)id.hci_version);
        return 1;
    }
    log_line("adapter: %s in ROM mode, uploading %s", chip->name, chip->firmware_file);
    if (!install_chip(hci, chip, why, why_cap)) {
        log_line("adapter: %s", why && why[0] ? why : "firmware upload failed");
        return 0;
    }
    return 1;
}
