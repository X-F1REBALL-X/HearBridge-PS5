/* Developed by X-F1REBALL-X. */
#include "blocklist.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
/* kind: 0 manual, 1 auto (a controller seen), 2 allowed (user unblocked an
 * auto entry: never auto-added again, not blocked). */
static unsigned char g_addr[HB_BLOCK_MAX][6];
static unsigned char g_kind[HB_BLOCK_MAX];
static int g_n;
static const char *const k_kind[] = { "manual", "auto", "allow" };
static char g_path[128] = HB_BLOCK_PATH;

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') c += 'a' - 'A';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int hb_block_parse(const char *s, unsigned char out[6])
{
    int i, hi, lo;
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    for (i = 0; i < 6; i++) {
        hi = hexv((unsigned char)s[0]);
        lo = hi < 0 ? -1 : hexv((unsigned char)s[1]);
        if (lo < 0) return 0;
        out[i] = (unsigned char)(hi << 4 | lo);
        s += 2;
        if (i < 5) {
            if (*s != ':' && *s != '-') return 0;
            s++;
        }
    }
    return !(*s && *s != '\r' && *s != '\n' && *s != ' ' && *s != '&');
}

static int find(const unsigned char a[6])
{
    int i;
    for (i = 0; i < g_n; i++)
        if (!memcmp(g_addr[i], a, 6)) return i;
    return -1;
}

static int save_locked(void)
{
    char tmp[140];
    FILE *f;
    int i, ok;
    snprintf(tmp, sizeof tmp, "%s.tmp", g_path);
    f = fopen(tmp, "w");
    if (!f) return 0;
    for (i = 0; i < g_n; i++)
        fprintf(f, "%02X:%02X:%02X:%02X:%02X:%02X %s\n", g_addr[i][0], g_addr[i][1],
                g_addr[i][2], g_addr[i][3], g_addr[i][4], g_addr[i][5], k_kind[g_kind[i]]);
    ok = fclose(f) == 0;
    return ok && rename(tmp, g_path) == 0;
}

int hb_block_load(const char *path)
{
    FILE *f;
    char line[64];
    unsigned char a[6];
    int n;
    pthread_mutex_lock(&mu);
    if (path) snprintf(g_path, sizeof g_path, "%s", path);
    g_n = 0;
    f = fopen(g_path, "r");
    if (f) {
        while (g_n < HB_BLOCK_MAX && fgets(line, sizeof line, f))
            if (hb_block_parse(line, a) && find(a) < 0) {
                g_kind[g_n] = strstr(line, " allow") ? 2 : strstr(line, " auto") ? 1 : 0;
                memcpy(g_addr[g_n++], a, 6);
            }
        fclose(f);
    }
    n = g_n;
    pthread_mutex_unlock(&mu);
    return n;
}

int hb_block_has(const unsigned char addr[6])
{
    int r;
    if (!addr) return 0;
    pthread_mutex_lock(&mu);
    r = find(addr);
    r = r >= 0 && g_kind[r] != 2;
    pthread_mutex_unlock(&mu);
    return r;
}

static int add_locked(const unsigned char addr[6], int kind)
{
    int i = find(addr);
    if (i >= 0) {
        if (kind == 1) return 0;                /* auto: never over a user choice */
        if (g_kind[i] == kind) return 1;
        g_kind[i] = (unsigned char)kind;
        return save_locked();
    }
    if (g_n >= HB_BLOCK_MAX) return 0;
    memcpy(g_addr[g_n], addr, 6);
    g_kind[g_n++] = (unsigned char)kind;
    return save_locked();
}

int hb_block_add(const unsigned char addr[6])
{
    int ok;
    pthread_mutex_lock(&mu);
    ok = add_locked(addr, 0);
    pthread_mutex_unlock(&mu);
    return ok;
}

int hb_block_remove(const unsigned char addr[6])
{
    int i, ok = 1;
    pthread_mutex_lock(&mu);
    i = find(addr);
    if (i >= 0 && g_kind[i] == 1) {
        g_kind[i] = 2;                          /* unblocked: remembered as allowed */
        ok = save_locked();
    } else if (i >= 0) {
        memmove(g_addr[i], g_addr[i + 1], (size_t)(g_n - i - 1) * 6);
        memmove(g_kind + i, g_kind + i + 1, (size_t)(g_n - i - 1));
        g_n--;
        ok = save_locked();
    }
    pthread_mutex_unlock(&mu);
    return ok;
}

int hb_block_json(char *out, int cap)
{
    int i, len, w;
    if (!out || cap < 64) return -1;
    pthread_mutex_lock(&mu);
    len = snprintf(out, (size_t)cap, "{\"blocked\":[");
    for (i = 0, w = 0; i < g_n && len + 48 < cap; i++) {
        if (g_kind[i] == 2) continue;
        len += snprintf(out + len, (size_t)(cap - len),
                        "%s{\"addr\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"auto\":%d}",
                        w++ ? "," : "", g_addr[i][0], g_addr[i][1], g_addr[i][2],
                        g_addr[i][3], g_addr[i][4], g_addr[i][5], g_kind[i] == 1);
    }
    pthread_mutex_unlock(&mu);
    if (len + 3 >= cap) return -1;
    len += snprintf(out + len, (size_t)(cap - len), "]}");
    return len;
}

int hb_block_is_controller(unsigned cod, const char *name)
{
    unsigned major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;
    if (major == 0x05 && ((minor & 0x0F) == 0x01 || (minor & 0x0F) == 0x02))
        return 1;                               /* peripheral: joystick / gamepad */
    if (name && (strstr(name, "Wireless Controller") || strstr(name, "DualSense") ||
                 strstr(name, "DUALSHOCK")))
        return 1;                               /* Sony DualSense / DS4 names */
    return 0;
}

int hb_block_auto(const unsigned char addr[6], unsigned cod, const char *name)
{
    int r;
    if (!addr || !hb_block_is_controller(cod, name)) return 0;
    pthread_mutex_lock(&mu);
    r = find(addr) < 0 && add_locked(addr, 1);
    pthread_mutex_unlock(&mu);
    return r;
}
