/* diag.h - plain-text diagnostics report ("key: value" lines).
 *
 * Collected while HearBridge starts (firmware, model, credentials, every
 * step of the home-screen tile, USB devices, audio libraries) and kept in
 * memory so the web server can return it at /api/diag. diag_save() also
 * writes it to a file (/data/hearbridge/diag.txt on the console) so it can
 * be fetched over FTP when the page does not open.
 * Thread-safe. Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_DIAG_H
#define HEARBRIDGE_DIAG_H

#include <stddef.h>

#define DIAG_MAX_ENTRIES 96
#define DIAG_KEY_MAX     40
#define DIAG_VAL_MAX     480

/* Forget everything; `path` (may be NULL) is where diag_save() writes. */
void diag_init(const char *path);

/* Change where diag_save() writes (NULL = nowhere) without clearing. */
void diag_set_path(const char *path);

/* Set `key` to the formatted value. An existing key keeps its position and
 * gets the new value; a new key is appended (silently dropped when full).
 * Newlines in the value are turned into spaces. */
void diag_set(const char *key, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Render the report into out (always NUL-terminated when cap > 0).
 * Returns the length written. */
int diag_text(char *out, size_t cap);

/* Write the report to the path given to diag_init(). 0 on success, -1 on
 * error (errno set) or when no path was given. */
int diag_save(void);

#endif
