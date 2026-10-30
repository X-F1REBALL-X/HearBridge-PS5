/* adapter.h - bring up a USB Bluetooth adapter that belongs to us alone.
 *
 * The console's own controller is only ever attached to: its settings and
 * links are shared with the system stack. An adapter in a USB port is ours
 * from HCI Reset onwards, so the reset, a Realtek ROM-mode firmware patch
 * when the stick needs one (see realtek_fw.h; CSR and other plain HCI
 * dongles skip it), the inquiry mode, class and local name are set here.
 * The internal path is a no-op.
 */
#ifndef HEARBRIDGE_ADAPTER_H
#define HEARBRIDGE_ADAPTER_H

#include "hci.h"

/* 1 = ready (internal path always), 0 = stop. `why` (may be NULL) receives
 * the user-facing reason on failure, e.g. a missing Realtek firmware file. */
int bt_adapter_bringup(hci_t hci, int external, char *why, int why_cap);

#endif
