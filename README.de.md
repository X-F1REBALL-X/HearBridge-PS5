<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**Bluetooth-Kopfhörer für eine gejailbreakte PS5: Spiel- und Systemton, ohne Dongle.**

Entwickelt von **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 **Deutsch** · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 ist ein Payload (ELF), der Spiel- und Systemton deiner PS5 über das eigene Bluetooth der Konsole auf normale Bluetooth-Kopfhörer bringt. Gesteuert wird er über eine Webseite der Konsole.

<p align="center"><img src="docs/img/ui-tv.png" alt="Webseite von HearBridge PS5" width="440"> <img src="docs/img/ui-settings.png" alt="Webseite von HearBridge PS5" width="440"></p>

## Highlights

- Eine Datei für jede PS5, mit Marvell/NXP- oder MediaTek-Bluetooth.
- Spielprofile: dein Klang folgt dem Spiel und dem Kopfhörer.
- Akkustand, Nachtmodus und Kopfhörerwechsel mit einem Tipp.

## Installieren

1. Lade **HearBridge-PS5-1.3.0.elf** aus dem [neuesten Release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest) herunter.
2. Schick die Datei an deinen ELF-Loader auf Port 9021.
3. Öffne **http://&lt;console-ip&gt;:8090** oder die Kachel **HearBridge** und kopple deine Kopfhörer unter **Einstellungen**.

Alles Weitere, von Funktionen bis Fehlerbehebung, steht in der **[Anleitung](https://x-f1reball-x.github.io/HearBridge-PS5/)**.

## Bauen

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
```

## Danksagung

MediaTek Scan-Pause und USB-Pipe-Fix sowie HCI-Debug-Tool von [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## Lizenz

GPL-3.0-or-later. Siehe [LICENSE](LICENSE) und [NOTICE](NOTICE).

[Das Projekt auf Ko-fi unterstützen](https://ko-fi.com/xf1reballx). Das Projekt bleibt kostenlos und Open Source.
