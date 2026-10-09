/* Developed by X-F1REBALL-X. Settings backup / restore (host test, temp dirs). */
#include "backup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

static void put(const char *dir, const char *rel, const char *text)
{
    char p[300];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    f = fopen(p, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static int same(const char *a, const char *b, const char *rel)
{
    char pa[300], pb[300], ta[4096] = "", tb[4096] = "";
    FILE *f;
    size_t na = 0, nb = 0;
    snprintf(pa, sizeof pa, "%s/%s", a, rel);
    snprintf(pb, sizeof pb, "%s/%s", b, rel);
    if ((f = fopen(pa, "r"))) { na = fread(ta, 1, sizeof ta - 1, f); fclose(f); }
    if ((f = fopen(pb, "r"))) { nb = fread(tb, 1, sizeof tb - 1, f); fclose(f); }
    return na && na == nb && !memcmp(ta, tb, na);
}

int main(int argc, char **argv)
{
    char base[200], sd[260], dd[260], rd[260], nm[64], when[32], p[300];
    char names[HB_BACKUP_KEEP][HB_BACKUP_NAME], pn[8][32];
    static char json[HB_BACKUP_MAX], idx[4096], idx2[4096];
    hb_bfile f[HB_BACKUP_FILES];
    static char buf[HB_BACKUP_MAX];
    int n, i;

    snprintf(base, sizeof base, "%s", argc > 1 ? argv[1] : "/tmp/hb_backup_test");
    snprintf(p, sizeof p, "rm -rf '%s'", base);
    if (system(p) != 0) return 1;
    mkdir(base, 0755);
    snprintf(sd, sizeof sd, "%s/state", base);
    snprintf(dd, sizeof dd, "%s/usb0/hearbridge-backup", base);
    snprintf(rd, sizeof rd, "%s/restored", base);
    mkdir(sd, 0755);
    snprintf(p, sizeof p, "%s/prefs", sd);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/usb0", base);
    mkdir(p, 0755);

    hb_backup_stamp(1791598380L, nm, sizeof nm);           /* 2026-10-10 02:13:00 UTC */
    CHECK(!strcmp(nm, "hearbridge-20261010-021300.json"), "stamp: file name from the clock");
    CHECK(hb_backup_name_ok(nm) && hb_backup_name_ok(HB_BACKUP_LATEST), "names: ours accepted");
    CHECK(!hb_backup_name_ok("../x.json") && !hb_backup_name_ok("hearbridge-2026101-021300.json") &&
          !hb_backup_name_ok(NULL), "names: anything else refused");
    hb_backup_when(nm, when, sizeof when);
    CHECK(!strcmp(when, "2026-10-10 02:13"), "when: shown date");
    CHECK(hb_backup_file_ok("paired.ini") && hb_backup_file_ok("prefs/AC800A123456.txt"), "files: settings files ok");
    CHECK(!hb_backup_file_ok("prefs/../../x.txt") && !hb_backup_file_ok("/etc/passwd") &&
          !hb_backup_file_ok("prefs/ac800a123456.txt") && !hb_backup_file_ok("lock"), "files: nothing else is written");
    n = hb_backup_prefs_names("name=A\naddr=ac:80:0a:12:34:56\naddr=AC:80:0A:12:34:56\naddr=zz\n", pn, 8);
    CHECK(n == 1 && !strcmp(pn[0], "prefs/AC800A123456.txt"), "prefs names from saved addresses, no duplicates");

    put(sd, "paired.ini", "[0]\naddr=AC:80:0A:12:34:56\nname=Sony \"WH\"\n");
    put(sd, "headset.ini", "addr=AC:80:0A:12:34:56\nkey=00112233445566778899aabbccddeeff\n");
    put(sd, "gain", "2.50\n");
    put(sd, "games.txt", "PPSA01325\tASTRO BOT\teq=on\n");
    put(sd, "prefs/AC800A123456.txt", "codec=auto\nlatency_ms=90\nnight=on\n");
    put(sd, "lock", "not backed up\n");

    n = hb_backup_make(sd, "1.2.0", 1791598380L, json, (int)sizeof json);
    CHECK(n > 0 && strstr(json, "\"hearbridge_backup\"") && strstr(json, "prefs/AC800A123456.txt") &&
          !strstr(json, "not backed up"), "make: settings in, other files out");
    n = hb_backup_parse(json, f, HB_BACKUP_FILES, buf, (int)sizeof buf);
    CHECK(n == 5, "parse: all five files back");
    CHECK(hb_backup_parse("{\"x\":1}", f, HB_BACKUP_FILES, buf, (int)sizeof buf) < 0, "parse: not a backup");
    CHECK(hb_backup_parse("{\"hearbridge_backup\":1,\"files\":{\"../../etc/x\":\"evil\"}}", f, HB_BACKUP_FILES,
                          buf, (int)sizeof buf) <= 0, "parse: a path outside the settings is refused");

    CHECK(hb_backup_find_usb(base, p, sizeof p) == 0, "usb: a plain folder is not a drive");

    CHECK(hb_backup_save(sd, dd, "1.2.0", 1791598380L, nm, sizeof nm), "save to the drive");
    CHECK(hb_backup_save(sd, dd, "1.2.0", 1791598440L, nm, sizeof nm), "save again a minute later");
    n = hb_backup_list(dd, names, HB_BACKUP_KEEP);
    CHECK(n == 2 && !strcmp(names[0], "hearbridge-20261010-021400.json"), "list: newest first");

    CHECK(hb_backup_restore(rd, dd, names[1]) == 5, "restore into an empty folder");
    CHECK(same(sd, rd, "paired.ini") && same(sd, rd, "headset.ini") && same(sd, rd, "gain") &&
          same(sd, rd, "games.txt") && same(sd, rd, "prefs/AC800A123456.txt"), "restored files match byte for byte");
    CHECK(hb_backup_restore(rd, dd, "../state/paired.ini") < 0, "restore: only our backup names");

    idx[0] = 0;
    for (i = 0; i < HB_BACKUP_KEEP + 5; i++) {
        hb_backup_stamp(1791598380L + i * 60, nm, sizeof nm);
        n = hb_backup_index_add(idx, nm, idx2, (int)sizeof idx2);
        if (n > 0) memcpy(idx, idx2, (size_t)n + 1);
    }
    CHECK(hb_backup_index_list(idx, names, HB_BACKUP_KEEP) == HB_BACKUP_KEEP, "index keeps the last 20");

    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
