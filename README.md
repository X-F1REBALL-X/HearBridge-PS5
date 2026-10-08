<div align="center">

# HearBridge PS5

**Bluetooth headphones for a jailbroken PS5: game and system audio, no dongle.**

Developed by **X-F1REBALL-X**

🇺🇸 **English** · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md) · 🇮🇱 [עברית](README.he.md)

</div>

HearBridge PS5 is a payload (ELF) for a jailbroken PS5. It captures the console's audio and streams it over the console's own Bluetooth radio to ordinary Bluetooth headphones or speakers (A2DP, SBC). You control it from a web page served by the console. It does not modify games, the firmware or the jailbreak.

<p align="center"><img src="docs/img/ui-en.png" alt="HearBridge PS5 web page" width="520"></p>

## Requirements

- A jailbroken PS5 with an ELF loader listening on port **9021** (elfldr).
- Bluetooth headphones or a speaker with **A2DP** (SBC).
- The PS5 browser, a phone or a PC on the same network.

## Install and run

1. Download **HearBridge-PS5-1.0.2.elf** from the [release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.2).
2. Send it to the console's loader, for example:
   `socat -u FILE:HearBridge-PS5-1.0.2.elf TCP:<console-ip>:9021`
3. Open **http://&lt;console-ip&gt;:8090** (or the **HearBridge** tile that is added to the home screen on first run).

## Usage

- **Pair:** put the headphones in pairing mode, press **Scan for devices** (about 20 s), then **Connect** next to them. They are saved and audio starts.
- **Connect:** saved devices connect only when you press **Connect** on their row. Nothing connects in the background.
- **Disconnect:** drops the link; the device stays saved. If the headphones go back in their case or out of range, the page shows **Not connected** until you press Connect again.
- **Forget:** removes the device and its pairing key.
- **Volume:** the boost slider (software gain, up to 500 %) and the headset volume (AVRCP absolute volume, also follows the headphones' own buttons). **Mute** and **Test tone** help with checks.
- **Stop HearBridge** ends the payload cleanly. Use it before loading the ELF again.

The page is available in 11 languages, including Hebrew and Arabic (right to left).

## Notes

- Uses the PS5's built-in Bluetooth; the DualSense keeps working. Run only one payload that uses Bluetooth at a time.
- SBC codec only, one device at a time, no microphone. The TV keeps playing sound too.
- Tested with Sony WF-1000XM6, OnePlus Buds Ace 2 and Xbox Wireless Headset.
- Settings, saved devices and the log are in `/data/hearbridge/` (`hearbridge.log` records every step).

## Troubleshooting / reporting a problem

**Tested on:** HearBridge was developed and tested on a PS5 fat (original model, CFI-10xx) running firmware 10.20. Other models (Slim, Pro, later fat revisions) and other firmwares are untested and may use a different Bluetooth chip, so reports from them are welcome.

Common problems:

- **No HearBridge icon on the home screen** after running the ELF.
- **The page does not open** (http://&lt;console-ip&gt;:8090).
- **Headphones are not found** when you press Scan.
- **No sound** although the headphones are connected.

**Firmware 13.60:** try **HearBridge-PS5-1.0.2-fw13.60.elf** from the [v1.0.2 release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.2). It is an experimental build meant to fix the missing home-screen icon and adds diagnostics. It has not been tested on a real console yet.

**Get the log:**

1. Send an FTP server payload (for example [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv)) with the same loader you use for HearBridge.
2. Connect with FileZilla to the console's IP on the port the server shows (ftpsrv usually uses **2121**).
3. Download `/data/hearbridge/hearbridge.log` and, with the fw13.60 build, `/data/hearbridge/diag.txt`.

With the fw13.60 build you can also open **http://&lt;console-ip&gt;:8090/api/diag** and copy the text.

**Open a [GitHub issue](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose)** and include:

- the console model (CFI number, e.g. CFI-1016A)
- the firmware version
- the loader you used
- whether the **"HearBridge 1.0.2: http://…"** notification appeared
- whether the page opens
- the log files (`hearbridge.log`, `diag.txt`)

## Build

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make ps5
make test
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

---

<p align="center">Developed by X-F1REBALL-X</p>
