#ifndef HEARBRIDGE_NOTIFY_H
#define HEARBRIDGE_NOTIFY_H

void notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
