/* crc32.h - CRC-32 as specified for IEEE 802.3 (poly 0x04C11DB7, processed
 * LSB-first as 0xEDB88320, init and final XOR 0xFFFFFFFF). Check value for
 * the ASCII string "123456789" is 0xCBF43926. */
#ifndef HEARBRIDGE_CRC32_H
#define HEARBRIDGE_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* Continue a running CRC; start with crc = 0. */
uint32_t crc32_update(uint32_t crc, const void *buf, size_t n);

/* Convenience: one-shot CRC of a buffer. */
uint32_t crc32_ieee(const void *buf, size_t n);

/* Sony HID output reports over Bluetooth end with a CRC-32 over the byte
 * 0xA2 (HIDP DATA|Output) followed by the report body. `len` includes the
 * four trailing CRC bytes, which are overwritten (little-endian). */
void crc32_hid_output_trailer(uint8_t *report, int len);

#endif
