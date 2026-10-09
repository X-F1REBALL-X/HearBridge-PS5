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
   If port 8090 is already taken, HearBridge uses the next free port from **8091 to 8095**; the notification and the log show the address it really uses. (The home-screen tile opens port 8090 unless `tile_url` says otherwise.)

**Loaders:** HearBridge was developed with **elfldr** listening on port 9021 (firmware 10.20). Reported with other setups so far, both without success: Relapse on 13.60 ([#1](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/1)) and Payload Manager 0.5.2 with kstuff 1.13 and ShadowMount 1.7beta4 on 13.20 ([#2](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/2)). Other loaders are untested.

## Usage

- **Pair:** put the headphones in pairing mode, press **Scan for devices** (about 20 s), then **Connect** next to them. They are saved and audio starts.
- **Connect:** saved devices connect only when you press **Connect** on their row. Nothing connects in the background.
- **Disconnect:** drops the link; the device stays saved. If the headphones go back in their case or out of range, the page shows **Not connected** until you press Connect again.
- **Forget:** removes the device and its pairing key.
- **Volume:** the boost slider (software gain, up to 500 %) and the headset volume (AVRCP absolute volume, also follows the headphones' own buttons). **Mute** and **Test tone** help with checks.
- **Low latency / Stable** (builds after 1.0.2): how much audio HearBridge may queue when the radio falls behind. See *Latency* below.
- **Stop HearBridge** ends the payload cleanly. Use it before loading the ELF again.

The page is available in 11 languages, including Hebrew and Arabic (right to left).

## Notes

- Uses the PS5's built-in Bluetooth; the DualSense keeps working. Run only one payload that uses Bluetooth at a time.
- SBC codec only, one device at a time, no microphone. The TV keeps playing sound too.
- The console audio is captured at 48 kHz stereo and there is no resampler or downmix, so the headphones must accept **SBC at 48 kHz in a stereo mode**. Builds after 1.0.2 refuse other devices with a clear message ("device needs 48 kHz stereo") instead of playing distorted sound. A2DP requires headphones to support 48 kHz stereo, so this should be rare.
- Tested with Sony WF-1000XM6, OnePlus Buds Ace 2 and Xbox Wireless Headset.
- Settings, saved devices and the log are in `/data/hearbridge/` (`hearbridge.log` records every step).

### Latency

What HearBridge itself adds (the headphones add their own buffering on top, which depends on the model; the total delay has not been measured on a console):

- Each media packet carries about 13 to 40 ms of audio, depending on the bitpool and the headphones' packet size.
- The encoder runs at most about 40 ms ahead of real time.
- When the radio falls behind, packets queue up. **Low latency** (default in builds after 1.0.2) keeps at most about **200 ms** queued and drops the oldest audio beyond that. **Stable** allows about **1 s** (the 1.0.x behaviour): fewer dropouts on a busy radio, more delay. The SBC bitpool also steps down automatically while the radio is behind and back up (to the headphones' maximum, usually 53) when it is calm.

### Files in `/data/hearbridge/`

| File | What it is |
|---|---|
| `hearbridge.log` | Log of every step (attach it to bug reports) |
| `diag.txt` | Diagnostics report (firmware, model, icon install steps, USB/Bluetooth, audio libraries); also at `/api/diag` |
| `headset.ini`, `paired.ini` | Current and saved headphones with their pairing keys |
| `saved.json`, `devices.json`, `status.txt`, `select.txt` | Saved list, scan results, status and commands between the page and the stream loop |
| `gain` | Boost level (written by the page) |
| `latency` | `low` or `stable` (written by the page, builds after 1.0.2) |
| `hearbridge.lock` | Single-instance lock |
| `stop` | Create it to stop HearBridge (same as **Stop HearBridge**) |
| `tone` | Create it to play the 1 kHz test tone |
| `no_tile` | Create it to never add the home-screen icon |
| `remove_tile` | Create it to remove the icon once on the next start |
| `tile_url` | Optional address the icon opens (`start` = built-in fallback page) |
| `media_dump` | Debug, builds after 1.0.2: create it to write the first 200 media packets to `media_dump.bin` |
| `hci_debug` | Debug, builds after 1.0.2: create it to get an HCI trace at `/api/hcilog` and raw HCI commands at `/api/hci` (POST, token) |

## Troubleshooting / reporting a problem

**Tested on:** HearBridge was developed and tested on a PS5 fat (original model, CFI-10xx) running firmware 10.20. Other models (Slim, Pro, later fat revisions) and other firmwares are untested and may use a different Bluetooth chip, so reports from them are welcome.

**Tested firmware / models** (from the developer's testing, GitHub issues and the release notes; everything else is untested):

| Console | Firmware | Loader / jailbreak | HearBridge file | Result | Source |
|---|---|---|---|---|---|
| PS5 fat, original model (CFI-10xx) | 10.20 | elfldr (port 9021) | 1.0.0, 1.0.1 | Works (development console) | README, release notes |
| PS5 fat | 13.60 | Relapse | 1.0.x and the experimental fw13.60 build | Does not start: no icon, page on port 8090 does not open | [#1](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/1) |
| PS5 Pro (CFI-7000) | 13.20 | Payload Manager 0.5.2, kstuff 1.13, ShadowMount 1.7beta4 | 1.0.2 | Does not start: no notification, no page, no icon | [#2](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/2) |
| PS5 Slim, other fat revisions, other firmwares | – | – | – | Untested | – |

1.0.2 itself has not been tested on a real console yet. Headphones tested on the development console: Sony WF-1000XM6, OnePlus Buds Ace 2, Xbox Wireless Headset.

Builds after 1.0.2 show a **"HearBridge &lt;version&gt;: starting"** notification first thing when the payload runs. If you do not see it, the loader did not run HearBridge at all; if you see it but nothing else, the log and `diag.txt` say where it stopped.

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

Get the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk/releases) release and its prerequisites (clang and lld, version 15 or newer), then:

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make ps5      # dist/HearBridge-PS5-<version>.elf
make test     # host tests: cc, ffmpeg, python3 + numpy (node optional)
make send PS5_HOST=<console-ip>   # send it to elfldr on port 9021
```

There is one ELF for all firmwares (the separate fw13.60 build of 1.0.2 was merged into it). GitHub Actions runs `make test` and the console build on every pull request.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

---

<p align="center">Developed by X-F1REBALL-X</p>
