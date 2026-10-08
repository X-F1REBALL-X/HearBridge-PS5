/* lock.h - single-instance guard.
 *
 * The running instance holds flock() on a small text file (released by the
 * kernel when the process ends, so a crash never leaves a blocking lock)
 * and records "<pid> <boot-time> F" in it. Records from 1.0.1 and older
 * (no "F") fall back to the old check: refuse while that pid is alive in
 * the same boot. See lock.c. */
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

/* The decision lock_take_ex() makes, exposed for host tests.
 * flock_result/errno: result of flock(LOCK_EX|LOCK_NB) on the lock file;
 * fields/pid/when/flocked: the record found in it ("<pid> <boot> [F]");
 * boot: current boot time (0 = unknown); self: our pid. */
int  lock_decide(int flock_result, int flock_errno, int fields, long pid, long when,
                 int flocked, long boot, long self);
void lock_release(void);

#endif
