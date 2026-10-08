/* lock.c - single-instance guard, see lock.h. */
#include "lock.h"
#include "log.h"

#include <sys/types.h>
#ifndef HB_LOCK_HOST_TEST
#include <sys/sysctl.h>
#endif
#include <sys/time.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char held_path[256];

long lock_boot_time(void)
{
#ifdef HB_LOCK_HOST_TEST
    return 0;          /* host test: no boot-time check */
#else
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    struct timeval bt;
    size_t sz = sizeof bt;

    memset(&bt, 0, sizeof bt);
    if (sysctl(mib, 2, &bt, &sz, NULL, 0) != 0)
        return 0;
    return (long)bt.tv_sec;
#endif
}

/* Is the instance described by the file still running? */
static int owner_alive(const char *path, long boot)
{
    FILE *f = fopen(path, "r");
    long pid = 0, when = 0;
    int got;

    if (!f)
        return 0;
    got = fscanf(f, "%ld %ld", &pid, &when);
    fclose(f);
    if (got != 2 || pid <= 0 || pid == (long)getpid())
        return 0;
    if (boot && when != boot)
        return 0;                       /* written during an earlier boot */
    if (kill((pid_t)pid, 0) == 0 || errno == EPERM)
        return 1;
    return 0;
}

int lock_take_ex(const char *path, int *err)
{
    long boot = lock_boot_time();
    FILE *f;

    if (err) *err = 0;
    if (owner_alive(path, boot)) {
        log_line("lock: %s belongs to a running instance", path);
        return LOCK_BUSY;
    }
    f = fopen(path, "w");
    if (!f) {
        int e = errno;
        log_line("lock: cannot write %s (errno %d: %s)", path, e, strerror(e));
        if (err) *err = e;
        return LOCK_NO_WRITE;
    }
    fprintf(f, "%ld %ld\n", (long)getpid(), boot);
    if (fclose(f) != 0) {
        int e = errno;
        log_line("lock: cannot write %s (errno %d: %s)", path, e, strerror(e));
        unlink(path);
        if (err) *err = e;
        return LOCK_NO_WRITE;
    }
    snprintf(held_path, sizeof held_path, "%s", path);
    return LOCK_OK;
}

int lock_take(const char *path)
{
    return lock_take_ex(path, NULL) == LOCK_OK;
}

void lock_release(void)
{
    if (held_path[0]) {
        unlink(held_path);
        held_path[0] = '\0';
    }
}
