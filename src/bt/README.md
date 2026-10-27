# BT transport (HCI for A2DP)

Independent HCI layer for an A2DP source. It uses a USB Bluetooth adapter when
one is present (the adapter is ours alone) and otherwise attaches to the
console's first HCI interface (shared with the system's DualSense stack).

| File | Role |
| --- | --- |
| `hci.h` | Transport-neutral HCI callback table |
| `hci_usb.c` | ugen(4) transport (fs API); picks a USB adapter or the console chip |
| `usb_hci_desc.c` | Finds HCI interfaces/endpoints (e0/01/01 and ff/01/01) |
| `adapter.c` | Bring-up for a USB adapter: reset, firmware, inquiry mode, class, name |
| `realtek_fw.c` | Realtek ROM-mode patch uploader (`btrtl` formats v1/v2) |
| `hci_evasm.c` | Reassembles HCI events from interrupt-endpoint chunks |
| `hci_cmd.c` | Minimal synchronous Command Complete / Status helper |
| `smp_crypto.c` / `crc32.c` | AES-128, AES-CMAC, SMP f4/f5/f6/g2/ah, CRC-32 (tests in `tests/`) |

Shared `util`, `log` and `lock` live in `src/`.

**Adapter selection:** `/data/hearbridge/adapter` may contain `auto` (default),
`internal`, `usb`, or an explicit `ugenX.Y`. Realtek patches are read from
`/data/hearbridge/firmware/`.

**Conflict:** for the console's own chip, only one payload may own the first
HCI interface at a time. A USB adapter has no such conflict; that is why it is
preferred when present.
