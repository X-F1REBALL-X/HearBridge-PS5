/* Developed by X-F1REBALL-X. */
#include "gameprof.h"

#include <stdio.h>
#include <string.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int hb_game_id_ok(const char *id)
{
    int i;
    if (!id) return 0;
    for (i = 0; i < 4; i++) if (id[i] < 'A' || id[i] > 'Z') return 0;
    for (; i < 9; i++) if (id[i] < '0' || id[i] > '9') return 0;
    return id[9] == 0;
}

/* Signed decimal (no strtol: the payload imports only what 1.0.2 did). */
static long num(const char *s, const char **end)
{
    long v = 0;
    int neg = 0, any = 0;
    const char *p = s;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') { if (v < 100000) v = v * 10 + (*p - '0'); p++; any = 1; }
    *end = any ? p : s;
    return neg ? -v : v;
}

static void parse_line(hb_games *gs, char *line)
{
    hb_game g;
    char *p = line, *sp;
    memset(&g, 0, sizeof g);
    g.gain_pct = 250;
    g.hs_vol = -1;
    sp = strchr(p, ' ');
    if (!sp) return;
    *sp = 0;
    if (!hb_game_id_ok(p)) return;
    snprintf(g.id, sizeof g.id, "%.15s", p);
    p = sp + 1;
    while (*p) {
        char *key = p, *v, *nx;
        const char *end;
        if (!strncmp(p, "name=", 5)) {             /* the rest of the line */
            snprintf(g.name, sizeof g.name, "%s", p + 5);
            break;
        }
        nx = strchr(p, ' ');
        if (nx) *nx = 0;
        v = strchr(key, '=');
        if (v) {
            *v++ = 0;
            if (!strcmp(key, "eq")) g.eq_on = !strcmp(v, "on");
            else if (!strcmp(key, "gain")) { long x = num(v, &end); if (end != v) g.gain_pct = clampi((int)x, 0, 500); }
            else if (!strcmp(key, "hs_vol")) { long x = num(v, &end); if (end != v) g.hs_vol = clampi((int)x, -1, 127); }
            else if (!strcmp(key, "eq_db")) {
                const char *s = v;
                int i;
                for (i = 0; i < 5; i++) {
                    long d = num(s, &end);
                    if (end == s) break;
                    g.eq_db[i] = clampi((int)d, -12, 12);
                    s = end;
                    if (*s == ',') s++;
                }
            }
        }
        if (!nx) break;
        p = nx + 1;
    }
    if (gs->n < HB_GAME_MAX && hb_games_find(gs, g.id) < 0) gs->g[gs->n++] = g;
}

void hb_games_parse(hb_games *gs, const char *t)
{
    char line[256];
    gs->n = 0;
    while (t && *t) {
        const char *e = strchr(t, '\n');
        size_t n = e ? (size_t)(e - t) : strlen(t);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, t, n);
        line[n] = 0;
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        if (line[0] && line[0] != '#') parse_line(gs, line);
        t = e ? e + 1 : NULL;
    }
}

int hb_games_format(const hb_games *gs, char *out, int max)
{
    int i, n = snprintf(out, (size_t)max, "# HearBridge per-game audio, newest first\n");
    for (i = 0; i < gs->n && n > 0 && n < max; i++) {
        const hb_game *g = &gs->g[i];
        char name[HB_GAME_NAME];
        int k;
        /* one line per game: no newlines in the name */
        snprintf(name, sizeof name, "%s", g->name);
        for (k = 0; name[k]; k++) if (name[k] == '\n' || name[k] == '\r') name[k] = ' ';
        n += snprintf(out + n, (size_t)(max - n), "%s eq=%s eq_db=%d,%d,%d,%d,%d gain=%d hs_vol=%d%s%s\n",
                      g->id, g->eq_on ? "on" : "off", g->eq_db[0], g->eq_db[1], g->eq_db[2],
                      g->eq_db[3], g->eq_db[4], g->gain_pct, g->hs_vol, name[0] ? " name=" : "", name);
    }
    return n > 0 && n < max ? n : -1;
}

int hb_games_find(const hb_games *gs, const char *id)
{
    int i;
    if (!id || !id[0]) return -1;
    for (i = 0; i < gs->n; i++) if (!strcmp(gs->g[i].id, id)) return i;
    return -1;
}

void hb_games_put(hb_games *gs, const hb_game *g)
{
    int i = hb_games_find(gs, g->id);
    if (!hb_game_id_ok(g->id)) return;
    if (i < 0) i = gs->n < HB_GAME_MAX ? gs->n++ : HB_GAME_MAX - 1;
    memmove(&gs->g[1], &gs->g[0], sizeof gs->g[0] * (size_t)i);
    gs->g[0] = *g;
}

int hb_games_drop(hb_games *gs, const char *id)
{
    int i = hb_games_find(gs, id);
    if (i < 0) return 0;
    memmove(&gs->g[i], &gs->g[i + 1], sizeof gs->g[0] * (size_t)(gs->n - i - 1));
    gs->n--;
    return 1;
}

int hb_game_decide(const char *applied, const char *cur, int has)
{
    int on = applied && applied[0];
    if (cur && cur[0] && has) return on && !strcmp(applied, cur) ? HB_GAME_KEEP : HB_GAME_APPLY;
    return on ? HB_GAME_RESTORE : HB_GAME_KEEP;
}

int hb_game_json_title(const char *js, char *out, int max)
{
    const char *p = js ? strstr(js, "\"titleName\"") : NULL;
    int n = 0;
    if (!p || max < 2) return 0;
    p += 11;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != ':') return 0;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != '"') return 0;
    while (*p && *p != '"' && n < max - 1) {
        if (*p == '\\') {
            p++;
            if (*p == '"' || *p == '\\' || *p == '/') out[n++] = *p++;
            else if (*p) p++;
            continue;
        }
        if ((unsigned char)*p >= 0x20) out[n++] = *p;
        p++;
    }
    /* never end inside a UTF-8 sequence (cut by max) */
    if (n > 0) {
        int k = n - 1, need;
        unsigned char lead;
        while (k > 0 && ((unsigned char)out[k] & 0xC0) == 0x80) k--;
        lead = (unsigned char)out[k];
        need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (n - k < need) n = k;
    }
    out[n] = 0;
    return n > 0;
}
