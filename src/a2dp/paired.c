/* Developed by X-F1REBALL-X. */
#include "paired.h"
#include "devclass.h"
#include "utf8.h"

#include <stdio.h>
#include <string.h>

void paired_addr_text(const unsigned char a[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
}

static int parse_addr(const char *s, unsigned char out[6])
{
    unsigned b[6];
    int i;
    if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return 0;
    for (i = 0; i < 6; i++) out[i] = (unsigned char)b[5 - i];
    return 1;
}

static int hexkey(const char *s, unsigned char k[16])
{
    int i;
    for (i = 0; i < 16; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return 0;
        k[i] = (unsigned char)v;
    }
    return 1;
}

static int keep(headset_ini *list, int n, int max, headset_ini *cur)
{
    if (n < max && cur->have_addr && cur->ok) list[n++] = *cur;
    memset(cur, 0, sizeof *cur);
    return n;
}

int paired_load(const char *path, headset_ini *list, int max)
{
    FILE *f = fopen(path, "r");
    char line[256];
    headset_ini cur;
    int n = 0, open = 0;

    if (!f) return 0;
    memset(&cur, 0, sizeof cur);
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "[device]")) {
            if (open) n = keep(list, n, max, &cur);
            open = 1;
        } else if (!open) {
            continue;
        } else if (!strncmp(line, "addr=", 5)) {
            cur.have_addr = parse_addr(line + 5, cur.addr);
        } else if (!strncmp(line, "name=", 5)) {
            hb_utf8_copy(cur.name, sizeof cur.name, line + 5, strlen(line + 5));
        } else if (!strncmp(line, "cod=", 4)) {
            unsigned c = 0; sscanf(line + 4, "%x", &c); cur.cod = c;
        } else if (!strncmp(line, "key_type=", 9)) {
            unsigned t = 0; sscanf(line + 9, "%u", &t); cur.key_type = (unsigned char)t;
        } else if (!strncmp(line, "key=", 4)) {
            cur.ok = strlen(line + 4) >= 32 && hexkey(line + 4, cur.link_key);
        }
    }
    if (open) n = keep(list, n, max, &cur);
    fclose(f);
    return n;
}

int paired_save(const char *path, const headset_ini *list, int n)
{
    char tmp[256], a[18];
    FILE *f;
    int i, j, ok = 1;

    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!(f = fopen(tmp, "w"))) return 0;
    fprintf(f, "# HearBridge paired headsets, newest first\n");
    for (i = 0; i < n; i++) {
        paired_addr_text(list[i].addr, a);
        fprintf(f, "[device]\naddr=%s\nname=%s\ncod=%06x\nkey_type=%u\nkey=", a,
                list[i].name, (unsigned)list[i].cod, (unsigned)list[i].key_type);
        for (j = 0; j < 16; j++) fprintf(f, "%02x", list[i].link_key[j]);
        fprintf(f, "\n");
    }
    if (fclose(f)) ok = 0;
    if (!ok || rename(tmp, path)) { remove(tmp); return 0; }
    return 1;
}

int paired_find(const headset_ini *list, int n, const unsigned char addr[6])
{
    int i;
    for (i = 0; i < n; i++) if (!memcmp(list[i].addr, addr, 6)) return i;
    return -1;
}

int paired_drop(headset_ini *list, int *n, const unsigned char addr[6])
{
    int i = paired_find(list, *n, addr);
    if (i < 0) return 0;
    memmove(list + i, list + i + 1, (size_t)(*n - i - 1) * sizeof *list);
    (*n)--;
    return 1;
}

void paired_put(headset_ini *list, int *n, int max, const headset_ini *d)
{
    headset_ini copy = *d;
    paired_drop(list, n, copy.addr);
    if (*n >= max) *n = max - 1;
    memmove(list + 1, list, (size_t)*n * sizeof *list);
    list[0] = copy;
    (*n)++;
}

static size_t put_str(char *o, size_t cap, size_t len, const char *s)
{
    for (; *s && len + 7 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { o[len++] = '\\'; o[len++] = (char)c; }
        else if (c < 0x20) len += (size_t)snprintf(o + len, cap - len, "\\u%04x", c);
        else o[len++] = (char)c;
    }
    return len;
}

int paired_json(const headset_ini *list, int n, const unsigned char *current,
                char *out, size_t cap)
{
    size_t len = 0;
    int i;
    char a[18];
    const char *kind;

    if (cap < 32) return -1;
    len = (size_t)snprintf(out, cap, "{\"devices\":[");
    for (i = 0; i < n; i++) {
        if (len + 200 + 2 * sizeof list[i].name >= cap) return -1;
        paired_addr_text(list[i].addr, a);
        kind = hb_dev_kind(list[i].cod, list[i].name);
        len += (size_t)snprintf(out + len, cap - len, "%s{\"addr\":\"%s\",\"name\":\"",
                                i ? "," : "", a);
        len = put_str(out, cap, len, list[i].name);
        len += (size_t)snprintf(out + len, cap - len, "\",\"kind\":\"%s\",\"current\":%d}",
                                kind ? kind : "audio",
                                current && !memcmp(current, list[i].addr, 6));
    }
    if (len + 3 >= cap) return -1;
    len += (size_t)snprintf(out + len, cap - len, "]}");
    return (int)len;
}
