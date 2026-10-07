/* smp_crypto.h - Security Manager cryptographic toolbox.
 *
 * Implements the functions defined in the Bluetooth Core spec, Vol 3
 * Part H, section 2.2 (LE Secure Connections and address resolution), on
 * top of AES-128 (FIPS-197) and AES-CMAC (NIST SP 800-38B / RFC 4493).
 *
 * Byte order: all multi-octet inputs and outputs are big-endian ("most
 * significant octet first"), exactly as the values are printed in the spec
 * and its Appendix D sample data. Over-the-air SMP fields are little-endian;
 * use smp_swap() at that boundary. */
#ifndef HEARBRIDGE_SMP_CRYPTO_H
#define HEARBRIDGE_SMP_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/* Security function e: AES-128 encryption of one block. */
void smp_e(const uint8_t k[16], const uint8_t block_in[16], uint8_t block_out[16]);

/* AES-CMAC with 128-bit key over msg[0..n). */
void smp_cmac(const uint8_t key[16], const uint8_t *m, size_t mlen, uint8_t t[16]);

/* ah(k, r): random address hash; r and the result are 24-bit values. */
uint32_t smp_ah(const uint8_t k[16], uint32_t r24);

/* f4(U, V, X, Z): confirm value. U, V are 256-bit, X 128-bit, Z 8-bit. */
void smp_f4(const uint8_t u[32], const uint8_t v[32], const uint8_t x[16],
            uint8_t z, uint8_t confirm[16]);

/* f5(W, N1, N2, A1, A2): MacKey and LTK. A1/A2 = address type (1 octet)
 * followed by the 48-bit address. */
void smp_f5(const uint8_t w[32], const uint8_t n1[16], const uint8_t n2[16],
            const uint8_t a1[7], const uint8_t a2[7],
            uint8_t mackey[16], uint8_t ltk[16]);

/* f6(W, N1, N2, R, IOcap, A1, A2): DHKey check value. */
void smp_f6(const uint8_t w[16], const uint8_t n1[16], const uint8_t n2[16],
            const uint8_t r[16], const uint8_t iocap[3],
            const uint8_t a1[7], const uint8_t a2[7], uint8_t check[16]);

/* g2(U, V, X, Y): numeric comparison value (32 bits; the 6-digit number
 * shown to the user is this mod 1000000). */
uint32_t smp_g2(const uint8_t u[32], const uint8_t v[32],
                const uint8_t x[16], const uint8_t y[16]);

/* Reverse n octets (dst may equal src). */
void smp_swap(uint8_t *to, const uint8_t *from, size_t len);

#endif
