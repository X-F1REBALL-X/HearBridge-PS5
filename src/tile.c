/* Developed by X-F1REBALL-X. */
#include "tile.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int tile_manifest(char *out, size_t cap, const char *url)
{
    static const char *langs[] = { "en-US", "en-GB", "es-ES", "fr-FR", "de-DE", "pt-BR",
                                   "ru-RU", "ja-JP", "zh-Hans", "it-IT", "ar-AE", "he-IL" };
    size_t i, len = 0;
    int n;

    n = snprintf(out, cap,
                 "{\n  \"titleId\": \"%s\",\n  \"applicationCategoryType\": 65536,\n"
                 "  \"deeplinkUri\": \"%s\",\n  \"localizedParameters\": {\n"
                 "    \"defaultLanguage\": \"en-US\"", HB_TILE_ID, url && url[0] ? url : HB_TILE_URL);
    if (n < 0 || (size_t)n >= cap) return -1;
    len = (size_t)n;
    for (i = 0; i < sizeof langs / sizeof *langs; i++) {
        n = snprintf(out + len, cap - len, ",\n    \"%s\": { \"titleName\": \"%s\" }",
                     langs[i], HB_TILE_NAME);
        if (n < 0 || (size_t)n >= cap - len) return -1;
        len += (size_t)n;
    }
    n = snprintf(out + len, cap - len, "\n  }\n}\n");
    if (n < 0 || (size_t)n >= cap - len) return -1;
    return (int)(len + (size_t)n);
}

/* 1 if path already holds exactly buf. */
static int holds(const char *path, const void *buf, size_t len)
{
    unsigned char chunk[4096];
    const unsigned char *p = buf;
    size_t off = 0, got;
    struct stat st;
    FILE *f;

    if (stat(path, &st) || (size_t)st.st_size != len || !(f = fopen(path, "rb"))) return 0;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) {
        if (off + got > len || memcmp(chunk, p + off, got)) { fclose(f); return 0; }
        off += got;
    }
    fclose(f);
    return off == len;
}

/* Write via a temp file + rename so a half-written icon never shows up. */
static int store(const char *path, const void *buf, size_t len)
{
    char tmp[512];
    FILE *f;
    int ok;

    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!(f = fopen(tmp, "wb"))) return -1;
    ok = fwrite(buf, 1, len, f) == len;
    if (fclose(f)) ok = 0;
    if (!ok || rename(tmp, path)) { unlink(tmp); return -1; }
    return 0;
}

static int ensure_dir(const char *p)
{
    return (mkdir(p, 0777) == 0 || errno == EEXIST) ? 0 : -1;
}

#define P_APP 256
#define P_SYS (P_APP + 16)
#define P_FILE (P_SYS + 16)

static void paths(const char *root, char app[P_APP], char sys[P_SYS],
                  char man[P_FILE], char ico[P_FILE])
{
    snprintf(app, P_APP, "%.200s/%s", root, HB_TILE_ID);
    snprintf(sys, P_SYS, "%s/sce_sys", app);
    snprintf(man, P_FILE, "%s/param.json", sys);
    snprintf(ico, P_FILE, "%s/icon0.png", sys);
}

int tile_files_put(const char *root, const char *url,
                   const unsigned char *png, size_t png_len,
                   const unsigned char *html, size_t html_len)
{
    char app[P_APP], sys[P_SYS], man[P_FILE], ico[P_FILE], js[2048], st[P_FILE];
    int jl, wrote = 0;

    if ((jl = tile_manifest(js, sizeof js, url)) < 0) return -1;
    paths(root, app, sys, man, ico);
    if (ensure_dir(app) || ensure_dir(sys)) return -1;
    if (!holds(man, js, (size_t)jl)) {
        if (store(man, js, (size_t)jl)) return -1;
        wrote = 1;
    }
    if (!holds(ico, png, png_len)) {
        if (store(ico, png, png_len)) return -1;
        wrote = 1;
    }
    snprintf(st, sizeof st, "%s/start.html", app);
    if (html && !holds(st, html, html_len)) {      /* page only: no re-register needed */
        if (store(st, html, html_len)) return -1;
    }
    return wrote;
}

void tile_files_drop(const char *root)
{
    char app[P_APP], sys[P_SYS], man[P_FILE], ico[P_FILE];

    paths(root, app, sys, man, ico);
    unlink(man);
    unlink(ico);
    {
        char st[P_FILE];
        snprintf(st, sizeof st, "%s/start.html", app);
        unlink(st);
    }
    rmdir(sys);
    rmdir(app);
}

int tile_need_register(int files_changed, int marker_exists, int appmeta_exists)
{
    return files_changed || !marker_exists || !appmeta_exists;
}

static const char *yn(int v) { return v ? "yes" : "no"; }

#define ADD(...) do { if ((size_t)n < cap) n += snprintf(out + n, cap - (size_t)n, __VA_ARGS__); } while (0)

int tile_report_line(const tile_report *r, char *out, size_t cap)
{
    int n = 0;

    if (!cap) return 0;
    out[0] = 0;
    ADD("files %s (errno %d); marker %s; appmeta before %s",
        r->files < 0 ? "ERROR" : r->files ? "written" : "unchanged",
        r->files_errno, yn(r->marker), yn(r->appmeta_before));
    if (r->files >= 0 && r->skipped) {
        ADD("; already registered, skipped");
    } else if (r->files >= 0) {
        ADD("; authid %#llx -> %#llx", r->authid_before, r->authid_used);
        ADD("; init %#x (errno %d)", (unsigned)r->init_rc, r->init_errno);
        if (!r->init_rc) {
            ADD("; TitleDir %s", r->titledir_found ? "found" : "NOT FOUND");
            if (r->titledir_called)
                ADD(" -> %#x (errno %d)", (unsigned)r->titledir_rc, r->titledir_errno);
            if (r->all_called)
                ADD("; InstallAll -> %#x (errno %d)", (unsigned)r->all_rc, r->all_errno);
            else if (!r->all_found)
                ADD("; InstallAll NOT FOUND");
            ADD("; appmeta after %s", yn(r->appmeta_after));
        }
    }
    if (r->result)
        ADD("; RESULT failed at %s (%#x)", r->failed ? r->failed : "?", (unsigned)r->code);
    else
        ADD("; RESULT ok");
    if ((size_t)n >= cap) n = (int)cap - 1;
    return n;
}

#undef ADD
