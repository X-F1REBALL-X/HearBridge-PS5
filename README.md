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

1. Download **HearBridge-PS5-1.0.1.elf** from the [release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.1).
2. Send it to the console's loader, for example:
   `socat -u FILE:HearBridge-PS5-1.0.1.elf TCP:<console-ip>:9021`
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
