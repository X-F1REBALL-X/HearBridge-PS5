/* Host-side checks: FIPS-197 C.1, RFC 4493, Core spec Vol 3 Part H App. D. */
#include "crc32.h"
#include "smp_crypto.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void hex(const char *s, uint8_t *out, size_t n)
{
    size_t i = 0;
    while (*s && i < n) {
        unsigned v;
        if (*s == ' ') { s++; continue; }
        sscanf(s, "%2x", &v);
        out[i++] = (uint8_t)v;
        s += 2;
    }
}

static void expect(const char *name, const uint8_t *got, const char *want, size_t n)
{
    uint8_t w[80];
    hex(want, w, n);
    if (memcmp(got, w, n)) { printf("FAIL %s\n", name); fails++; }
    else printf("ok   %s\n", name);
}

int main(void)
{
    uint8_t k[16], p[16], o[16], u[32], v[32], x[16], y[16], w[32], n1[16], n2[16],
            a1[7], a2[7], r[16], io[3], mk[16], ltk[16], msg[64];

    hex("000102030405060708090a0b0c0d0e0f", k, 16);
    hex("00112233445566778899aabbccddeeff", p, 16);
    smp_e(k, p, o);
    expect("AES-128 FIPS-197 C.1", o, "69c4e0d86a7b0430d8cdb78070b4c55a", 16);

    hex("2b7e151628aed2a6abf7158809cf4f3c", k, 16);
    smp_cmac(k, msg, 0, o);
    expect("CMAC len0", o, "bb1d6929e95937287fa37d129b756746", 16);
    hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710", msg, 64);
    smp_cmac(k, msg, 16, o);
    expect("CMAC len16", o, "070a16b46b4d4144f79bdd9dd04a287c", 16);
    smp_cmac(k, msg, 40, o);
    expect("CMAC len40", o, "dfa66747de9ae63030ca32611497c827", 16);
    smp_cmac(k, msg, 64, o);
    expect("CMAC len64", o, "51f0bebf7e3b9d92fc49741779363cfe", 16);

    hex("20b003d2f297be2c5e2c83a7e9f9a5b9eff49111acf4fddbcc0301480e359de6", u, 32);
    hex("55188b3d32f6bb9a900afcfbeed4e72a59cb9ac2f19d7cfb6b4fdd49f47fc5fd", v, 32);
    hex("d5cb8454d177733effffb2ec712baeab", x, 16);
    smp_f4(u, v, x, 0, o);
    expect("f4 D.2", o, "f2c916f107a9bd1cf1eda1bea974872d", 16);

    hex("ec0234a357c8ad05341010a60a397d9b99796b13b4f866f1868d34f373bfa698", w, 32);
    hex("d5cb8454d177733effffb2ec712baeab", n1, 16);
    hex("a6e8e7cc25a75f6e216583f7ff3dc4cf", n2, 16);
    hex("0056123737bfce", a1, 7);
    hex("00a713702dcfc1", a2, 7);
    smp_f5(w, n1, n2, a1, a2, mk, ltk);
    expect("f5 D.3 MacKey", mk, "2965f176a1084a02fd3f6a20ce636e20", 16);
    expect("f5 D.3 LTK", ltk, "6986791169d7cd23980522b594750a38", 16);

    hex("12a3343bb453bb5408da42d20c2d0fc8", r, 16);
    hex("010102", io, 3);
    smp_f6(mk, n1, n2, r, io, a1, a2, o);
    expect("f6 D.4", o, "e3c473989cd0e8c5d26c0b09da958f61", 16);

    hex("a6e8e7cc25a75f6e216583f7ff3dc4cf", y, 16);
    {
        uint32_t g = smp_g2(u, v, x, y);
        printf("%s g2 D.5 = %08x (want 2f9ed5ba)\n", g == 0x2f9ed5ba ? "ok  " : "FAIL", g);
        if (g != 0x2f9ed5ba) fails++;
    }

    hex("ec0234a357c8ad05341010a60a397d9b", k, 16);
    {
        uint32_t h = smp_ah(k, 0x708194);
        printf("%s ah D.7 = %06x (want 0dfbaa)\n", h == 0x0dfbaa ? "ok  " : "FAIL", h);
        if (h != 0x0dfbaa) fails++;
    }

    {
        uint32_t c = crc32_ieee("123456789", 9);
        printf("%s crc32 check = %08x\n", c == 0xCBF43926u ? "ok  " : "FAIL", c);
        if (c != 0xCBF43926u) fails++;
    }
    return fails != 0;
}
