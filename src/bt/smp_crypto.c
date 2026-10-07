/* smp_crypto.c - see smp_crypto.h.
 *
 * AES-128 follows FIPS-197 directly: the S-box is derived at first use from
 * its definition (multiplicative inverse in GF(2^8) followed by the affine
 * map with constant 0x63) rather than stored as a literal table. State is
 * kept column-major as in the standard: s[r + 4c]. */
#include "smp_crypto.h"

#include <string.h>

static uint8_t sbox[256];
static int sbox_ready;

/* Multiply in GF(2^8) modulo x^8 + x^4 + x^3 + x + 1. */
static uint8_t gmul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;

    while (b) {
        if (b & 1)
            r ^= a;
        a = (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1B : 0));
        b >>= 1;
    }
    return r;
}

static uint8_t rotl8(uint8_t v, int k)
{
    return (uint8_t)((v << k) | (v >> (8 - k)));
}

static void build_sbox(void)
{
    int x;

    for (x = 0; x < 256; x++) {
        uint8_t inv = 0, y;
        int c;

        if (x) {                         /* brute-force inverse: fine, once */
            for (c = 1; c < 256; c++)
                if (gmul((uint8_t)x, (uint8_t)c) == 1) {
                    inv = (uint8_t)c;
                    break;
                }
        }
        y = (uint8_t)(inv ^ rotl8(inv, 1) ^ rotl8(inv, 2) ^ rotl8(inv, 3) ^
                      rotl8(inv, 4) ^ 0x63);
        sbox[x] = y;
    }
    sbox_ready = 1;
}

/* Expand to 11 round keys of 16 octets (FIPS-197 section 5.2). */
static void expand_key(const uint8_t key[16], uint8_t rk[176])
{
    int i;
    uint8_t rcon = 0x01;

    for (i = 0; i < 16; i++) rk[i] = key[i];
    for (i = 16; i < 176; i += 4) {
        uint8_t w[4];

        int b;

        for (b = 0; b < 4; b++) w[b] = rk[i - 4 + b];
        if ((i & 15) == 0) {
            /* RotWord then SubWord, then XOR Rcon into the first octet. */
            uint8_t t0 = w[0];
            for (b = 0; b < 3; b++) w[b] = sbox[w[b + 1]];
            w[3] = sbox[t0];
            w[0] ^= rcon;
            rcon = gmul(rcon, 2);
        }
        for (b = 0; b < 4; b++) rk[i + b] = (uint8_t)(rk[i + b - 16] ^ w[b]);
    }
}

static void add_round_key(uint8_t s[16], const uint8_t *k)
{
    int i;
    for (i = 0; i < 16; i++)
        s[i] ^= k[i];
}

static void sub_and_shift(uint8_t s[16])
{
    uint8_t t[16];
    int r, c;

    /* row r is rotated left by r positions */
    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++)
            t[r + 4 * c] = sbox[s[r + 4 * ((c + r) & 3)]];
    memcpy(s, t, 16);
}

static void mix_columns(uint8_t s[16])
{
    int c;

    for (c = 0; c < 4; c++) {
        uint8_t *col = s + 4 * c;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = (uint8_t)(gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3);
        col[1] = (uint8_t)(a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3);
        col[2] = (uint8_t)(a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3));
        col[3] = (uint8_t)(gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2));
    }
}

void smp_e(const uint8_t key[16], const uint8_t plain[16], uint8_t cipher[16])
{
    uint8_t rk[176], st[16];
    int round;

    if (!sbox_ready)
        build_sbox();
    expand_key(key, rk);
    memcpy(st, plain, 16);
    add_round_key(st, rk);
    for (round = 1; round <= 10; round++) {
        sub_and_shift(st);
        if (round != 10)
            mix_columns(st);
        add_round_key(st, rk + 16 * round);
    }
    memcpy(cipher, st, 16);
}

/* ---- CMAC (SP 800-38B) ---- */

/* Left shift a 128-bit big-endian value by one; xor Rb=0x87 on carry-out. */
static void dbl(const uint8_t in[16], uint8_t out[16])
{
    uint8_t carry = 0;
    int i;

    for (i = 15; i >= 0; i--) {
        uint8_t next = (uint8_t)(in[i] >> 7);
        out[i] = (uint8_t)((in[i] << 1) | carry);
        carry = next;
    }
    if (carry)
        out[15] ^= 0x87;
}

void smp_cmac(const uint8_t key[16], const uint8_t *msg, size_t n, uint8_t tag[16])
{
    static const uint8_t zero[16];
    uint8_t L[16], K1[16], K2[16], x[16], last[16];
    size_t blocks = n ? (n + 15) / 16 : 1, b;
    int whole = n && (n % 16 == 0);
    int i;

    smp_e(key, zero, L);
    dbl(L, K1);
    dbl(K1, K2);

    memset(last, 0, 16);
    {
        size_t tail = n - (blocks - 1) * 16;
        memcpy(last, msg + (blocks - 1) * 16, tail);
        if (!whole)
            last[tail] = 0x80;
        for (i = 0; i < 16; i++)
            last[i] ^= whole ? K1[i] : K2[i];
    }

    memset(x, 0, 16);
    for (b = 0; b + 1 < blocks; b++) {
        for (i = 0; i < 16; i++)
            x[i] ^= msg[16 * b + i];
        smp_e(key, x, x);
    }
    for (i = 0; i < 16; i++)
        x[i] ^= last[i];
    smp_e(key, x, tag);
}

/* ---- Vol 3 Part H 2.2 functions ---- */

void smp_swap(uint8_t *dst, const uint8_t *src, size_t n)
{
    size_t i;

    for (i = 0; i < n / 2; i++) {
        uint8_t a = src[i], z = src[n - 1 - i];
        dst[i] = z;
        dst[n - 1 - i] = a;
    }
    if (n & 1)
        dst[n / 2] = src[n / 2];
}

uint32_t smp_ah(const uint8_t irk[16], uint32_t r)
{
    uint8_t in[16], out[16];

    /* r' = padding (104 zero bits) || r, MSB-first */
    memset(in, 0, sizeof in);
    in[13] = (uint8_t)(r >> 16);
    in[14] = (uint8_t)(r >> 8);
    in[15] = (uint8_t)r;
    smp_e(irk, in, out);
    return ((uint32_t)out[13] << 16) | ((uint32_t)out[14] << 8) | out[15];
}

void smp_f4(const uint8_t u[32], const uint8_t v[32], const uint8_t x[16],
            uint8_t z, uint8_t out[16])
{
    uint8_t uvz[32 + 32 + 1];       /* U || V || Z */

    memcpy(uvz + 0, u, sizeof uvz / 2);
    memcpy(uvz + 32, v, 32);
    uvz[sizeof uvz - 1] = z;
    smp_cmac(x, uvz, sizeof uvz, out);
}

void smp_f5(const uint8_t w[32], const uint8_t n1[16], const uint8_t n2[16],
            const uint8_t a1[7], const uint8_t a2[7],
            uint8_t mackey[16], uint8_t ltk[16])
{
    static const uint8_t salt[16] = {
        0x6C, 0x88, 0x83, 0x91, 0xAA, 0xF5, 0xA5, 0x38,
        0x60, 0x37, 0x0B, 0xDB, 0x5A, 0x60, 0x83, 0xBE,
    };
    uint8_t t[16], m[53];

    smp_cmac(salt, w, 32, t);

    /* Counter(1) || keyID "btle"(4) || N1 || N2 || A1 || A2 || Length=256(2) */
    {
        uint8_t *q = m;
        *q++ = 0x00;
        *q++ = 0x62; *q++ = 0x74; *q++ = 0x6C; *q++ = 0x65;
        q = (uint8_t *)memcpy(q, n1, 16) + 16;
        q = (uint8_t *)memcpy(q, n2, 16) + 16;
        q = (uint8_t *)memcpy(q, a1, 7) + 7;
        q = (uint8_t *)memcpy(q, a2, 7) + 7;
        q[0] = 0x01;
        q[1] = 0x00;
    }
    smp_cmac(t, m, sizeof m, mackey);
    m[0] = 1;
    smp_cmac(t, m, sizeof m, ltk);
}

void smp_f6(const uint8_t w[16], const uint8_t n1[16], const uint8_t n2[16],
            const uint8_t r[16], const uint8_t iocap[3],
            const uint8_t a1[7], const uint8_t a2[7], uint8_t out[16])
{
    uint8_t m[65];
    uint8_t *p = m;

    memcpy(p, n1, 16);    p += 16;
    memcpy(p, n2, 16);    p += 16;
    memcpy(p, r, 16);     p += 16;
    memcpy(p, iocap, 3);  p += 3;
    memcpy(p, a1, 7);     p += 7;
    memcpy(p, a2, 7);
    smp_cmac(w, m, sizeof m, out);
}

uint32_t smp_g2(const uint8_t u[32], const uint8_t v[32],
                const uint8_t x[16], const uint8_t y[16])
{
    uint8_t m[80], mac[16];

    memcpy(m, u, 32);
    memcpy(m + 32, v, 32);
    memcpy(m + 64, y, 16);
    smp_cmac(x, m, sizeof m, mac);
    return ((uint32_t)mac[12] << 24) | ((uint32_t)mac[13] << 16) |
           ((uint32_t)mac[14] << 8) | mac[15];
}
