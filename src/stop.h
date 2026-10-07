/* stop.h - clean shutdown request via /data/hearbridge/stop. */
#ifndef HEARBRIDGE_STOP_H
#define HEARBRIDGE_STOP_H

#define HB_STOP_PATH "/data/hearbridge/stop"

/* 1 once a stop was requested (stop file seen or SIGTERM/SIGINT/SIGHUP).
 * The file is checked at most every ~500 ms; cheap to call often. */
int  hb_stop_requested(void);

/* Request a stop from inside the app (web Stop button). */
void hb_stop_request(void);

/* Install signal handlers and remove a stale stop file from a past run. */
void hb_stop_init(void);

/* Remove the stop file (called on exit). */
void hb_stop_clear(void);

#endif
