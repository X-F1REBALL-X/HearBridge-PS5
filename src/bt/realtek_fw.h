/* realtek_fw.h - Realtek USB Bluetooth patch uploader (btrtl-like).
 *
 * Some Realtek sticks boot into a ROM that answers HCI and can be opened,
 * but whose radio cannot scan until the host uploads the matching patch.
 * HearBridge ships the common RTL8761BU files and writes them under the
 * firmware directory below on start (same layout as Linux's btrtl). Other
 * chips still accept a manual copy into that folder.
 *
 * The parsers and the fragmenter are pure functions (host-tested in
 * tests/test_realtek_fw.c); only hb_rtl_prepare() talks to the controller.
 */
#ifndef HEARBRIDGE_REALTEK_FW_H
#define HEARBRIDGE_REALTEK_FW_H

#include <stddef.h>

#include "hci.h"

#define HB_RTL_FW_DIR "/data/hearbridge/firmware"

/* Identity from Read Local Version (0x1001), Core Vol 4 Part E 7.4.1. */
typedef struct {
    unsigned char  hci_version;
    unsigned short hci_revision;
    unsigned char  lmp_version;
    unsigned short manufacturer;
    unsigned short lmp_subversion;
} hb_chip_identity;

/* Parse the Command Complete event of Read Local Version. 1 on success. */
int hb_chip_identity_from_cc(const unsigned char *ev, int len, hb_chip_identity *out);

typedef struct {
    unsigned short lmp_subversion;
    unsigned short hci_revision;
    unsigned char  hci_version;
    const char    *name;
    const char    *firmware_file;
    const char    *config_file;
    int            config_required;
    const char    *firmware_file_v2;  /* tried first when set (RTL8852C) */
} hb_rtl_chip;

/* What the container parser needs to know about the chip. */
typedef struct {
    unsigned short lmp_subversion; /* the file's project id must be for this chip */
    unsigned char  rom_version;    /* selects the patch (eco = rom_version + 1) */
    unsigned char  key_id;         /* 0: no security sections, the config is used */
} hb_rtl_target;

/* The patch that matches this exact ROM identity, or NULL. */
const hb_rtl_chip *hb_rtl_chip_for(const hb_chip_identity *id);

/* 1 when the LMP subversion belongs to a Realtek USB part that can still be
 * in ROM mode (the identity alone cannot tell a patched chip: btrtl matches
 * the pair of revision/version for that). */
int hb_rtl_in_rom_mode(const hb_chip_identity *id);

/* Build the image the chip expects from a firmware file and an optional
 * config file: check the extension and project id, select the Realtech v1 or
 * RTBTCore v2 patch for rom_version, order v2 sections by priority (including
 * the key-id security sections) and append the config blob unless the chip has
 * a key id. On success *out is a malloc'd buffer (caller frees) and *out_len
 * its size. */
int hb_rtl_build_image(const unsigned char *fw, size_t fw_len,
                       const unsigned char *cfg, size_t cfg_len,
                       const hb_rtl_target *target,
                       unsigned char **out, size_t *out_len,
                       char *why, int why_cap);

/* Upload an image in 252-byte fragments with 0xfc20, then HCI Reset. */
int hb_rtl_upload(hci_t hci, const unsigned char *image, size_t image_len,
                  char *why, int why_cap);

/* Write bundled Realtek firmware blobs into dir when missing or the size
 * differs (so a newer ELF refreshes the files). dir NULL uses HB_RTL_FW_DIR.
 * Returns the number of files written, or -1 if the directory cannot be made. */
int hb_rtl_ensure_firmware_dir(const char *dir);

/* Same as hb_rtl_ensure_firmware_dir(HB_RTL_FW_DIR). Call once after the
 * state directory exists so USB adapters find their patch without a manual copy. */
int hb_rtl_ensure_firmware(void);

/* Read Local Version; when this is a Realtek USB part still in ROM mode,
 * read its firmware files from HB_RTL_FW_DIR and upload the patch.
 * 1 = carry on (no step needed or the upload succeeded), 0 = stop and set
 * why (for example: the firmware file is missing). */
int hb_rtl_prepare(hci_t hci, char *why, int why_cap);

#endif
