/* Developed by X-F1REBALL-X. */
#ifndef HB_UTF8_H
#define HB_UTF8_H
#include <stddef.h>
/* In place: keeps valid UTF-8, turns invalid bytes into '?', control
 * characters into ' ', drops a sequence cut off at the end (truncated
 * names), trims trailing spaces. Returns the new length. */
size_t hb_utf8_clean(char *s);
/* Copies at most cap-1 bytes of src without splitting a character, then
 * cleans it. */
void hb_utf8_copy(char *dst, size_t cap, const char *src, size_t srclen);
#endif
