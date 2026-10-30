# Bundled Realtek USB Bluetooth firmware

Files here are copied from the
[linux-firmware `rtl_bt` tree](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/rtl_bt)
and redistributed under
[LICENCE.rtlwifi_firmware.txt](LICENCE.rtlwifi_firmware.txt).

`scripts/gen_rtl_fw.py` embeds the listed blobs into `src/rtl_fw_blobs.h`.
On start, HearBridge writes any missing or outdated copies to
`/data/hearbridge/firmware/` on the console.

Currently shipped:

- `rtl8761bu_fw.bin`
- `rtl8761bu_config.bin`

These cover the RTL8761BU sticks tested with HearBridge (TP-Link UB500/UB600,
USB id `0BDA:B00C` / `0BDA:8771`).
