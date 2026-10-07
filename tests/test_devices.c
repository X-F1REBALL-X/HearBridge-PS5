/* Developed by X-F1REBALL-X. Host tests: UTF-8 names, device classes, saved list. */
#define _DEFAULT_SOURCE
#include "utf8.h"
#include "devclass.h"
#include "paired.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

static headset_ini mk(unsigned char last, const char *name, unsigned cod)
{
    headset_ini h;
    int i;
    memset(&h, 0, sizeof h);
    for (i = 0; i < 6; i++) h.addr[i] = (unsigned char)(0x10 + i);
    h.addr[0] = last;
    snprintf(h.name, sizeof h.name, "%s", name);
    h.cod = cod;
    h.key_type = 4;
    for (i = 0; i < 16; i++) h.link_key[i] = (unsigned char)(i * 17 + last);
    h.ok = h.have_addr = 1;
    return h;
}

int main(void)
{
    char b[64], js[4096], path[] = "/tmp/hb_paired_XXXXXX";
    headset_ini L[PAIRED_MAX], R[PAIRED_MAX], h;
    int n = 0, m, fd;

    /* UTF-8 */
    strcpy(b, "Caf\xc3\xa9 \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d"); hb_utf8_clean(b);
    CHECK(!strcmp(b, "Caf\xc3\xa9 \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d"), "utf8: valid text kept");
    strcpy(b, "TV\xff\xfeX"); hb_utf8_clean(b);
    CHECK(!strcmp(b, "TV??X"), "utf8: invalid bytes -> ?");
    strcpy(b, "A\xe2\x82"); hb_utf8_clean(b);
    CHECK(!strcmp(b, "A"), "utf8: cut-off tail dropped");
    strcpy(b, "\xc0\xaf\xed\xa0\x80z"); hb_utf8_clean(b);
    CHECK(!strcmp(b, "?????z"), "utf8: overlong / surrogate rejected");
    strcpy(b, "a\tb\x01  "); hb_utf8_clean(b);
    CHECK(!strcmp(b, "a b"), "utf8: controls -> space, trailing trimmed");
    {
        const char src[] = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";   /* 3 chars, 9 bytes */
        hb_utf8_copy(b, 8, src, sizeof src - 1);
        CHECK(!strcmp(b, "\xe6\x97\xa5\xe6\x9c\xac"), "utf8: copy never splits a character");
        hb_utf8_copy(b, sizeof b, "ab\0cd", 5);
        CHECK(!strcmp(b, "ab"), "utf8: copy stops at NUL");
    }

    /* Class of Device */
    CHECK(!strcmp(hb_dev_kind(0x240404, "WH-1000XM5"), "headset"), "cod 240404 headset");
    CHECK(!strcmp(hb_dev_kind(0x240418, "WF-1000XM6"), "headphones"), "cod 240418 headphones");
    CHECK(!strcmp(hb_dev_kind(0x240414, "Speaker"), "speaker"), "cod 240414 speaker");
    CHECK(!strcmp(hb_dev_kind(0x24041c, "Boom"), "portable"), "cod 24041c portable");
    CHECK(!strcmp(hb_dev_kind(0x280424, "[TV] Living room"), "tv"), "cod 280424 (set-top) -> tv");
    CHECK(hb_dev_rank(0x280424, "[TV] Living room") == -1, "cod 280424 (set-top / TV) hidden");
    CHECK(hb_dev_rank(0x240414, "JBL Flip") == 1 && hb_dev_rank(0x240428, "HiFi") == 1, "loudspeaker / hi-fi listed");
    CHECK(hb_dev_rank(0x240420, "Car kit") == -1, "car audio hidden");
    CHECK(hb_dev_rank(0x24043c, "Monitor") == -1, "video display hidden");
    CHECK(hb_dev_rank(0x240418, "x") == 0 && hb_dev_rank(0x240404, "x") == 0, "headphones/headset first");
    CHECK(hb_dev_rank(0x24041c, "x") == 1 && hb_dev_rank(0x240408, "x") == 2, "portable, then hands-free");
    CHECK(hb_dev_rank(0x5a020c, "Phone") == -1, "phone hidden");
    CHECK(hb_dev_rank(0x280424, "") == -1 && hb_dev_rank(0x280424, "TV - \xd7\x97\xd7\x93\xd7\xa8 \xd7\xa9\xd7\x99\xd7\xa0\xd7\x94") == -1 &&
          hb_dev_rank(0x280424, "\xd7\x97\xd7\x93\xd7\xa8 \xd7\x99\xd7\x9c\xd7\x93\xd7\x99\xd7\x9d") == -1, "console TVs (CoD 280424) hidden");
    CHECK(hb_dev_rank(0x240414, "") == -1 && hb_dev_rank(0x000000, "") == -1 && hb_dev_rank(0x240404, "") == 0,
          "unnamed: only a headphone / headset class is listed");
    CHECK(hb_dev_rank(0x5a020c, "My Buds phone") == -1 && hb_dev_rank(0x10010c, "Headquarters laptop") == -1,
          "phones and computers hidden even with a headphone-like name");
    CHECK(hb_dev_rank(0x240420, "Car Earphone kit") == -1 && hb_dev_rank(0x240424, "Head TV box") == -1 &&
          hb_dev_rank(0x24043c, "Ear TV") == -1, "car kits, set-top boxes and TVs hidden even with a hint name");
    CHECK(hb_dev_rank(0x200100, "PC with audio service") == -1, "audio service bit alone is not enough");
    CHECK(hb_dev_rank(0x000000, "OnePlus Buds Ace 2") == 3 && hb_dev_rank(0x001f00, "AirPods Pro") == 3 &&
          hb_dev_rank(0x1f00, "WF-1000XM6") == 3 && hb_dev_rank(0x0, "Galaxy Earbuds") == 3 &&
          hb_dev_rank(0x0, "Headset X") == 3, "odd CoD but headphone-like name: listed");
    CHECK(!strcmp(hb_dev_kind(0x000000, "OnePlus Buds Ace 2"), "headphones"), "name hint gets the headphones icon");
    /* Saved list */
    h = mk(0xA1, "WF-1000XM6", 0x240418); paired_put(L, &n, PAIRED_MAX, &h);
    h = mk(0xB2, "Speaker \xc3\xa9", 0x240414); paired_put(L, &n, PAIRED_MAX, &h);
    CHECK(n == 2 && L[0].addr[0] == 0xB2, "put: newest first");
    h = mk(0xA1, "WF-1000XM6", 0x240418); paired_put(L, &n, PAIRED_MAX, &h);
    CHECK(n == 2 && L[0].addr[0] == 0xA1 && L[1].addr[0] == 0xB2, "put: same address moves to front");
    fd = mkstemp(path); close(fd);
    CHECK(paired_save(path, L, n), "save");
    m = paired_load(path, R, PAIRED_MAX);
    CHECK(m == 2 && !memcmp(R[1].link_key, L[1].link_key, 16) && R[1].key_type == 4 &&
          !strcmp(R[1].name, "Speaker \xc3\xa9") && R[0].cod == 0x240418, "load round-trip");
    {
        char a[18];
        paired_addr_text(R[0].addr, a);
        CHECK(!strcmp(a, "15:14:13:12:11:A1"), "address text in display order");
    }
    CHECK(paired_json(L, n, L[1].addr, js, sizeof js) > 0 &&
          strstr(js, "\"addr\":\"15:14:13:12:11:A1\",\"name\":\"WF-1000XM6\",\"kind\":\"headphones\",\"current\":0") &&
          strstr(js, "\"kind\":\"speaker\",\"current\":1") && !strstr(js, "key"), "json: no keys, current flag");
    CHECK(paired_drop(L, &n, L[0].addr) && n == 1 && L[0].addr[0] == 0xB2, "drop");
    {
        unsigned char none[6] = { 9, 9, 9, 9, 9, 9 };
        CHECK(!paired_drop(L, &n, none) && paired_find(L, n, none) == -1, "drop unknown is a no-op");
    }
    for (m = 0; m < 12; m++) { h = mk((unsigned char)m, "x", 0x240404); paired_put(L, &n, PAIRED_MAX, &h); }
    CHECK(n == PAIRED_MAX && L[0].addr[0] == 11, "list capped at PAIRED_MAX");
    {
        FILE *f = fopen(path, "w");
        fputs("addr=11:22:33:44:55:66\n[device]\naddr=zz\nkey=00\n[device]\naddr=01:02:03:04:05:06\n"
              "name=ok\nkey=000102030405060708090a0b0c0d0e0f\n", f);
        fclose(f);
        m = paired_load(path, R, PAIRED_MAX);
        CHECK(m == 1 && !strcmp(R[0].name, "ok"), "load skips broken entries");
    }
    unlink(path);
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
