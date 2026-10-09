/* Console side of rest mode: has the system asked to go to rest mode?
 * Everything is looked up at run time (no new imports); a firmware without
 * it just says "unknown" and the resume path still works after the wake.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_REST_SYS_H
#define HEARBRIDGE_REST_SYS_H

/* 1 = we can watch for a rest request on this console. */
int hb_rest_sys_avail(void);
/* 1 = a rest mode request is pending right now. Cheap, poll every ~250 ms. */
int hb_rest_sys_going_down(void);

#endif
