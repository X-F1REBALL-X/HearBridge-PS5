<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**Bluetooth headphones for a jailbroken PS5: game and system audio, no dongle.**

Developed by **X-F1REBALL-X**

🇺🇸 **English** · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 is a payload (ELF) that plays your PS5's game and system audio on regular Bluetooth headphones, using the console's own Bluetooth. You control it from a web page served by the console.

<p align="center"><img src="docs/img/ui-tv.png" alt="HearBridge PS5 web page" width="440"> <img src="docs/img/ui-settings.png" alt="HearBridge PS5 web page" width="440"></p>

## Highlights

- One file for every PS5, Marvell/NXP or MediaTek Bluetooth.
- Game profiles: your sound follows the game and the headset.
- Battery percent, night mode and one-tap headset switching.

## Install

1. Download **HearBridge-PS5-1.3.0.elf** from the [latest release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Send it to your ELF loader on port 9021.
3. Open **http://&lt;console-ip&gt;:8090** or the **HearBridge** tile, then pair your headphones in **Settings**.

Everything else, from features to troubleshooting, is in the **[guide](https://x-f1reball-x.github.io/HearBridge-PS5/)**.

## Build

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
```

## Credits

MediaTek scan-pause and USB pipe fix, plus HCI debug tool, by [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
