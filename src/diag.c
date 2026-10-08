/* diag.c - see diag.h. Developed by X-F1REBALL-X. */
#include "diag.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct entry {
    char key[DIAG_KEY_MAX];
    char val[DIAG_VAL_MAX];
};

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static struct entry ent[DIAG_MAX_ENTRIES];
static int n_ent;
static char save_path[128];

void diag_init(const char *path)
{
    pthread_mutex_lock(&mu);
    memset(ent, 0, sizeof ent);
    n_ent = 0;
    snprintf(save_path, sizeof save_path, "%s", path ? path : "");
    pthread_mutex_unlock(&mu);
}

void diag_set(const char *key, const char *fmt, ...)
{
    char val[DIAG_VAL_MAX];
    va_list va;
    int i;
    char *p;

    if (!key || !key[0]) return;
    va_start(va, fmt);
    (void)vsnprintf(val, sizeof val, fmt, va);
    va_end(va);
    for (p = val; *p; p++)
        if (*p == '\n' || *p == '\r') *p = ' ';

    pthread_mutex_lock(&mu);
    for (i = 0; i < n_ent; i++)
        if (!strncmp(ent[i].key, key, DIAG_KEY_MAX - 1)) break;
    if (i == n_ent) {
        if (n_ent == DIAG_MAX_ENTRIES) { pthread_mutex_unlock(&mu); return; }
        snprintf(ent[i].key, sizeof ent[i].key, "%s", key);
        n_ent++;
    }
    snprintf(ent[i].val, sizeof ent[i].val, "%s", val);
    pthread_mutex_unlock(&mu);
}

static int render(char *out, size_t cap)
{
    size_t n = 0;
    int i;

    if (!cap) return 0;
    out[0] = 0;
    for (i = 0; i < n_ent; i++) {
        int w = snprintf(out + n, cap - n, "%s: %s\n", ent[i].key, ent[i].val);
        if (w < 0) break;
        if ((size_t)w >= cap - n) { n = cap - 1; out[n] = 0; break; }
        n += (size_t)w;
    }
    return (int)n;
}

int diag_text(char *out, size_t cap)
{
    int n;
    pthread_mutex_lock(&mu);
    n = render(out, cap);
    pthread_mutex_unlock(&mu);
    return n;
}

int diag_save(void)
{
    static char buf[DIAG_MAX_ENTRIES * (DIAG_KEY_MAX + DIAG_VAL_MAX + 3)];
    char path[128], tmp[140];
    FILE *f;
    int n, ok;

    pthread_mutex_lock(&mu);
    snprintf(path, sizeof path, "%s", save_path);
    n = render(buf, sizeof buf);
    if (!path[0]) { pthread_mutex_unlock(&mu); errno = EINVAL; return -1; }
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "w");
    if (!f) { pthread_mutex_unlock(&mu); return -1; }
    ok = fwrite(buf, 1, (size_t)n, f) == (size_t)n;
    ok = (fclose(f) == 0) && ok;
    pthread_mutex_unlock(&mu);
    if (!ok || rename(tmp, path) != 0) { remove(tmp); return -1; }
    return 0;
}
