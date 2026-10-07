/* Developed by X-F1REBALL-X. */
#include "utf8.h"
#include <string.h>

/* Length of a valid sequence at p (n bytes available), or 0 if invalid,
 * or -1 if valid so far but cut off by the end. */
static int seq_len(const unsigned char *p, size_t n)
{
    unsigned c = p[0], need, cp, i, min;
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) { need = 1; cp = c & 0x1F; min = 0x80; }
    else if (c >= 0xE0 && c <= 0xEF) { need = 2; cp = c & 0x0F; min = 0x800; }
    else if (c >= 0xF0 && c <= 0xF4) { need = 3; cp = c & 0x07; min = 0x10000; }
    else return 0;
    for (i = 1; i <= need; i++) {
        if (i >= n) return -1;
        if ((p[i] & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return (int)need + 1;
}

size_t hb_utf8_clean(char *s)
{
    unsigned char *r = (unsigned char *)s, *w = r;
    size_t n = strlen(s);
    const unsigned char *end = r + n;

    while (r < end) {
        int k = seq_len(r, (size_t)(end - r));
        if (k < 0) break;                       /* cut-off tail: drop it */
        if (k == 0) { *w++ = '?'; r++; continue; }
        if (k == 1 && (*r < 0x20 || *r == 0x7F)) { *w++ = ' '; r++; continue; }
        memmove(w, r, (size_t)k);
        w += k; r += k;
    }
    while (w > (unsigned char *)s && w[-1] == ' ') w--;
    *w = 0;
    return (size_t)(w - (unsigned char *)s);
}

void hb_utf8_copy(char *dst, size_t cap, const char *src, size_t srclen)
{
    size_t n = 0;
    if (!cap) return;
    while (n < srclen && src[n]) n++;            /* stop at an embedded NUL */
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    hb_utf8_clean(dst);
}
