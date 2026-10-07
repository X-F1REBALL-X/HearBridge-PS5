/* lock.c - single-instance guard, see lock.h. */
#include "lock.h"
#include "log.h"

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char held_path[256];

long lock_boot_time(void)
{
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    struct timeval bt;
    size_t sz = sizeof bt;

    memset(&bt, 0, sizeof bt);
    if (sysctl(mib, 2, &bt, &sz, NULL, 0) != 0)
        return 0;
    return (long)bt.tv_sec;
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

int lock_take(const char *path)
{
    long boot = lock_boot_time();
    FILE *f;

    if (owner_alive(path, boot)) {
        log_line("lock: %s belongs to a running instance", path);
        return 0;
    }
    f = fopen(path, "w");
    if (!f) {
        log_line("lock: cannot write %s (errno %d)", path, errno);
        return 0;
    }
    fprintf(f, "%ld %ld\n", (long)getpid(), boot);
    fclose(f);
    snprintf(held_path, sizeof held_path, "%s", path);
    return 1;
}

void lock_release(void)
{
    if (held_path[0]) {
        unlink(held_path);
        held_path[0] = '\0';
    }
}
