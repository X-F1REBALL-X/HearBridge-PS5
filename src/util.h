/* util.h - small helpers used everywhere in HearBridge.
 *
 * Bluetooth wire formats mix endianness: HCI/L2CAP/AVDTP headers are
 * little-endian, SDP data elements are big-endian (Core spec Vol 3 Part B). */
#ifndef HEARBRIDGE_UTIL_H
#define HEARBRIDGE_UTIL_H

#include <stdint.h>

/* Monotonic time in milliseconds. If ph_clock_hook is set (unit tests), its
 * value is returned instead of the system clock. */
long now_ms(void);
extern long (*ph_clock_hook)(void);

static inline unsigned le16(const unsigned char *b)
{
    return (unsigned)(b[1] << 8 | b[0]);
}

static inline int16_t les16(const unsigned char *b)
{
    return (int16_t)(uint16_t)le16(b);
}

static inline unsigned be16(const unsigned char *b)
{
    return (unsigned)(b[0] << 8 | b[1]);
}

static inline uint32_t be32(const unsigned char *b)
{
    return ((uint32_t)be16(b) << 16) | be16(b + 2);
}

static inline void put16(unsigned char *b, unsigned val)
{
    b[0] = (unsigned char)val;
    b[1] = (unsigned char)(val >> 8);
}

static inline void put32(unsigned char *b, uint32_t val)
{
    put16(b, (unsigned)(val & 0xFFFFu));
    put16(b + 2, (unsigned)(val >> 16));
}

#endif
