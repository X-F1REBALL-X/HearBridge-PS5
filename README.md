<div align="center">

# HearBridge PS5

**Bluetooth headphones for a jailbroken PS5: game and system audio, no dongle.**

Developed by **X-F1REBALL-X**

🇺🇸 **English** · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 is a payload (ELF) for a jailbroken PS5. It streams the console's audio over the PS5's own Bluetooth to regular Bluetooth headphones or speakers (A2DP). You control it from a web page served by the console. It doesn't touch games, the firmware or the jailbreak.

<p align="center"><img src="docs/img/ui-en.png" alt="HearBridge PS5 web page" width="900"></p>

## Requirements

- A jailbroken PS5 with an ELF loader on port **9021** (elfldr).
- Bluetooth headphones or a speaker with A2DP (SBC, 48 kHz stereo).
- A browser on the same network (PS5, phone or PC).

## Install and run

1. Download **HearBridge-PS5-1.2.0.elf** from the [latest release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Send it to the loader: `socat -u FILE:HearBridge-PS5-1.2.0.elf TCP:<console-ip>:9021`
3. Open **http://&lt;console-ip&gt;:8090**, or the **HearBridge** tile on the home screen.

Sending the ELF again replaces the running copy. **Stop HearBridge** on the page ends it.

## Which chip do I have?

One file works on both Bluetooth chips, Marvell/NXP and MediaTek. HearBridge finds the chip by itself and shows it in the **Chip** row of the **Status** panel.

| Model | Bluetooth chip |
|---|---|
| CFI-10xx (launch) | Marvell/NXP |
| CFI-11xx, CFI-12xx (fat) | Marvell/NXP or MediaTek |
| CFI-20xx, CFI-21xx (Slim) | Marvell/NXP or MediaTek |
| CFI-70xx, CFI-71xx (Pro) | MediaTek |

Tested on: PS5 fat CFI-10xx (Marvell/NXP), PS5 Slim CFI-2008 (MediaTek). Other models: please report how it goes.

In `/data/hearbridge/hearbridge.log` the line `usb: /dev/ugen0.2 is 1286:2059 …` names it too: `1286` = Marvell/NXP, `0e8d` = MediaTek.

Sources: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights PS5 Pro teardown](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## Features

- **Pair:** put the headphones in pairing mode, press **Scan for devices** (20 s), then **Connect**.
- **Auto-connect:** saved headphones connect on their own when you turn them on or take them out of the case. Taking out another saved pair switches to it. After a manual **Disconnect** they wait for **Connect**.
- **Disconnect / Forget:** Disconnect drops the link and keeps the headphones saved. Forget disconnects and removes them.
- **Volume:** boost (software gain up to 500 %, starts at 250 %) and headset volume (AVRCP, starts at 50 %). Saved per headset once you change them. **Mute** and **Test tone** for checks.
- **Equalizer:** 5 bands (±12 dB) with presets, saved per headset, with a limiter so boosts don't clip.
- **Latency:** buffer target from 60 to 200 ms (default 200 ms) and a live estimate of the delay.
- **Clean sound:** equalizer off, boost back to 250 %, buffer back to 200 ms.
- **Log** on the page with every step, in color: green worked, red failed, blue for your presses.

## Codecs

- **Auto:** plain SBC, works with everything.
- **SBC HQ** and **SBC-XQ:** pick them by hand, only when the headphones support them. Unsupported ones are greyed out.

AAC, aptX and LDAC are not supported.

## Known limits

- One headset at a time, no microphone. The TV keeps playing sound too.
- Run only one Bluetooth payload at a time. The DualSense keeps working.
- Headphones tested: Sony WF-1000XM6, OnePlus Buds Ace 2, Xbox Wireless Headset.

## Troubleshooting / reporting a problem

When the payload runs you should see a **"HearBridge &lt;version&gt;: starting"** notification. If you don't, the loader didn't run it.

Settings, saved headphones and the log are in `/data/hearbridge/`. To report a problem:

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
