/* The one place HearBridge PS5's version is set. */
#ifndef HEARBRIDGE_VERSION_H
#define HEARBRIDGE_VERSION_H

#ifndef HEARBRIDGE_VERSION
#define HEARBRIDGE_VERSION "1.0.1"
#endif

/* Build variant, appended to the ELF name and shown in diagnostics only
 * (the version shown on screen stays HEARBRIDGE_VERSION). Empty = default. */
#ifndef HEARBRIDGE_FLAVOR
#define HEARBRIDGE_FLAVOR "fw13.60"
#endif

#endif
