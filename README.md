<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**Bluetooth headphones for a jailbroken PS5: game and system audio, no dongle.**

Developed by **X-F1REBALL-X**

🇺🇸 **English** · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 is a payload (ELF) that streams the console's audio over the PS5's own Bluetooth to regular Bluetooth headphones or speakers. You control it from a web page served by the console. It doesn't touch games, the firmware or the jailbreak.

<p align="center"><img src="docs/img/ui-tv.png" alt="HearBridge PS5 web page" width="900"></p>

## Requirements

- A jailbroken PS5 with an ELF loader on port **9021** (elfldr).
- Bluetooth headphones or a speaker with A2DP (SBC, 48 kHz stereo).
- A browser on the same network (PS5, phone or PC).

## Install and run

1. Download **HearBridge-PS5-1.3.0.elf** from the [latest release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Send it to the loader: `socat -u FILE:HearBridge-PS5-1.3.0.elf TCP:<console-ip>:9021`
3. Open **http://&lt;console-ip&gt;:8090**, or the **HearBridge** tile on the home screen.
4. Open **Settings**, then **Headsets**. Put the headphones in pairing mode, press **Scan for devices** (about 12 s), then **Connect**.

Closing the page leaves HearBridge running. Sending the ELF again replaces the running copy, and **Stop HearBridge** ends it.

## The page

- **Headset:** battery percent (read over the hands-free link), signal and link quality. **Switch headset** jumps to another saved headset with one tap.
- **Sound:** 5 band equalizer with presets, boost up to 500 %, headset volume that follows the headset's buttons, **Mute** and **Night mode** (quieter explosions, clearer voices).
- **Connection:** codec and a latency slider from 40 to 200 ms with a live delay estimate.
- **Log:** every step in color: green worked, red failed, blue for your presses.

**Settings** opens a side menu:

- **Home:** back to the main page.
- **Headsets:** scan, connect, disconnect and forget.
- **Sound & games:** Test tone, Clean sound, **Earbud next/previous changes volume** (for earbuds without volume keys) and your saved games.
- **Details:** format, battery, AVRCP, latency breakdown and the Bluetooth chip.
- **Backup & restore:** saved headsets with their pairing, settings and game profiles in one file, on a USB drive, the console or the device you're browsing from.

### Game profiles

Start a game, set the sound you like and save it. Profiles are kept per game and per headset, turn on by themselves when the game starts and hand back your usual sound when it closes. **Update Game Profile** on the Sound panel saves your changes.

### Auto-connect and rest mode

Saved headsets connect on their own when you turn them on or take them out of the case. Before rest mode HearBridge stops the headset cleanly and reconnects it after wake.

### Codecs

**Auto** is plain SBC and works with everything. **SBC HQ** and **SBC-XQ** can be picked by hand when the headset supports them. AAC, aptX and LDAC are not supported.

## Which chip do I have?

One file works on both Bluetooth chips. HearBridge finds the chip by itself and shows it under **Details**.

| Model | Bluetooth chip |
|---|---|
| CFI-10xx (launch) | Marvell/NXP |
| CFI-11xx, CFI-12xx (fat) | Marvell/NXP or MediaTek |
| CFI-20xx, CFI-21xx (Slim) | Marvell/NXP or MediaTek |
| CFI-70xx, CFI-71xx (Pro) | MediaTek |

Tested on PS5 CFI-10xx (Marvell/NXP) and PS5 Slim CFI-2008 (MediaTek). Reports from other models are welcome.

Sources: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights PS5 Pro teardown](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## Known limits

- One headset at a time, no microphone. The TV keeps playing sound too.
- Run only one Bluetooth payload at a time. The DualSense keeps working.
- Headphones tested: Sony WF-1000XM6, OnePlus Buds Ace 2, Xbox Wireless Headset.

## Reporting a problem

When the payload runs you should see a **"HearBridge &lt;version&gt;: starting"** notification. If you don't, the loader didn't run it.

1. Get `/data/hearbridge/hearbridge.log` and `diag.txt` over FTP (for example [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv), port 2121). `diag.txt` is also at http://&lt;console-ip&gt;:8090/api/diag.
2. Open a [GitHub issue](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) with the console model, firmware, loader, HearBridge version and the log files.

## Build

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test     # host tests: cc, ffmpeg, python3 + numpy (node optional)
make send PS5_HOST=<console-ip>
```

## Credits

MediaTek scan-pause and USB pipe fix, plus HCI debug tool, by [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
