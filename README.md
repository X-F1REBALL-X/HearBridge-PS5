<div align="center">

<img src="docs/img/banner.png" alt="HearBridge PS5">

**Bluetooth headphones for a jailbroken PS5: game and system audio, no dongle.**

Developed by **X-F1REBALL-X**

🇺🇸 **English** · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

**HearBridge family:** [PS5](https://github.com/X-F1REBALL-X/HearBridge-PS5) · [PS4](https://github.com/X-F1REBALL-X/HearBridge-PS4) · [Browser](https://github.com/X-F1REBALL-X/HearBridge-Browser)


HearBridge PS5 is a payload (ELF) that plays your PS5's game and system audio on regular Bluetooth headphones, using the console's own Bluetooth. You control it from a web page served by the console.

<p align="center"><img src="docs/img/ui-tv.png" alt="HearBridge PS5 web page" width="440"> <img src="docs/img/ui-settings.png" alt="HearBridge PS5 web page" width="440"></p>

## Highlights

- One file for every PS5, Marvell/NXP or MediaTek Bluetooth.
- **USB Bluetooth adapters too**, including Realtek sticks that boot in ROM mode.
- Game profiles: your sound follows the game and the headset.
- Battery percent, night mode and one-tap headset switching.

## Install

1. Download **HearBridge-1.3.6.elf** from the [latest release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Send it to your ELF loader on port 9021.
3. Open **http://&lt;console-ip&gt;:8090** or the **HearBridge** tile, then pair your headphones in **Settings**.

Everything else, from features to troubleshooting, is in the **[guide](https://x-f1reball-x.github.io/HearBridge-PS5/)**.

## USB Bluetooth adapter

Plug in a USB Bluetooth adapter and HearBridge uses it automatically; without
one it uses the console's own chip. An adapter is not shared with the console's
gamepad, so the controller can connect and reconnect normally while audio is
streaming.

Pick the controller under **Settings > Details > Bluetooth adapter**:
**Auto** (USB stick when one is plugged in, else the console chip), **Console
chip**, **USB adapter**, or a listed ugen node when more than one stick is
present. A change is written to `/data/hearbridge/adapter` and Bluetooth is
restarted. You can still create that file by hand with one line:

```text
auto        # default: a USB adapter if one is present, else the console chip
internal    # always the console's own chip
usb         # always a USB adapter (start fails if there is none)
ugen1.2     # one specific ugen node
```

The same Details page shows which adapter is in use and its chip under **Chip**.

### Realtek ROM-only adapters

Some Realtek sticks (RTL8761BU/UB500/UB600 and other USB parts) start in a ROM
mode that answers Bluetooth commands but cannot scan until a patch is uploaded.
HearBridge uploads it the same way Linux's `btrtl` does, from files under
`/data/hearbridge/firmware/`.

For the RTL8761BU (UB500/UB600, USB id `0BDA:B00C` / `0BDA:8771`) the matching
files are **bundled in the ELF** and written there automatically on start:

```text
/data/hearbridge/firmware/rtl8761bu_fw.bin
/data/hearbridge/firmware/rtl8761bu_config.bin
```

No manual copy is needed for that stick. Other Realtek USB parts the loader
knows still need their files placed there by hand. Those files are in the
`linux-firmware` package (`/lib/firmware/rtl_bt/` on Linux, `firmware-realtek`
on Debian and Ubuntu) or in the
[`rtl_bt` tree of linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/rtl_bt).
The adapter name and the file it needs are in the log
(`/data/hearbridge/hearbridge.log`). Only the RTL8761BU (the UB600) has been
tested on hardware. The loader also knows the other USB chips in Linux's
`btrtl` table, but they are untested; in particular the secure-boot parts
(RTL8852AU/BU/CU, RTL8851BU, RTL8922AU, RTL8852BTU) take their patch from the
key-id security sections, a path that has not been run on a console yet.

Pairing keys belong to one Bluetooth controller. A headset paired with the
console's chip may have to be paired once more after switching to a USB adapter
(or the other way round).

## Build

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-<version>.elf
make test
```

## Credits

MediaTek scan-pause and USB pipe fix, plus HCI debug tool, by [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

USB Bluetooth adapter path and Realtek ROM uploader by [jamesst20](https://github.com/jamesst20) ([#45](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/45))

Bundled RTL8761BU firmware blobs are from the
[linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/rtl_bt)
`rtl_bt` tree (Realtek Semiconductor Corporation). See
[assets/firmware/LICENCE.rtlwifi_firmware.txt](assets/firmware/LICENCE.rtlwifi_firmware.txt).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

[Support the project on Ko-fi](https://ko-fi.com/xf1reballx). The project stays free and open source.
