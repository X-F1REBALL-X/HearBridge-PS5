/* crc32.c - bit-serial CRC-32, see crc32.h. Small and table-free; the data
 * volumes here are tiny. */
#include "crc32.h"

#define CRC32_REFLECTED_POLY 0xEDB88320u

uint32_t crc32_update(uint32_t crc, const void *buf, size_t n)
{
    const uint8_t *p = buf;

    crc = ~crc;
    while (n--) {
        int bit;
        crc ^= *p++;
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (CRC32_REFLECTED_POLY & (0u - (crc & 1u)));
    }
    return ~crc;
}

uint32_t crc32_ieee(const void *buf, size_t n)
{
    return crc32_update(0, buf, n);
}

void crc32_hid_output_trailer(uint8_t *report, int len)
{
    static const uint8_t hidp_hdr = 0xA2;
    uint32_t c;
    int i;

    if (len < 4)
        return;
    c = crc32_update(0, &hidp_hdr, 1);
    c = crc32_update(c, report, (size_t)(len - 4));
    for (i = 0; i < 4; i++)
        report[len - 4 + i] = (uint8_t)(c >> (8 * i));
}
