/* notify.c - on-screen toast via the kernel notification service.
 *
 * sceKernelSendNotificationRequest takes a fixed 3120-byte request whose
 * text field starts at offset 45; everything else may stay zero. */
#include "notify.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define NOTIF_TEXT_OFFSET 45
#define NOTIF_TEXT_BYTES  3075

struct sce_notification {
    unsigned char head[NOTIF_TEXT_OFFSET];
    char text[NOTIF_TEXT_BYTES];
};

int sceKernelSendNotificationRequest(int, struct sce_notification *, size_t, int);

static struct sce_notification toast;

void notify(const char *fmt, ...)
{
    va_list va;

    memset(&toast, 0, sizeof toast);
    va_start(va, fmt);
    (void)vsnprintf(toast.text, NOTIF_TEXT_BYTES, fmt, va);
    va_end(va);
    (void)sceKernelSendNotificationRequest(0, &toast, sizeof(struct sce_notification), 0);
}
