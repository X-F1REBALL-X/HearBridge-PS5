/* lock.c - single-instance guard, see lock.h.
 *
 * The running instance keeps the lock file open with flock(LOCK_EX). The
 * kernel drops that lock when the process ends for any reason (Stop,
 * crash, kill), so a lock left behind by a dead instance can never block a
 * new run, even if its pid has been reused by another process. The file
 * also records "<pid> <boot-time> F" for the log; "F" says the writer held
 * the flock. Records without "F" come from 1.0.1 and older, which only
 * used the pid check; for those (and on file systems without flock) the
 * old pid + boot-time check still applies. */
#include "lock.h"
#include "log.h"

#include <sys/types.h>
#include <sys/file.h>
#ifndef HB_LOCK_HOST_TEST
#include <sys/sysctl.h>
#endif
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char held_path[256];
static int held_fd = -1;

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

/* Parse "<pid> <boot> [F]". Returns the number of fields read. */
static int read_record(int fd, long *pid, long *when, int *flocked)
{
    char buf[64], f = 0;
    ssize_t n;
    int got;

    *pid = *when = 0;
    *flocked = 0;
    if (lseek(fd, 0, SEEK_SET) < 0) return 0;
    n = read(fd, buf, sizeof buf - 1);
    if (n <= 0) return 0;
    buf[n] = 0;
    got = sscanf(buf, "%ld %ld %c", pid, when, &f);
    *flocked = got == 3 && f == 'F';
    return got;
}

/* Is the instance described by the record still running (pid check only)? */
static int pid_alive(long pid, long when, long boot)
{
    if (pid <= 0 || pid == (long)getpid())
        return 0;
    if (boot && when != boot)
        return 0;                       /* written during an earlier boot */
    if (kill((pid_t)pid, 0) == 0 || errno == EPERM)
        return 1;
    return 0;
}

int lock_decide(int flock_result, int flock_errno, int fields, long pid, long when,
                int flocked, long boot, long self)
{
    if (flock_result != 0) {
        if (flock_errno == EWOULDBLOCK || flock_errno == EAGAIN)
            return LOCK_BUSY;           /* a live instance holds the flock */
        /* No flock on this file system: fall back to the pid check. */
        if (fields >= 2 && pid != self && pid_alive(pid, when, boot)) return LOCK_BUSY;
        return LOCK_OK;
    }
    /* We hold the flock. A record written under flock is therefore stale
     * (its writer is gone, whatever its pid is doing now). An old-format
     * record from 1.0.1 with a live pid may still be a running 1.0.1. */
    if (fields >= 2 && !flocked && pid != self && pid_alive(pid, when, boot))
        return LOCK_BUSY;
    return LOCK_OK;
}

int lock_take_ex(const char *path, int *err)
{
    long boot = lock_boot_time(), pid, when;
    int fd, fr, fe, fields, flocked, d;
    char rec[64];

    if (err) *err = 0;
    fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        int e = errno;
        log_line("lock: cannot write %s (errno %d: %s)", path, e, strerror(e));
        if (err) *err = e;
        return LOCK_NO_WRITE;
    }
    fr = flock(fd, LOCK_EX | LOCK_NB);
    fe = fr ? errno : 0;
    fields = read_record(fd, &pid, &when, &flocked);
    d = lock_decide(fr, fe, fields, pid, when, flocked, boot, (long)getpid());
    if (d == LOCK_BUSY) {
        log_line("lock: %s belongs to a running instance (pid %ld%s)", path, pid,
                 fr ? ", flock held" : ", 1.0.1-style record");
        close(fd);
        return LOCK_BUSY;
    }
    if (fields >= 2)
        log_line("lock: stale record (pid %ld, boot %ld%s) taken over", pid, when,
                 flocked ? ", written under flock" : "");
    if (fr)
        log_line("lock: flock unavailable (errno %d), pid check only", fe);
    snprintf(rec, sizeof rec, "%ld %ld%s\n", (long)getpid(), boot, fr ? "" : " F");
    if (ftruncate(fd, 0) != 0 || lseek(fd, 0, SEEK_SET) < 0 ||
        write(fd, rec, strlen(rec)) != (ssize_t)strlen(rec)) {
        int e = errno;
        log_line("lock: cannot write %s (errno %d: %s)", path, e, strerror(e));
        close(fd);
        if (err) *err = e;
        return LOCK_NO_WRITE;
    }
    held_fd = fd;                      /* keep open: the flock lives with it */
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
    if (held_fd >= 0) {
        close(held_fd);
        held_fd = -1;
    }
}
