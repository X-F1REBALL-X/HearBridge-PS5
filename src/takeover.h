/* takeover.h - a newly started copy cleanly replaces a running one (#25)
 * and shows up as "hearbridge.elf" in the process list (#24).
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_TAKEOVER_H
#define HEARBRIDGE_TAKEOVER_H

#define HB_PROC_NAME "hearbridge.elf"

/* Name this process (p_comm, what process lists show) like other PS5
 * payloads do: thr_set_name on the main thread. */
void hb_set_proc_name(const char *name);

/* pid of another running process with this name, or -1. */
long hb_find_pid(const char *name);

/* Wait up to ms for pid to be gone. 1 = gone. */
int hb_wait_pid_gone(long pid, int ms);

/* Another copy whose lock file is gone (state folder deleted) still runs:
 * found by name, asked to stop (stop file + SIGTERM), killed after
 * graceful_ms. Returns its pid, 0 if there was none. */
long hb_stop_named(const char *name, const char *stop_path, int graceful_ms);

/* The page port is still held (a copy from before 1.3.1, which has no
 * process name of its own, or one still closing): ask it to stop with the
 * stop file and wait up to ms for the port to be free. 0 = it was free,
 * 1 = freed, -1 = still held (something else owns it). */
int hb_port_takeover(int port, const char *stop_path, int ms);

#endif
