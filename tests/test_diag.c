/* Developed by X-F1REBALL-X. Host tests for the fw13.60 build additions:
 * diagnostics report, USB descriptor summary, tile re-register decision and
 * report line, lock error classification. */
#include "diag.h"
#include "lock.h"
#include "tile.h"
#include "usb_hci_desc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

static void test_diag(void)
{
    char dir[] = "/tmp/hb_diag_XXXXXX", path[300], buf[4096], small[12];
    FILE *f;
    int n, i;

    CHECK(mkdtemp(dir) != NULL, "temp dir");
    snprintf(path, sizeof path, "%s/diag.txt", dir);
    diag_init(path);
    CHECK(diag_text(buf, sizeof buf) == 0 && buf[0] == 0, "empty report");
    diag_set("firmware", "%s (kernel %#x)", "13.60", 0x13600000u);
    diag_set("tile", "first");
    diag_set("model", "CFI-1016A");
    diag_set("tile", "second\nline");
    n = diag_text(buf, sizeof buf);
    CHECK(n > 0 && !strcmp(buf, "firmware: 13.60 (kernel 0x13600000)\ntile: second line\nmodel: CFI-1016A\n"),
          "keys keep their order, values replace, newlines flattened");
    n = diag_text(small, sizeof small);
    CHECK(n == (int)sizeof small - 1 && small[sizeof small - 1] == 0, "truncated render stays terminated");
    CHECK(diag_save() == 0, "save");
    f = fopen(path, "r");
    n = f ? (int)fread(buf, 1, sizeof buf - 1, f) : -1;
    if (f) fclose(f);
    if (n >= 0) buf[n] = 0;
    CHECK(n > 0 && strstr(buf, "model: CFI-1016A\n"), "diag.txt written");
    for (i = 0; i < DIAG_MAX_ENTRIES + 10; i++) {
        char k[16];
        snprintf(k, sizeof k, "k%d", i);
        diag_set(k, "%d", i);
    }
    n = diag_text(buf, sizeof buf);
    CHECK(n > 0 && n < (int)sizeof buf, "full table does not overflow");
    diag_init("/nonexistent-dir/x/diag.txt");
    diag_set("a", "b");
    CHECK(diag_save() == -1, "unwritable path fails");
    diag_init(NULL);
    CHECK(diag_save() == -1, "no path fails");
    unlink(path);
    rmdir(dir);
}

static void test_usb_describe(void)
{
    /* config(9) + iface0 E0/01/01 + 3 endpoints + iface1 alt1 E0/01/01 + 1 ep */
    static const unsigned char d[] = {
        9, 2, 0x3e, 0, 2, 1, 0, 0xe0, 50,
        9, 4, 0, 0, 3, 0xe0, 0x01, 0x01, 0,
        7, 5, 0x81, 3, 0x10, 0, 1,
        7, 5, 0x82, 2, 0x40, 0, 0,
        7, 5, 0x02, 2, 0x40, 0, 0,
        9, 4, 1, 1, 1, 0xe0, 0x01, 0x01, 0,
        7, 5, 0x83, 1, 0x31, 0, 1,
    };
    char out[256], tiny[10];
    struct usbhci_iface f[USBHCI_MAX_IFACES];

    usbhci_describe(d, (int)sizeof d, out, sizeof out);
    CHECK(!strcmp(out, "if0.0 e0/01/01 (BT HCI) ep81 int/16 ep82 bulk/64 ep02 bulk/64; "
                       "if1.1 e0/01/01 (BT HCI) ep83 iso/49"), "descriptor summary");
    CHECK(usbhci_scan(d, (int)sizeof d, f) == 1 && f[0].evt_ep == 0x81 && f[0].in_ep == 0x82 &&
          f[0].out_ep == 0x02, "scan still finds the HCI interface");
    usbhci_describe(d, 9, out, sizeof out);
    CHECK(!strcmp(out, "no interfaces"), "config without interfaces");
    CHECK(usbhci_describe(d, (int)sizeof d, tiny, sizeof tiny) == (int)sizeof tiny - 1 &&
          tiny[sizeof tiny - 1] == 0, "summary truncates safely");
    {
        unsigned char bad[] = { 9, 4, 0, 0, 3, 0xe0, 1, 1, 0, 0, 5 };   /* bLength 0 stops */
        usbhci_describe(bad, (int)sizeof bad, out, sizeof out);
        CHECK(!strcmp(out, "if0.0 e0/01/01 (BT HCI)"), "malformed tail ignored");
    }
}

static void test_tile_logic(void)
{
    tile_report r;
    char line[512];

    CHECK(tile_need_register(1, 1, 1) == 1, "changed files -> register");
    CHECK(tile_need_register(0, 0, 1) == 1, "no marker -> register");
    CHECK(tile_need_register(0, 1, 0) == 1, "marker but no appmeta -> register again");
    CHECK(tile_need_register(0, 1, 1) == 0, "marker + appmeta + unchanged -> skip");

    memset(&r, 0, sizeof r);
    r.marker = 1; r.appmeta_before = 1; r.appmeta_after = 1; r.skipped = 1;
    tile_report_line(&r, line, sizeof line);
    CHECK(strstr(line, "already registered, skipped") && strstr(line, "RESULT ok"), "report: skipped");

    memset(&r, 0, sizeof r);
    r.files = 1; r.authid_before = 0x4800000000000006ULL; r.authid_used = 0x4801000000000013ULL;
    r.titledir_found = 1; r.titledir_called = 1; r.titledir_rc = (int)0x80990015u;
    r.all_found = 1; r.all_called = 1; r.all_rc = (int)0x80990016u;
    r.result = -1; r.failed = "InstallAll"; r.code = (int)0x80990016u;
    tile_report_line(&r, line, sizeof line);
    CHECK(strstr(line, "TitleDir found -> 0x80990015") && strstr(line, "InstallAll -> 0x80990016") &&
          strstr(line, "authid 0x4800000000000006 -> 0x4801000000000013") &&
          strstr(line, "RESULT failed at InstallAll (0x80990016)"), "report: both calls failed");

    memset(&r, 0, sizeof r);
    r.files = 0; r.all_found = 1; r.all_called = 1; r.appmeta_after = 1;
    tile_report_line(&r, line, sizeof line);
    CHECK(strstr(line, "TitleDir NOT FOUND") && strstr(line, "InstallAll -> 0") &&
          strstr(line, "appmeta after yes") && strstr(line, "RESULT ok"), "report: fallback used");

    memset(&r, 0, sizeof r);
    r.files = -1; r.files_errno = EACCES; r.result = -1; r.failed = "writing"; r.code = EACCES;
    tile_report_line(&r, line, sizeof line);
    CHECK(strstr(line, "files ERROR (errno 13)") && !strstr(line, "init"), "report: write error");

    memset(&r, 0, sizeof r);
    r.init_rc = (int)0x80990001u; r.result = -1; r.failed = "installer init"; r.code = r.init_rc;
    tile_report_line(&r, line, sizeof line);
    CHECK(strstr(line, "init 0x80990001") && !strstr(line, "TitleDir"), "report: init failed");
    {
        char tiny[16];
        CHECK(tile_report_line(&r, tiny, sizeof tiny) == 15 && tiny[15] == 0, "report truncates safely");
    }
}

static void test_lock(void)
{
    char dir[] = "/tmp/hb_lock_XXXXXX", path[300];
    FILE *f;
    int err = -1;

    CHECK(mkdtemp(dir) != NULL, "lock temp dir");
    snprintf(path, sizeof path, "%s/hearbridge.lock", dir);
    CHECK(lock_take_ex(path, &err) == LOCK_OK && err == 0, "lock taken");
    lock_release();
    f = fopen(path, "w");
    if (f) { fprintf(f, "%ld 0\n", (long)getppid()); fclose(f); }
    CHECK(lock_take_ex(path, &err) == LOCK_BUSY && err == 0, "live owner -> busy");
    f = fopen(path, "w");
    if (f) { fprintf(f, "999999 0\n"); fclose(f); }
    CHECK(lock_take_ex(path, &err) == LOCK_OK, "dead owner -> taken over");
    lock_release();
    CHECK(lock_take_ex("/nonexistent-dir/hearbridge.lock", &err) == LOCK_NO_WRITE && err == ENOENT,
          "unwritable folder -> no-write with errno (not 'already running')");
    CHECK(lock_take("/nonexistent-dir/hearbridge.lock") == 0, "old API still reports failure");
    unlink(path);
    rmdir(dir);
}

int main(void)
{
    test_diag();
    test_usb_describe();
    test_tile_logic();
    test_lock();
    if (fails) printf("%d test(s) FAILED\n", fails);
    return fails != 0;
}
