/* Developed by X-F1REBALL-X. */
#include "backup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- names ---------------------------------------------------------- */

static void civil(long days, int *y, int *m, int *d)
{
    /* days since 1970-01-01 -> y/m/d (H. Hinnant's civil_from_days) */
    long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

void hb_backup_stamp(long t, char *out, int max)
{
    int y, m, d;
    long s = t < 0 ? 0 : t;
    civil(s / 86400, &y, &m, &d);
    s %= 86400;
    snprintf(out, (size_t)max, "hearbridge-%04d%02d%02d-%02ld%02ld%02ld.json",
             y, m, d, s / 3600, s / 60 % 60, s % 60);
}

static int digits(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

int hb_backup_name_ok(const char *n)
{
    if (!n) return 0;
    if (!strcmp(n, HB_BACKUP_LATEST)) return 1;
    return strlen(n) == 31 && !strncmp(n, "hearbridge-", 11) && digits(n + 11, 8) &&
           n[19] == '-' && digits(n + 20, 6) && !strcmp(n + 26, ".json");
}

void hb_backup_when(const char *n, char *out, int max)
{
    if (!hb_backup_name_ok(n) || !strcmp(n, HB_BACKUP_LATEST)) { if (max) out[0] = 0; return; }
    snprintf(out, (size_t)max, "%.4s-%.2s-%.2s %.2s:%.2s", n + 11, n + 15, n + 17, n + 20, n + 22);
}

static int hexu(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); }

int hb_backup_file_ok(const char *r)
{
    int i;
    if (!r) return 0;
    if (!strcmp(r, "paired.ini") || !strcmp(r, "headset.ini") || !strcmp(r, "gain") ||
        !strcmp(r, "games.txt")) return 1;
    if (strlen(r) != 22 || strncmp(r, "prefs/", 6) || strcmp(r + 18, ".txt")) return 0;
    for (i = 6; i < 18; i++) if (!hexu(r[i])) return 0;
    return 1;
}

int hb_backup_prefs_names(const char *t, char (*names)[32], int max)
{
    int n = 0;
    while (t && *t && n < max) {
        const char *e = strchr(t, '\n');
        if (!strncmp(t, "addr=", 5)) {
            char a[13];
            int k = 0, j;
            const char *p = t + 5;
            for (j = 0; j < 17 && p[j] && p[j] != '\n' && p[j] != '\r'; j++) {
                char c = p[j];
                if (c == ':') continue;
                if (c >= 'a' && c <= 'f') c = (char)(c - 32);
                if (!hexu(c) || k >= 12) { k = -1; break; }
                a[k++] = c;
            }
            if (k == 12) {
                int dup = 0, i;
                a[12] = 0;
                snprintf(names[n], 32, "prefs/%s.txt", a);
                for (i = 0; i < n; i++) if (!strcmp(names[i], names[n])) dup = 1;
                if (!dup) n++;
            }
        }
        t = e ? e + 1 : NULL;
    }
    return n;
}

/* ---- JSON ----------------------------------------------------------- */

static int put_str(char *o, int n, int max, const char *s, int len)
{
    int i;
    if (n < 0 || n + 2 >= max) return -1;
    o[n++] = '"';
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (n + 8 >= max) return -1;
        if (c == '"' || c == '\\') { o[n++] = '\\'; o[n++] = (char)c; }
        else if (c == '\n') { o[n++] = '\\'; o[n++] = 'n'; }
        else if (c == '\r') { o[n++] = '\\'; o[n++] = 'r'; }
        else if (c == '\t') { o[n++] = '\\'; o[n++] = 't'; }
        else if (c < 0x20) n += snprintf(o + n, (size_t)(max - n), "\\u%04x", c);
        else o[n++] = (char)c;
    }
    o[n++] = '"';
    o[n] = 0;
    return n;
}

int hb_backup_build(const hb_bfile *f, int nf, const char *ver, long t, char *o, int max)
{
    char st[40], when[24];
    int i, n;
    hb_backup_stamp(t, st, sizeof st);
    hb_backup_when(st, when, sizeof when);
    n = snprintf(o, (size_t)max, "{\"hearbridge_backup\":1,\"version\":\"%s\",\"created\":\"%s UTC\",\"files\":{",
                 ver ? ver : "", when);
    if (n < 0 || n >= max) return -1;
    for (i = 0; i < nf; i++) {
        if (!hb_backup_file_ok(f[i].name) || !f[i].text) continue;
        if (n + 1 >= max) return -1;
        if (o[n - 1] != '{') o[n++] = ',';
        n = put_str(o, n, max, f[i].name, (int)strlen(f[i].name));
        if (n < 0 || n + 1 >= max) return -1;
        o[n++] = ':';
        n = put_str(o, n, max, f[i].text, f[i].len);
        if (n < 0) return -1;
    }
    if (n + 3 >= max) return -1;
    o[n++] = '}'; o[n++] = '}'; o[n++] = '\n'; o[n] = 0;
    return n;
}

static const char *ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static int hexv(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
           c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* JSON string at p (after the quote) into out; returns past the closing
 * quote or NULL. *len gets the length. */
static const char *get_str(const char *p, char *out, int max, int *len)
{
    int n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            c = *p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                int k, v = 0;
                for (k = 0; k < 4; k++) { int h = hexv(p[k]); if (h < 0) return NULL; v = v * 16 + h; }
                p += 4;
                c = v < 0x80 ? (char)v : '?';
                break;
            }
            case '"': case '\\': case '/': break;
            default: return NULL;
            }
        }
        if (n + 1 >= max) return NULL;
        out[n++] = c;
    }
    if (*p != '"') return NULL;
    out[n] = 0;
    if (len) *len = n;
    return p + 1;
}

int hb_backup_parse(const char *js, hb_bfile *f, int max, char *buf, int bufmax)
{
    const char *p;
    int nf = 0, used = 0;
    if (!js || !strstr(js, "\"hearbridge_backup\"")) return -1;
    p = strstr(js, "\"files\"");
    if (!p) return -1;
    p = ws(p + 7);
    if (*p++ != ':') return -1;
    p = ws(p);
    if (*p++ != '{') return -1;
    for (;;) {
        char key[64];
        int len = 0;
        p = ws(p);
        if (*p == '}') break;
        if (*p++ != '"') return -1;
        p = get_str(p, key, sizeof key, NULL);
        if (!p) return -1;
        p = ws(p);
        if (*p++ != ':') return -1;
        p = ws(p);
        if (*p++ != '"') return -1;
        if (used >= bufmax) return -1;
        p = get_str(p, buf + used, bufmax - used, &len);
        if (!p) return -1;
        if (hb_backup_file_ok(key) && len <= HB_BACKUP_FILE_MAX && nf < max) {
            snprintf(f[nf].name, sizeof f[nf].name, "%.31s", key);
            f[nf].text = buf + used;
            f[nf].len = len;
            nf++;
            used += len + 1;
        }
        p = ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') break;
        return -1;
    }
    return nf;
}

/* ---- index ---------------------------------------------------------- */

int hb_backup_index_list(const char *t, char (*names)[HB_BACKUP_NAME], int max)
{
    int n = 0;
    while (t && *t && n < max) {
        const char *e = strchr(t, '\n');
        size_t l = e ? (size_t)(e - t) : strlen(t);
        char nm[HB_BACKUP_NAME];
        int i, dup = 0;
        if (l >= sizeof nm) l = sizeof nm - 1;
        memcpy(nm, t, l);
        nm[l] = 0;
        while (l && (nm[l - 1] == '\r' || nm[l - 1] == ' ')) nm[--l] = 0;
        for (i = 0; i < n; i++) if (!strcmp(names[i], nm)) dup = 1;
        if (!dup && hb_backup_name_ok(nm) && strcmp(nm, HB_BACKUP_LATEST))
            snprintf(names[n++], HB_BACKUP_NAME, "%s", nm);
        t = e ? e + 1 : NULL;
    }
    return n;
}

int hb_backup_index_add(const char *old, const char *name, char *out, int max)
{
    char names[HB_BACKUP_KEEP + 1][HB_BACKUP_NAME];
    int n, i, o;
    int kept = 1;
    n = hb_backup_index_list(old, names, HB_BACKUP_KEEP + 1);
    o = snprintf(out, (size_t)max, "%s\n", name);
    for (i = 0; i < n && kept < HB_BACKUP_KEEP && o > 0 && o < max; i++) {
        if (!strcmp(names[i], name)) continue;
        o += snprintf(out + o, (size_t)(max - o), "%s\n", names[i]);
        kept++;
    }
    return o > 0 && o < max ? o : -1;
}

/* ---- files ---------------------------------------------------------- */

static int read_all(const char *path, char *buf, int max)
{
    FILE *f = fopen(path, "r");
    int n;
    if (!f) return -1;
    n = (int)fread(buf, 1, (size_t)(max - 1), f);
    fclose(f);
    if (n < 0) n = 0;
    buf[n] = 0;
    return n;
}

static int write_atomic(const char *path, const char *d, int n)
{
    char tmp[200];
    FILE *f;
    int ok;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "w");
    if (!f) return 0;
    ok = (int)fwrite(d, 1, (size_t)n, f) == n;
    if (fclose(f) != 0) ok = 0;
    if (ok && rename(tmp, path) == 0) return 1;
    remove(tmp);
    return 0;
}

int hb_backup_find_usb(const char *root, char *dir, int max)
{
    struct stat rs, us;
    int i;
    if (stat(root, &rs) != 0) return 0;
    for (i = 0; i < 8; i++) {
        char p[96];
        snprintf(p, sizeof p, "%s/usb%d", root, i);
        if (stat(p, &us) != 0 || !S_ISDIR(us.st_mode)) continue;
        if (us.st_dev == rs.st_dev) continue;      /* empty mount point, no drive */
        snprintf(dir, (size_t)max, "%s/%s", p, HB_BACKUP_SUB);
        return 1;
    }
    return 0;
}

int hb_backup_make(const char *sd, const char *ver, long t, char *json, int jmax)
{
    static const char *const fixed[] = { "paired.ini", "headset.ini", "gain", "games.txt" };
    static char texts[HB_BACKUP_FILES][HB_BACKUP_FILE_MAX + 1];
    char pn[8][32], path[200];
    hb_bfile f[HB_BACKUP_FILES];
    int nf = 0, i, np, n;

    for (i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", sd, fixed[i]);
        n = read_all(path, texts[nf], HB_BACKUP_FILE_MAX + 1);
        if (n < 0) continue;
        snprintf(f[nf].name, sizeof f[nf].name, "%s", fixed[i]);
        f[nf].text = texts[nf];
        f[nf].len = n;
        nf++;
    }
    np = 0;
    for (i = 0; i < nf; i++)
        if (!strcmp(f[i].name, "paired.ini") || !strcmp(f[i].name, "headset.ini")) {
            char more[8][32];
            int k, m = hb_backup_prefs_names(f[i].text, more, 8), j;
            for (k = 0; k < m && np < 8; k++) {
                int dup = 0;
                for (j = 0; j < np; j++) if (!strcmp(pn[j], more[k])) dup = 1;
                if (!dup) snprintf(pn[np++], 32, "%.31s", more[k]);
            }
        }
    for (i = 0; i < np && nf < HB_BACKUP_FILES; i++) {
        snprintf(path, sizeof path, "%.120s/%.31s", sd, pn[i]);
        n = read_all(path, texts[nf], HB_BACKUP_FILE_MAX + 1);
        if (n < 0) continue;
        snprintf(f[nf].name, sizeof f[nf].name, "%.31s", pn[i]);
        f[nf].text = texts[nf];
        f[nf].len = n;
        nf++;
    }
    return hb_backup_build(f, nf, ver, t, json, jmax);
}

int hb_backup_save(const char *sd, const char *dd, const char *ver, long t, char *name, int nmax)
{
    static char json[HB_BACKUP_MAX], idx[2048], nidx[2048];
    char path[200], nm[48];
    int n = hb_backup_make(sd, ver, t, json, (int)sizeof json);
    if (n <= 0) return 0;
    mkdir(dd, 0755);
    hb_backup_stamp(t, nm, sizeof nm);
    snprintf(path, sizeof path, "%s/%s", dd, nm);
    if (!write_atomic(path, json, n)) return 0;
    snprintf(path, sizeof path, "%s/%s", dd, HB_BACKUP_LATEST);
    (void)write_atomic(path, json, n);
    snprintf(path, sizeof path, "%s/index.txt", dd);
    if (read_all(path, idx, (int)sizeof idx) < 0) idx[0] = 0;
    n = hb_backup_index_add(idx, nm, nidx, (int)sizeof nidx);
    if (n > 0) (void)write_atomic(path, nidx, n);
    if (name && nmax > 0) snprintf(name, (size_t)nmax, "%s", nm);
    return 1;
}

int hb_backup_list(const char *dd, char (*names)[HB_BACKUP_NAME], int max)
{
    static char idx[2048];
    char all[HB_BACKUP_KEEP][HB_BACKUP_NAME], path[200];
    struct stat st;
    int n, i, k = 0;
    snprintf(path, sizeof path, "%s/index.txt", dd);
    if (read_all(path, idx, (int)sizeof idx) < 0) idx[0] = 0;
    n = hb_backup_index_list(idx, all, HB_BACKUP_KEEP);
    for (i = 0; i < n && k < max; i++) {
        snprintf(path, sizeof path, "%.120s/%.39s", dd, all[i]);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) snprintf(names[k++], HB_BACKUP_NAME, "%.39s", all[i]);
    }
    snprintf(path, sizeof path, "%s/%s", dd, HB_BACKUP_LATEST);
    if (!k && k < max && stat(path, &st) == 0 && S_ISREG(st.st_mode))
        snprintf(names[k++], HB_BACKUP_NAME, "%s", HB_BACKUP_LATEST);
    return k;
}

int hb_backup_restore(const char *sd, const char *dd, const char *name)
{
    static char json[HB_BACKUP_MAX + 1];
    char path[200];
    if (!hb_backup_name_ok(name)) return -1;
    snprintf(path, sizeof path, "%s/%s", dd, name);
    if (read_all(path, json, (int)sizeof json) <= 0) return -1;
    return hb_backup_restore_text(sd, json);
}

int hb_backup_restore_text(const char *sd, const char *json)
{
    static char buf[HB_BACKUP_MAX + 1];
    hb_bfile f[HB_BACKUP_FILES];
    char path[200];
    int n, i, w = 0;
    n = hb_backup_parse(json, f, HB_BACKUP_FILES, buf, (int)sizeof buf);
    if (n <= 0) return -1;
    mkdir(sd, 0755);
    snprintf(path, sizeof path, "%s/prefs", sd);
    mkdir(path, 0755);
    for (i = 0; i < n; i++) {
        snprintf(path, sizeof path, "%.120s/%.31s", sd, f[i].name);
        if (write_atomic(path, f[i].text, f[i].len)) w++;
    }
    return w;
}
