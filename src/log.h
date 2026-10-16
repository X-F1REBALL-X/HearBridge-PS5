/* log.h - line-oriented diagnostics. Lines go to the file given to
 * log_open(), or to stderr when no file is open. */
#ifndef HEARBRIDGE_LOG_H
#define HEARBRIDGE_LOG_H

#include <stddef.h>

/* printf-style; a newline is appended. */
void log_line(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* Returns 1 if `file` was opened for appending. */
int  log_open(const char *file);
void log_close(void);

/* The last few KB of log lines (kept in memory), oldest first, whole lines.
 * Returns the length written; out is always NUL-terminated when cap > 0. */
int  log_tail(char *out, size_t cap);

#endif
