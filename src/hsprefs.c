/* Developed by X-F1REBALL-X. */
#include "hsprefs.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *const g_codec_keys[HB_CODEC_N] = { "auto", "sbc", "hq", "xq" };

const char *hb_codec_key(int codec)
{
    return codec >= 0 && codec < HB_CODEC_N ? g_codec_keys[codec] : "auto";
}

int hb_codec_from_key(const char *s)
{
    int i;
    for (i = 0; i < HB_CODEC_N; i++)
        if (!strcmp(s, g_codec_keys[i])) return i;
    return -1;
}

void hb_prefs_default(hb_prefs *p)
{
    memset(p, 0, sizeof *p);
    p->codec = HB_CODEC_AUTO;
    p->latency_ms = HB_LAT_DEFAULT_MS;
    p->gain_pct = -1;
    p->hs_vol = -1;
}

int hb_prefs_gain(const hb_prefs *p)
{
    return p && p->gain_user && p->gain_pct >= 0 ? p->gain_pct : HB_PREFS_GAIN_DEFAULT;
}

int hb_prefs_hs_volume(const hb_prefs *p)
{
    return p && p->hs_vol >= 0 ? p->hs_vol : HB_PREFS_HS_VOL_DEFAULT;
}

void hb_prefs_new_headset(hb_prefs *p)
{
    if (p) p->latency_ms = HB_LAT_DEFAULT_MS;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Signed decimal at s; *end past it (== s when none). No strtol: the
 * payload imports only what 1.0.2 imported. */
static long parse_long(const char *s, const char **end)
{
    long v = 0;
    int neg = 0, any = 0;
    const char *p = s;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    while (*p >= '0' && *p <= '9') {
        if (v < 100000) v = v * 10 + (*p - '0');
        p++;
        any = 1;
    }
    *end = any ? p : s;
    return neg ? -v : v;
}

void hb_prefs_parse(hb_prefs *p, const char *t)
{
    char line[128];
    while (t && *t) {
        const char *e = strchr(t, '\n');
        size_t n = e ? (size_t)(e - t) : strlen(t);
        char *v;
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, t, n);
        line[n] = 0;
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        t = e ? e + 1 : NULL;
        v = strchr(line, '=');
        if (!v || line[0] == '#') continue;
        *v++ = 0;
        if (!strcmp(line, "codec")) {
            int c = hb_codec_from_key(v);
            if (c >= 0) p->codec = c;
        } else if (!strcmp(line, "auto_xq")) {
            p->auto_no_xq = !strcmp(v, "off");
        } else if (!strcmp(line, "latency_ms")) {
            const char *end;
            long ms = parse_long(v, &end);
            if (end != v) p->latency_ms = clampi((int)ms, HB_LAT_MIN_MS, HB_LAT_MAX_MS);
        } else if (!strcmp(line, "eq")) {
            p->eq_on = !strcmp(v, "on");
        } else if (!strcmp(line, "held")) {
            char *col = strchr(v, ':');
            const char *end;
            int c;
            long bp;
            if (!col) continue;
            *col++ = 0;
            c = hb_codec_from_key(v);
            bp = parse_long(col, &end);
            if (c >= HB_CODEC_SBC && end != col) {
                p->held_codec = c;
                p->held_bp = clampi((int)bp, 2, 64);
            }
        } else if (!strcmp(line, "gain")) {
            const char *end;
            long g = parse_long(v, &end);
            if (end != v) p->gain_pct = clampi((int)g, 0, 500);
        } else if (!strcmp(line, "gain_user")) {
            p->gain_user = !strcmp(v, "1");
        } else if (!strcmp(line, "hs_vol")) {
            const char *end;
            long hv = parse_long(v, &end);
            if (end != v) p->hs_vol = clampi((int)hv, 0, 127);
        } else if (!strcmp(line, "eq_db")) {
            int i;
            const char *s = v, *end;
            for (i = 0; i < HB_EQ_BANDS; i++) {
                long d = parse_long(s, &end);
                if (end == s) break;
                p->eq_db[i] = clampi((int)d, -HB_EQ_MAX_DB, HB_EQ_MAX_DB);
                s = end;
                if (*s == ',') s++;
            }
        }
    }
}

int hb_prefs_format(const hb_prefs *p, char *out, int max)
{
    int n = snprintf(out, (size_t)max,
                     "codec=%s\nauto_xq=%s\nlatency_ms=%d\neq=%s\neq_db=",
                     hb_codec_key(p->codec), p->auto_no_xq ? "off" : "on",
                     p->latency_ms, p->eq_on ? "on" : "off");
    int i;
    for (i = 0; i < HB_EQ_BANDS && n > 0 && n < max; i++)
        n += snprintf(out + n, (size_t)(max - n), "%s%d", i ? "," : "", p->eq_db[i]);
    if (n > 0 && n < max) n += snprintf(out + n, (size_t)(max - n), "\n");
    /* Only a gain the user set is kept (an old automatic gain=500 is dropped). */
    if (p->gain_user && p->gain_pct >= 0 && n > 0 && n < max)
        n += snprintf(out + n, (size_t)(max - n), "gain=%d\ngain_user=1\n", p->gain_pct);
    if (p->hs_vol >= 0 && n > 0 && n < max)
        n += snprintf(out + n, (size_t)(max - n), "hs_vol=%d\n", p->hs_vol);
    if (p->held_codec >= HB_CODEC_SBC && p->held_bp > 0 && n > 0 && n < max)
        n += snprintf(out + n, (size_t)(max - n), "held=%s:%d\n",
                      hb_codec_key(p->held_codec), p->held_bp);
    return n < max ? n : -1;
}

void hb_prefs_path(const char *dir, const unsigned char a[6], char *out, int max)
{
    snprintf(out, (size_t)max, "%s/%02X%02X%02X%02X%02X%02X.txt", dir,
             a[5], a[4], a[3], a[2], a[1], a[0]);
}

int hb_prefs_load(const char *dir, const unsigned char addr[6], hb_prefs *p)
{
    char path[160], buf[512];
    size_t n;
    FILE *f;
    hb_prefs_path(dir, addr, path, (int)sizeof path);
    f = fopen(path, "r");
    if (!f) return 0;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    hb_prefs_parse(p, buf);
    return 1;
}

int hb_prefs_save(const char *dir, const unsigned char addr[6], const hb_prefs *p)
{
    char path[160], tmp[168], buf[512];
    int n = hb_prefs_format(p, buf, (int)sizeof buf);
    FILE *f;
    if (n <= 0) return 0;
    mkdir(dir, 0755);
    hb_prefs_path(dir, addr, path, (int)sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "w");
    if (!f) return 0;
    fwrite(buf, 1, (size_t)n, f);
    fclose(f);
    return rename(tmp, path) == 0;
}
