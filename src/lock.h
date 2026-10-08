/* lock.h - single-instance guard.
 *
 * A small text file records "<pid> <boot-time>". A second copy refuses to
 * start while that pid is alive in the same boot. A record left over from a
 * previous boot or from a dead process is ignored and overwritten (pids are
 * reused after reboot, so the boot time is what tells records apart). */
#ifndef HEARBRIDGE_LOCK_H
#define HEARBRIDGE_LOCK_H

/* Seconds since the epoch at which the system booted, or 0. */
long lock_boot_time(void);

/* 1: this process now owns the lock at `lockfile`. 0: another live
 * instance owns it, or the file could not be written. */
int  lock_take(const char *lockfile);

/* Same, but tells the cases apart: LOCK_OK, LOCK_BUSY (another live
 * instance) or LOCK_NO_WRITE (the file could not be written; *err gets
 * errno). */
#define LOCK_OK        1
#define LOCK_BUSY      0
#define LOCK_NO_WRITE (-1)
int  lock_take_ex(const char *lockfile, int *err);
void lock_release(void);

#endif
