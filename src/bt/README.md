# BT transport (HCI for A2DP)

Independent HCI layer for the HearBridge A2DP source on the console's first
HCI interface.

| File | Role |
| --- | --- |
| `hci.h` | Transport-neutral HCI callback table |
| `hci_usb.c` | `/dev/ugen0.2` transport (ugen fs API, opened beside the system driver) |
| `usb_hci_desc.c` | Finds HCI interfaces/endpoints in the configuration descriptor |
| `hci_evasm.c` | Reassembles HCI events from interrupt-endpoint chunks |
| `hci_cmd.c` | Minimal synchronous Command Complete / Status helper |
| `smp_crypto.c` / `crc32.c` | AES-128, AES-CMAC, SMP f4/f5/f6/g2/ah, CRC-32 (tests in `tests/`) |

Shared `util`, `log` and `lock` live in `src/`.

**Conflict:** only one payload may own the first HCI interface at a time.
