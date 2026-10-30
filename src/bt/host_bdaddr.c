/* host_bdaddr.c - see host_bdaddr.h. */
#include "host_bdaddr.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hci_cmd.h"
#include "log.h"
#include "realtek_fw.h"

/* ---- file -------------------------------------------------------------- */

static const char *host_path(void)
{
    return HB_HOST_BDADDR_PATH;
}

static int parse_addr_line(const char *line, unsigned char out[6])
{
    unsigned b[6];
    int n, i;

    if (!line || !out) return 0;
    n = sscanf(line, "%02x:%02x:%02x:%02x:%02x:%02x",
               &b[5], &b[4], &b[3], &b[2], &b[1], &b[0]);
    if (n != 6) return 0;
    for (i = 0; i < 6; i++) {
        if (b[i] > 0xff) return 0;
        out[i] = (unsigned char)b[i];
    }
    return 1;
}

int hb_host_bdaddr_load(unsigned char out[6])
{
    FILE *f;
    char line[64];

    if (!out) return 0;
    f = fopen(host_path(), "r");
    if (!f) return 0;
    if (!fgets(line, sizeof line, f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    line[strcspn(line, "\r\n")] = 0;
    return parse_addr_line(line, out);
}

int hb_host_bdaddr_save(const unsigned char addr[6])
{
    FILE *f;
    char astr[18];

    if (!addr) return 0;
    hci_addr_str(addr, astr);
    f = fopen(host_path(), "w");
    if (!f) {
        log_line("host_bdaddr: cannot write %s", host_path());
        return 0;
    }
    fprintf(f, "%s\n", astr);
    fclose(f);
    log_line("host_bdaddr: saved %s", astr);
    return 1;
}

static int read_bd_addr(hci_t hci, unsigned char out[6])
{
    unsigned char cc[32];
    int cc_len = 0;

    if (!hci_cmd_sync(hci, HB_OP_READ_BD_ADDR, NULL, 0, cc, &cc_len, (int)sizeof cc)
        || cc_len < 12 || cc[5] != 0)
        return 0;
    memcpy(out, cc + 6, 6);
    return 1;
}

static int same_addr(const unsigned char a[6], const unsigned char b[6])
{
    return memcmp(a, b, 6) == 0;
}

/* ---- CSR: BCCMD PSKEY_BDADDR + warm reset (BlueZ tools/bdaddr.c) -------- */

#define HB_OP_CSR_BCCMD 0xfc00

static int csr_vendor(hci_t hci, const unsigned char *body, int body_len,
                      unsigned char *rp, int *rp_len, int rp_max)
{
    unsigned char cp[64];
    int n;

    if (body_len < 0 || body_len > 60) return 0;
    cp[0] = 0xc2;
    memcpy(cp + 1, body, (size_t)body_len);
    n = 0;
    if (!hci_cmd_vendor(hci, HB_OP_CSR_BCCMD, cp, body_len + 1, rp, &n, rp_max))
        return 0;
    if (rp_len) *rp_len = n;
    if (n < 1 || rp[0] != 0xc2) {
        log_line("host_bdaddr: CSR BCCMD reply missing 0xc2 header");
        return 0;
    }
    return 1;
}

static int csr_write_bdaddr(hci_t hci, const unsigned char addr[6])
{
    /* BlueZ csr_write_bd_addr: PSKEY_BDADDR layout with zero pads. */
    unsigned char cmd[] = {
        0x02, 0x00, 0x0c, 0x00, 0x11, 0x47, 0x03, 0x70,
        0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    unsigned char rp[64];
    int rp_len = 0;

    cmd[16] = addr[2];
    cmd[17] = 0x00;
    cmd[18] = addr[0];
    cmd[19] = addr[1];
    cmd[20] = addr[3];
    cmd[21] = 0x00;
    cmd[22] = addr[4];
    cmd[23] = addr[5];

    if (!csr_vendor(hci, cmd, (int)sizeof cmd, rp, &rp_len, (int)sizeof rp))
        return 0;
    /* Status words at offsets 9..10 of the vendor payload (after 0xc2). */
    if (rp_len >= 11 && (rp[9] | (rp[10] << 8)) != 0) {
        log_line("host_bdaddr: CSR PSKEY_BDADDR status %#04x",
                 (unsigned)(rp[9] | (rp[10] << 8)));
        return 0;
    }
    return 1;
}

static int csr_warm_reset(hci_t hci)
{
    unsigned char cmd[] = {
        0x02, 0x00, 0x09, 0x00,
        0x00, 0x00, 0x01, 0x40, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    unsigned char rp[64];
    int rp_len = 0;

    /* Warm reset often yields no tidy reply; fire and settle. */
    (void)csr_vendor(hci, cmd, (int)sizeof cmd, rp, &rp_len, (int)sizeof rp);
    usleep(500 * 1000);
    return 1;
}

static int csr_apply(hci_t hci, const unsigned char want[6])
{
    unsigned char cc[32];
    int cc_len = 0;
    char astr[18];

    hci_addr_str(want, astr);
    if (!csr_write_bdaddr(hci, want)) {
        log_line("host_bdaddr: CSR write %s failed", astr);
        return 0;
    }
    log_line("host_bdaddr: CSR PSKEY_BDADDR set to %s, warm reset", astr);
    csr_warm_reset(hci);
    if (!hci_cmd_sync(hci, HB_OP_RESET, NULL, 0, cc, &cc_len, (int)sizeof cc)) {
        log_line("host_bdaddr: HCI Reset after CSR warm reset failed");
        return 0;
    }
    return 1;
}

/* ---- public ------------------------------------------------------------ */

void hb_host_bdaddr_learn_console(hci_t hci)
{
    unsigned char cur[6], saved[6];
    char astr[18];

    if (!read_bd_addr(hci, cur)) {
        log_line("host_bdaddr: console READ_BD_ADDR failed");
        return;
    }
    hci_addr_str(cur, astr);
    if (hb_host_bdaddr_load(saved) && same_addr(saved, cur)) {
        log_line("host_bdaddr: console %s (already saved)", astr);
        return;
    }
    if (hb_host_bdaddr_save(cur))
        log_line("host_bdaddr: console %s is now the shared host address", astr);
}

int hb_host_bdaddr_sync_usb(hci_t hci, int is_csr, int is_realtek)
{
    unsigned char want[6], cur[6];
    char wastr[18], castr[18];
    int have_want, wrote = 0;

    have_want = hb_host_bdaddr_load(want);
    if (!read_bd_addr(hci, cur)) {
        log_line("host_bdaddr: READ_BD_ADDR failed before sync");
        return 1;
    }
    hci_addr_str(cur, castr);

    if (!have_want) {
        /* First USB stick (or no console yet): this address becomes shared. */
        hb_host_bdaddr_save(cur);
        log_line("host_bdaddr: USB %s remembered as shared host address", castr);
        return 1;
    }

    hci_addr_str(want, wastr);
    if (same_addr(cur, want)) {
        log_line("host_bdaddr: USB already %s (matches shared)", castr);
        return 1;
    }

    log_line("host_bdaddr: USB is %s, cloning shared %s", castr, wastr);

    if (is_csr) {
        wrote = csr_apply(hci, want);
    } else if (is_realtek) {
        /* 0xFC61 is Read_REG16 on Realtek — not a BD_ADDR write. Address is
         * set via vendor config offset 0x0030 appended to the 0xFC20 patch. */
        hb_rtl_bdaddr_override(want);
        wrote = hb_rtl_repatch_bdaddr(hci, want);
        hb_rtl_bdaddr_override(NULL);
    } else {
        log_line("host_bdaddr: no write path for this chip (leaving %s)", castr);
        return 1;
    }

    if (!read_bd_addr(hci, cur)) {
        log_line("host_bdaddr: READ_BD_ADDR failed after write");
        return 1;
    }
    hci_addr_str(cur, castr);
    if (same_addr(cur, want))
        log_line("host_bdaddr: verified USB BD_ADDR %s", castr);
    else
        log_line("host_bdaddr: after write USB still %s (wanted %s)%s",
                 castr, wastr, wrote ? "" : " — write failed");
    return 1;
}
