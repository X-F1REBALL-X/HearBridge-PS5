/* sysinfo.h - console facts for the diagnostics report (firmware, model,
 * process credentials). Every system function is looked up at run time, so
 * a firmware without one only shows "unknown" instead of stopping the
 * payload from loading. Console only. */
#ifndef HEARBRIDGE_SYSINFO_H
#define HEARBRIDGE_SYSINFO_H

/* Address of a libkernel export by NID (tries libkernel_web, libkernel,
 * libkernel_sys), or NULL. */
void *sysinfo_libkernel_nid(const char *nid);

/* Fill the "firmware", "model", "process" ... diag keys and log them. */
void sysinfo_collect(void);

#endif
