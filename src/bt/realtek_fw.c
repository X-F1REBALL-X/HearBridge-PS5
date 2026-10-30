/* realtek_fw.c - see realtek_fw.h.
 *
 * The container formats, the project ids, the chip table, the ordering of the
 * v2 subsections and the fragment layout follow Linux's btrtl driver
 * (drivers/bluetooth/btrtl.c, GPL-2.0-or-later), which is what the chips
 * accept. The file is looked up by the Read Local Version identity, exactly
 * like btrtl does. */
#include "realtek_fw.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hci_cmd.h"
#include "log.h"
#include "rtl_fw_blobs.h"
#include "util.h"

#define OP_READ_ROM_VERSION 0xfc6d
#define OP_DOWNLOAD_PATCH   0xfc20
#define OP_READ_REG16       0xfc61
#define OP_DROP_FIRMWARE    0xfc66

/* Optional BD_ADDR (wire order) to inject into the vendor config at offset
 * 0x0030 on the next install_chip / prepare upload. */
static unsigned char g_bdaddr_override[6];
static int g_bdaddr_override_set;

void hb_rtl_bdaddr_override(const unsigned char addr[6])
{
    if (!addr) {
        g_bdaddr_override_set = 0;
        memset(g_bdaddr_override, 0, sizeof g_bdaddr_override);
        return;
    }
    memcpy(g_bdaddr_override, addr, 6);
    g_bdaddr_override_set = 1;
}

/* Realtek vendor config: magic 0x8723ab55, total_len, then entries
 * {offset le16, len u8, data[]}. BD_ADDR lives at offset 0x0030. */
static void inject_bdaddr_config(unsigned char *cfg, size_t *cfg_len, size_t cap)
{
    char astr[18];
    if (!g_bdaddr_override_set || !cfg || !cfg_len || cap < 15)
        return;
    cfg[0] = 0x55; cfg[1] = 0xab; cfg[2] = 0x23; cfg[3] = 0x87;
    cfg[4] = 0x09; cfg[5] = 0x00;           /* total_len = 9 */
    cfg[6] = 0x30; cfg[7] = 0x00;           /* offset 0x0030 */
    cfg[8] = 0x06;                          /* 6 address bytes */
    memcpy(cfg + 9, g_bdaddr_override, 6);
    *cfg_len = 15;
    hci_addr_str(g_bdaddr_override, astr);
    log_line("adapter: Realtek config BD_ADDR %s (offset 0x0030)", astr);
}


#define REALTEK_MANUFACTURER 0x005d
#define FRAGMENT_SIZE        252
#define MAX_FIRMWARE_FILE    (512u * 1024u)

#define EPATCH_HEADER        14u  /* signature(8) + version(4) + patches(2) */
#define EPATCH_HEADER_V2     20u  /* signature(8) + version(8) + sections(4) */
#define EPATCH_EXTENSION     7u   /* version instruction (3) + signature (4) */

#define V2_SECTION_SNIPPETS  1u
#define V2_SECTION_DUMMY     2u
#define V2_SECTION_SECURITY  3u

/* The extension at the end of every container, and the register that holds
 * the security key id (RTL_SEC_PROJ), read with 0xfc61. */
static const unsigned char g_extension_sig[4] = { 0x51, 0x04, 0xfd, 0x77 };
static const unsigned char g_sec_proj[5] = { 0x10, 0xa4, 0xad, 0x00, 0xb0 };

/* The USB parts btrtl knows, with the firmware file for each. UART variants
 * and the 8723A (older download format) are left out. An hci_version of 0 is
 * a wildcard, as in btrtl's table (the RTL8761CU row uses it). */
static const hb_rtl_chip g_chips[] = {
    { 0x8723, 0x000b, 0x06, "RTL8723BU", "rtl8723b_fw.bin", "rtl8723b_config.bin", 0, NULL },
    { 0x8723, 0x000d, 0x08, "RTL8723DU", "rtl8723d_fw.bin", "rtl8723d_config.bin", 1, NULL },
    { 0x8821, 0x000a, 0x06, "RTL8821AU", "rtl8821a_fw.bin", "rtl8821a_config.bin", 0, NULL },
    { 0x8821, 0x000c, 0x08, "RTL8821CU", "rtl8821c_fw.bin", "rtl8821c_config.bin", 0, NULL },
    { 0x8761, 0x000a, 0x06, "RTL8761AU", "rtl8761a_fw.bin", "rtl8761a_config.bin", 0, NULL },
    { 0x8761, 0x000b, 0x0a, "RTL8761BU", "rtl8761bu_fw.bin", "rtl8761bu_config.bin", 0, NULL },
    { 0x8761, 0x000e, 0x00, "RTL8761CU", "rtl8761cu_fw.bin", "rtl8761cu_config.bin", 0, NULL },
    { 0x8822, 0x000b, 0x07, "RTL8822BU", "rtl8822b_fw.bin", "rtl8822b_config.bin", 1, NULL },
    { 0x8822, 0x000c, 0x0a, "RTL8822CU", "rtl8822cu_fw.bin", "rtl8822cu_config.bin", 0, NULL },
    { 0x8852, 0x000a, 0x0b, "RTL8852AU", "rtl8852au_fw.bin", "rtl8852au_config.bin", 0, NULL },
    { 0x8852, 0x000b, 0x0b, "RTL8852BU", "rtl8852bu_fw.bin", "rtl8852bu_config.bin", 0, NULL },
    { 0x8852, 0x000c, 0x0c, "RTL8852CU", "rtl8852cu_fw.bin", "rtl8852cu_config.bin", 0,
      "rtl8852cu_fw_v2.bin" },
    { 0x8851, 0x000b, 0x0c, "RTL8851BU", "rtl8851bu_fw.bin", "rtl8851bu_config.bin", 0, NULL },
    { 0x8922, 0x000a, 0x0c, "RTL8922AU", "rtl8922au_fw.bin", "rtl8922au_config.bin", 0, NULL },
    { 0x8852, 0x0087, 0x0c, "RTL8852BTU", "rtl8852btu_fw.bin", "rtl8852btu_config.bin", 0, NULL },
};

/* What the version instruction in the file's extension names: the project id
 * of a chip family. btrtl compares the resulting LMP subversion with the
 * chip's. */
static const struct {
    unsigned char  id;
    unsigned short lmp_subversion;
} g_projects[] = {
    { 0, 0x8723 },  { 1, 0x8723 },  { 2, 0x8821 },  { 3, 0x8761 },
    { 7, 0x8703 },  { 8, 0x8822 },  { 9, 0x8723 },  { 10, 0x8821 },
    { 13, 0x8822 }, { 14, 0x8761 }, { 18, 0x8852 }, { 20, 0x8852 },
    { 25, 0x8852 }, { 36, 0x8851 }, { 44, 0x8922 }, { 47, 0x8852 },
    { 51, 0x8761 },
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

/* btrtl walks backwards from the extension signature to find the instruction
 * that names the project id (opcode 0, length 1, the id is its data byte).
 * Returns -1 when no such instruction is there. */
static int project_id_of(const unsigned char *fw, size_t fw_len, size_t floor)
{
    size_t at = fw_len - sizeof g_extension_sig;

    while (at >= floor + 3u) {
        unsigned char opcode = fw[--at];
        unsigned char length = fw[--at];
        unsigned char value = fw[--at];

        if (opcode == 0xff) break;                 /* end of the instructions */
        if (!length || length > at) return -1;
        if (opcode == 0 && length == 1) return (int)value;
        at -= length;
    }
    return -1;
}

/* The LMP subversion a project id belongs to, or -1 when it is not a known
 * one. */
static int project_lmp(unsigned char project)
{
    size_t i;
    for (i = 0; i < sizeof g_projects / sizeof g_projects[0]; i++)
        if (g_projects[i].id == project) return (int)g_projects[i].lmp_subversion;
    return -1;
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
            (g_chips[i].hci_version == 0 || g_chips[i].hci_version == id->hci_version))
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
 * chip's ROM version (the "eco"); they are applied in priority order. Security
 * sections are only used by chips with a key id, and then only the subsections
 * for that key id. */
static int build_v2(const unsigned char *fw, size_t fw_len, unsigned char rom_version,
                    unsigned char key_id, unsigned char **out, size_t *out_len,
                    char *why, int why_cap)
{
    const unsigned char *sec;
    size_t end, pos, total = 0;
    uint32_t sections;
    struct piece *pieces;
    size_t n = 0, i;
    unsigned char *dst;

    if (fw_len < EPATCH_HEADER_V2 + EPATCH_EXTENSION) {
        set_why(why, why_cap, "the file is too short");
        return 0;
    }
    end = fw_len - EPATCH_EXTENSION;
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
        if (type == V2_SECTION_SECURITY && !key_id) {
            /* a chip without a key id ignores every security section */
        } else if (type == V2_SECTION_SNIPPETS || type == V2_SECTION_DUMMY ||
                   type == V2_SECTION_SECURITY) {
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
                unsigned char eco, priority, section_key;
                uint32_t size;
                if (at + 8 > length) {
                    free(pieces);
                    set_why(why, why_cap, "a subsection is cut off");
                    return 0;
                }
                eco = sec[at];
                priority = sec[at + 1];
                section_key = sec[at + 2];   /* cb[0] for snippets, key id for security */
                size = le32(sec + at + 4);
                at += 8;
                if (size > length - at) {
                    free(pieces);
                    set_why(why, why_cap, "a subsection is cut off");
                    return 0;
                }
                if (eco == (unsigned char)(rom_version + 1) &&
                    (type != V2_SECTION_SECURITY || section_key == key_id)) {
                    pieces[n].priority = priority;
                    pieces[n].data = sec + at;
                    pieces[n].len = size;
                    n++;
                    total += size;
                }
                at += size;
            }
        }
        pos += length;
    }
    if (!n) {
        free(pieces);
        set_no_patch(why, why_cap, rom_version);
        return 0;
    }
    /* btrtl inserts before the first subsection whose priority is >= the new
     * one, so equal priorities end up in reverse file order; keep that order
     * to build the same patch as Linux does. */
    for (i = 1; i < n; i++) {
        struct piece p = pieces[i];
        size_t j = i;
        while (j > 0 && pieces[j - 1].priority >= p.priority) {
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
                       const hb_rtl_target *target,
                       unsigned char **out, size_t *out_len,
                       char *why, int why_cap)
{
    unsigned char *patch = NULL;
    size_t patch_len = 0, header, extra;
    int ok, project, expected;

    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!fw || !out || !out_len || !target) {
        set_why(why, why_cap, "no firmware data");
        return 0;
    }
    if (fw_len >= 8 && !memcmp(fw, "Realtech", 8))
        header = EPATCH_HEADER;
    else if (fw_len >= 8 && !memcmp(fw, "RTBTCore", 8))
        header = EPATCH_HEADER_V2;
    else {
        set_why(why, why_cap, "it is not a Realtek firmware file");
        return 0;
    }
    if (fw_len < header + EPATCH_EXTENSION) {
        set_why(why, why_cap, "the file is too short");
        return 0;
    }
    if (memcmp(fw + fw_len - sizeof g_extension_sig, g_extension_sig,
               sizeof g_extension_sig)) {
        set_why(why, why_cap, "its extension section is missing or damaged");
        return 0;
    }
    project = project_id_of(fw, fw_len, header);
    if (project < 0) {
        set_why(why, why_cap, "it has no project id");
        return 0;
    }
    expected = project_lmp((unsigned char)project);
    if (expected < 0) {
        if (why && why_cap > 0)
            snprintf(why, (size_t)why_cap, "its project id %d is not a known Realtek chip", project);
        return 0;
    }
    if ((unsigned short)expected != target->lmp_subversion) {
        if (why && why_cap > 0)
            snprintf(why, (size_t)why_cap, "it is for another chip (project id %d)", project);
        return 0;
    }
    if (header == EPATCH_HEADER)
        ok = build_v1(fw, fw_len, target->rom_version, &patch, &patch_len, why, why_cap);
    else
        ok = build_v2(fw, fw_len, target->rom_version, target->key_id, &patch, &patch_len,
                      why, why_cap);
    if (!ok) return 0;
    /* the chip parses the vendor config after the patch; a chip with a key id
     * takes its settings from the security sections instead (btrtl skips the
     * config file for those) */
    extra = target->key_id ? 0 : cfg_len;
    if (patch_len > SIZE_MAX - extra) {
        free(patch);
        set_why(why, why_cap, "the firmware is too large");
        return 0;
    }
    *out = malloc(patch_len + extra);
    if (!*out) {
        free(patch);
        set_why(why, why_cap, "out of memory");
        return 0;
    }
    memcpy(*out, patch, patch_len);
    if (extra) memcpy(*out + patch_len, cfg, extra);
    *out_len = patch_len + extra;
    free(patch);
    return 1;
}

/* ---- firmware files and upload ----------------------------------------- */

/* Read at most cap bytes from dir/name. Returns 0 when the file is missing or
 * unreadable; `why` is set only when the file exists but is unusable (too
 * large), so the caller can tell a missing file from a broken one. */
static int read_file(const char *dir, const char *name,
                     unsigned char *buf, size_t cap, size_t *len,
                     char *why, int why_cap)
{
    char path[256];
    FILE *f;
    size_t n = 0;

    *len = 0;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f) return 0;
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

static void set_missing(char *why, int cap, const hb_rtl_chip *chip, int with_config)
{
    char names[160];

    if (chip->firmware_file_v2)
        snprintf(names, sizeof names, "%s or %s", chip->firmware_file_v2, chip->firmware_file);
    else
        snprintf(names, sizeof names, "%s", chip->firmware_file);
    if (!why || cap <= 0) return;
    if (with_config)
        snprintf(why, (size_t)cap, "this adapter needs firmware: put %s and %s in %s/",
                 names, chip->config_file, HB_RTL_FW_DIR);
    else
        snprintf(why, (size_t)cap, "this adapter needs firmware: put %s in %s/",
                 names, HB_RTL_FW_DIR);
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
                         "the adapter stopped accepting the firmware (fragment %u of %u); "
                         "unplug and replug the adapter",
                         (unsigned)(i + 1u), (unsigned)count);
            return 0;
        }
    }
    if (!hci_cmd_sync(hci, HB_OP_RESET, NULL, 0, cc, &cc_len, (int)sizeof cc)) {
        set_why(why, why_cap, "the adapter did not answer the reset after the firmware upload; "
                              "unplug and replug the adapter");
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

/* RTL_SEC_PROJ through 0xfc61: status, then the register (low byte first). */
static int read_key_id(hci_t hci, unsigned char *key_id, char *why, int why_cap)
{
    unsigned char cc[64];
    int cc_len = 0;

    if (!hci_cmd_sync(hci, OP_READ_REG16, g_sec_proj, (int)sizeof g_sec_proj,
                      cc, &cc_len, (int)sizeof cc) ||
        cc_len < 8 || cc[5] != 0) {
        set_why(why, why_cap, "the adapter did not report its Realtek key id");
        return 0;
    }
    *key_id = cc[6];
    return 1;
}

static int install_chip(hci_t hci, const hb_rtl_chip *chip, char *why, int why_cap)
{
    unsigned char *fw = NULL, *cfg = NULL, *image = NULL;
    unsigned char rom_version, key_id = 0;
    const char *first, *used;
    hb_rtl_target target;
    size_t fw_len = 0, cfg_len = 0, image_len = 0;
    int ok = 0;

    if (!read_rom_version(hci, &rom_version, why, why_cap)) return 0;
    /* The key id decides whether the patch lives in the v2 security sections
     * and whether the config file is used at all (btrtl reads it always). */
    if (!read_key_id(hci, &key_id, why, why_cap)) return 0;
    log_line("adapter: %s key id %u", chip->name, (unsigned)key_id);
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
    first = chip->firmware_file_v2 ? chip->firmware_file_v2 : chip->firmware_file;
    used = first;
    if (!read_file(HB_RTL_FW_DIR, first, fw, MAX_FIRMWARE_FILE, &fw_len, why, why_cap)) {
        if (why && why[0]) goto out;    /* exists but is unusable */
        used = chip->firmware_file;
        if (!chip->firmware_file_v2 ||
            !read_file(HB_RTL_FW_DIR, used, fw, MAX_FIRMWARE_FILE, &fw_len, why, why_cap)) {
            if (!why || !why[0]) set_missing(why, why_cap, chip, 0);
            goto out;
        }
    }
    if (!key_id) {
        if (!read_file(HB_RTL_FW_DIR, chip->config_file, cfg, MAX_FIRMWARE_FILE,
                       &cfg_len, NULL, 0)) {
            if (chip->config_required) {
                set_missing(why, why_cap, chip, 1);
                goto out;
            }
            cfg_len = 0;
        }
        /* Shared host address overrides any factory config BD_ADDR entry. */
        inject_bdaddr_config(cfg, &cfg_len, MAX_FIRMWARE_FILE);
    }
    target.lmp_subversion = chip->lmp_subversion;
    target.rom_version = rom_version;
    target.key_id = key_id;
    if (!hb_rtl_build_image(fw, fw_len, cfg, cfg_len, &target, &image, &image_len,
                            why, why_cap))
        goto out;
    if (!hb_rtl_upload(hci, image, image_len, why, why_cap)) goto out;
    log_line("adapter: %s firmware %s uploaded (%u bytes in %u fragments)", chip->name,
             used, (unsigned)image_len, (unsigned)(image_len / FRAGMENT_SIZE + 1u));
    ok = 1;
out:
    free(image);
    free(cfg);
    free(fw);
    return ok;
}

static int read_identity(hci_t hci, hb_chip_identity *id)
{
    unsigned char cc[64];
    int cc_len = 0;

    if (!hci_cmd_sync(hci, HB_OP_READ_LOCAL_VERSION, NULL, 0, cc, &cc_len,
                      (int)sizeof cc))
        return 0;
    return hb_chip_identity_from_cc(cc, cc_len, id);
}


/* ---- bundled firmware install ------------------------------------------ */

static int file_size_matches(const char *path, size_t expect)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (size_t)st.st_size == expect;
}

static int write_blob(const char *dir, const hb_rtl_fw_blob *blob)
{
    char path[256], tmp[280];
    FILE *f;
    size_t n;

    if (!blob || !blob->name || !blob->data) return 0;
    snprintf(path, sizeof path, "%s/%s", dir, blob->name);
    if (file_size_matches(path, blob->len)) return 0;   /* already current */

    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) {
        log_line("adapter: cannot write %s (errno %d)", tmp, errno);
        return 0;
    }
    n = fwrite(blob->data, 1, blob->len, f);
    if (fclose(f) != 0 || n != blob->len) {
        unlink(tmp);
        log_line("adapter: short write for %s", blob->name);
        return 0;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        log_line("adapter: cannot install %s (errno %d)", path, errno);
        return 0;
    }
    log_line("adapter: installed bundled firmware %s (%u bytes)", blob->name,
             (unsigned)blob->len);
    return 1;
}

int hb_rtl_ensure_firmware_dir(const char *dir)
{
    size_t i;
    int wrote = 0;

    if (!dir || !dir[0]) dir = HB_RTL_FW_DIR;
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        log_line("adapter: cannot create %s (errno %d)", dir, errno);
        return -1;
    }
    for (i = 0; i < HB_RTL_FW_BLOB_COUNT; i++)
        wrote += write_blob(dir, &hb_rtl_fw_blobs[i]);
    return wrote;
}

int hb_rtl_ensure_firmware(void)
{
    return hb_rtl_ensure_firmware_dir(HB_RTL_FW_DIR);
}

int hb_rtl_prepare(hci_t hci, char *why, int why_cap)
{
    hb_chip_identity id;
    const hb_rtl_chip *chip;
    const char *file;

    if (why && why_cap > 0) why[0] = 0;
    /* Make sure the bundled RTL8761BU files are on disk before we look them up. */
    hb_rtl_ensure_firmware();
    if (!read_identity(hci, &id)) {
        log_line("adapter: local version not answered, no firmware step");
        return 1;
    }
    log_line("adapter: HCI %#04x, LMP %#04x, manufacturer %#06x, revision %#04x/%#04x",
             (unsigned)id.hci_version, (unsigned)id.lmp_version, (unsigned)id.manufacturer,
             (unsigned)id.hci_revision, (unsigned)id.lmp_subversion);
    if (id.manufacturer != REALTEK_MANUFACTURER) return 1;

    chip = hb_rtl_chip_for(&id);
    if (!chip) {
        hb_chip_identity again;

        /* A Realtek that matches no row may already carry a vendor patch, whose
         * numbers are not the ROM ones. btrtl sends 0xfc66 to drop it, waits
         * 200 ms and reads the version again. */
        if (hci.ops && hci.ops->cmd)
            hci.ops->cmd(hci.ctx, OP_DROP_FIRMWARE, NULL, 0);
        usleep(200 * 1000);
        if (read_identity(hci, &again)) id = again;
        chip = hb_rtl_chip_for(&id);
    }
    if (!chip) {
        if (!hb_rtl_in_rom_mode(&id)) return 1;
        log_line("adapter: Realtek %#06x/%#06x/%#04x is not a chip with a known patch, starting as it is",
                 (unsigned)id.lmp_subversion, (unsigned)id.hci_revision, (unsigned)id.hci_version);
        return 1;
    }
    file = chip->firmware_file_v2 ? chip->firmware_file_v2 : chip->firmware_file;
    log_line("adapter: %s in ROM mode, uploading %s", chip->name, file);
    if (!install_chip(hci, chip, why, why_cap)) {
        log_line("adapter: %s", why && why[0] ? why : "firmware upload failed");
        return 0;
    }
    return 1;
}

int hb_rtl_repatch_bdaddr(hci_t hci, const unsigned char addr[6])
{
    hb_chip_identity id;
    const hb_rtl_chip *chip;
    char why[160];

    if (!addr) return 0;
    hb_rtl_bdaddr_override(addr);
    hb_rtl_ensure_firmware();

    /* Drop loaded firmware back to ROM so the next upload carries the config. */
    if (hci.ops && hci.ops->cmd)
        hci.ops->cmd(hci.ctx, OP_DROP_FIRMWARE, NULL, 0);
    usleep(200 * 1000);

    if (!read_identity(hci, &id)) {
        log_line("adapter: Realtek repatch: no local version after drop");
        return 0;
    }
    chip = hb_rtl_chip_for(&id);
    if (!chip) {
        log_line("adapter: Realtek repatch: chip still unknown after 0xFC66");
        return 0;
    }
    why[0] = 0;
    log_line("adapter: %s re-uploading with shared BD_ADDR", chip->name);
    if (!install_chip(hci, chip, why, (int)sizeof why)) {
        log_line("adapter: Realtek repatch failed: %s", why[0] ? why : "unknown");
        return 0;
    }
    return 1;
}
