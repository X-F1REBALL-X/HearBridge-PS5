/* adapter_pref.c - see adapter_pref.h. */
#include "adapter_pref.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Host tests point this at a temp file; the console keeps the default path.
 * No getenv: 1.0.2 payloads must not import it. */
static char g_path_override[128];

void hb_adapter_path_override(const char *path)
{
    if (!path || !path[0]) {
        g_path_override[0] = 0;
        return;
    }
    snprintf(g_path_override, sizeof g_path_override, "%s", path);
}

static const char *adapter_path(void)
{
    return g_path_override[0] ? g_path_override : HB_ADAPTER_PATH;
}

static void lower_copy(char *dst, int cap, const char *src)
{
    int i = 0;
    while (src[i] && i < cap - 1) {
        char c = src[i];
        dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        i++;
    }
    dst[i] = 0;
}

int hb_adapter_selection(char *node, int cap)
{
    FILE *f;
    char line[64], raw[32], word[32];
    int n = 0, i;

    if (node && cap > 0) node[0] = 0;
    f = fopen(adapter_path(), "r");
    if (!f) return HB_SEL_AUTO;
    if (!fgets(line, sizeof line, f)) line[0] = 0;
    fclose(f);
    line[strcspn(line, "\r\n")] = 0;
    while (line[n] == ' ' || line[n] == '\t') n++;
    for (i = 0; line[n] && line[n] != ' ' && line[n] != '\t' && i < (int)sizeof raw - 1; n++)
        raw[i++] = line[n];
    raw[i] = 0;
    lower_copy(word, (int)sizeof word, raw);
    if (!word[0] || !strcmp(word, "auto")) return HB_SEL_AUTO;
    if (!strcmp(word, "internal") || !strcmp(word, "console")) return HB_SEL_INTERNAL;
    if (!strcmp(word, "usb") || !strcmp(word, "external") || !strcmp(word, "adapter"))
        return HB_SEL_USB;
    if (!strncmp(word, "/dev/ugen", 9) || !strncmp(word, "ugen", 4)) {
        if (node && cap > 0)
            snprintf(node, (size_t)cap, "%s%s", word[0] == '/' ? "" : "/dev/", word);
        return HB_SEL_NODE;
    }
    return HB_SEL_AUTO;
}

void hb_adapter_pref_word(char *out, int cap)
{
    char node[32];
    int sel;

    if (!out || cap < 2) return;
    sel = hb_adapter_selection(node, (int)sizeof node);
    if (sel == HB_SEL_INTERNAL) snprintf(out, (size_t)cap, "internal");
    else if (sel == HB_SEL_USB) snprintf(out, (size_t)cap, "usb");
    else if (sel == HB_SEL_NODE) {
        const char *p = node;
        if (!strncmp(p, "/dev/", 5)) p += 5;
        snprintf(out, (size_t)cap, "%s", p);
    } else snprintf(out, (size_t)cap, "auto");
}

int hb_adapter_pref_set(const char *word)
{
    char norm[32], line[40];
    FILE *f;
    int i = 0;

    if (!word || !word[0]) return 0;
    while (word[i] == ' ' || word[i] == '\t') i++;
    lower_copy(norm, (int)sizeof norm, word + i);
    if (!strcmp(norm, "auto") || !strcmp(norm, "internal") || !strcmp(norm, "console") ||
        !strcmp(norm, "usb") || !strcmp(norm, "external") || !strcmp(norm, "adapter")) {
        if (!strcmp(norm, "console")) snprintf(line, sizeof line, "internal\n");
        else if (!strcmp(norm, "external") || !strcmp(norm, "adapter"))
            snprintf(line, sizeof line, "usb\n");
        else snprintf(line, sizeof line, "%s\n", norm);
    } else if (!strncmp(norm, "/dev/ugen", 9) || !strncmp(norm, "ugen", 4)) {
        const char *p = norm[0] == '/' ? norm + 5 : norm;
        int n = 0;
        if (strncmp(p, "ugen", 4)) return 0;
        n = 4;
        while (p[n] >= '0' && p[n] <= '9') n++;
        if (p[n] != '.') return 0;
        n++;
        if (!(p[n] >= '0' && p[n] <= '9')) return 0;
        while (p[n] >= '0' && p[n] <= '9') n++;
        if (p[n]) return 0;
        snprintf(line, sizeof line, "%.*s\n", n, p);
    } else return 0;

    /* Same pattern as other HearBridge prefs: write a temp file, rename
     * over the real path, then read back so a failed overwrite cannot leave
     * the old choice (e.g. "internal") looking current. */
    {
        char tmp[160], got[40], want[40];
        const char *path = adapter_path();
        snprintf(tmp, sizeof tmp, "%s.tmp", path);
        f = fopen(tmp, "w");
        if (!f) return 0;
        if (fputs(line, f) < 0) { fclose(f); unlink(tmp); return 0; }
        if (fclose(f) != 0) { unlink(tmp); return 0; }
        if (rename(tmp, path) != 0) { unlink(tmp); return 0; }
        snprintf(want, sizeof want, "%s", line);
        want[strcspn(want, "\r\n")] = 0;
        f = fopen(path, "r");
        if (!f) return 0;
        if (!fgets(got, sizeof got, f)) { fclose(f); return 0; }
        fclose(f);
        got[strcspn(got, "\r\n")] = 0;
        if (strcmp(got, want) != 0) return 0;
    }
    return 1;
}
