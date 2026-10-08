/* Developed by X-F1REBALL-X. Host tests for the fw13.60 build additions:
 * diagnostics report and USB descriptor summary (icon registration and
 * the lock: tests/test_reinstall.c). */
#include "diag.h"
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

int main(void)
{
    test_diag();
    test_usb_describe();
    if (fails) printf("%d test(s) FAILED\n", fails);
    return fails != 0;
}
