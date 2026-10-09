/* hcidbg.c - see hcidbg.h. */
#include "hcidbg.h"
#include "util.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define RING_LINES 512
#define LINE_MAX   200

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_on;
static char g_ring[RING_LINES][LINE_MAX];
static unsigned g_seq;                 /* lines written so far */

static struct {
    int pending;
    unsigned op;
    unsigned char p[255];
    int n;
} g_q;

static void put_line(const char *kind, unsigned op, const unsigned char *b, int n)
{
    char *l;
    int k, i;
    pthread_mutex_lock(&g_mu);
    l = g_ring[g_seq % RING_LINES];
    k = snprintf(l, LINE_MAX, "%u %ld %s", g_seq + 1, now_ms(), kind);
    if (op) k += snprintf(l + k, (size_t)(LINE_MAX - k), " %04x", op);
    for (i = 0; i < n && k < LINE_MAX - 4; i++)
        k += snprintf(l + k, (size_t)(LINE_MAX - k), " %02x", b[i]);
    if (i < n && k < LINE_MAX - 4) snprintf(l + k, (size_t)(LINE_MAX - k), " ..");
    g_seq++;
    pthread_mutex_unlock(&g_mu);
}

void hcidbg_enable(void)
{
    g_on = 1;
}

int hcidbg_enabled(void)
{
    return g_on;
}

void hcidbg_cmd(unsigned op, const unsigned char *p, int n)
{
    if (g_on) put_line("CMD", op, p, n);
}

void hcidbg_event(const unsigned char *ev, int n)
{
    if (g_on) put_line("EVT", 0, ev, n);
}

int hcidbg_text(unsigned since, char *out, int max)
{
    unsigned s, first;
    int w = 0;
    pthread_mutex_lock(&g_mu);
    first = g_seq > RING_LINES ? g_seq - RING_LINES : 0;
    if (since < first) since = first;
    for (s = since; s < g_seq && w < max - LINE_MAX - 2; s++)
        w += snprintf(out + w, (size_t)(max - w), "%s\n", g_ring[s % RING_LINES]);
    pthread_mutex_unlock(&g_mu);
    return w;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int hcidbg_queue(const char *q)
{
    const char *o = q ? strstr(q, "op=") : NULL, *p = q ? strstr(q, "p=") : NULL;
    unsigned op = 0;
    unsigned char buf[255];
    int n = 0, i;
    if (!g_on || !o) return 0;
    if (p == o + 1) p = strstr(o + 3, "p=");       /* "op=" contains "p=" */
    for (i = 0, o += 3; i < 4; i++, o++) {
        int v = hexval(*o);
        if (v < 0) return 0;
        op = op << 4 | (unsigned)v;
    }
    if (p) {
        for (p += 2; hexval(p[0]) >= 0 && hexval(p[1]) >= 0 && n < (int)sizeof buf; p += 2)
            buf[n++] = (unsigned char)(hexval(p[0]) << 4 | hexval(p[1]));
    }
    pthread_mutex_lock(&g_mu);
    if (g_q.pending) { pthread_mutex_unlock(&g_mu); return 0; }
    g_q.op = op;
    memcpy(g_q.p, buf, (size_t)n);
    g_q.n = n;
    g_q.pending = 1;
    pthread_mutex_unlock(&g_mu);
    return 1;
}

void hcidbg_service(hci_t hci)
{
    unsigned op;
    unsigned char p[255];
    int n;
    pthread_mutex_lock(&g_mu);
    if (!g_q.pending) { pthread_mutex_unlock(&g_mu); return; }
    op = g_q.op;
    n = g_q.n;
    memcpy(p, g_q.p, (size_t)n);
    g_q.pending = 0;
    pthread_mutex_unlock(&g_mu);
    if (hci.ops && hci.ops->cmd && !hci.ops->cmd(hci.ctx, op, p, n))
        put_line("ERR send failed", op, p, n);
}
