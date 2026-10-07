<div align="center">

# HearBridge PS5

**Bluetooth-Kopfhörer für eine gejailbreakte PS5: Spiel- und Systemton, ohne Dongle.**

Entwickelt von **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 **Deutsch** · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md) · 🇮🇱 [עברית](README.he.md)

</div>

HearBridge PS5 ist ein Payload (ELF) für eine gejailbreakte PS5. Er nimmt den Ton der Konsole auf und überträgt ihn über das konsoleneigene Bluetooth-Funkmodul an normale Bluetooth-Kopfhörer oder -Lautsprecher (A2DP, SBC). Gesteuert wird er über eine Webseite, die die Konsole bereitstellt. Spiele, Firmware und Jailbreak bleiben unverändert.

<p align="center"><img src="docs/img/ui-en.png" alt="Webseite von HearBridge PS5" width="520"></p>

## Voraussetzungen

- Eine gejailbreakte PS5 mit einem ELF-Loader auf Port **9021** (elfldr).
- Bluetooth-Kopfhörer oder ein Lautsprecher mit **A2DP** (SBC).
- Der PS5-Browser, ein Handy oder ein PC im selben Netzwerk.

## Installation und Start

1. Lade **HearBridge-PS5-1.0.0.elf** aus dem [Release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.0) herunter.
2. Sende sie an den Loader der Konsole, zum Beispiel:
   `socat -u FILE:HearBridge-PS5-1.0.0.elf TCP:<console-ip>:9021`
3. Öffne **http://&lt;console-ip&gt;:8090** (oder die Kachel **HearBridge**, die beim ersten Start zum Startbildschirm hinzugefügt wird).

## Bedienung

- **Koppeln:** Versetze die Kopfhörer in den Kopplungsmodus, drücke **Nach Geräten suchen** (etwa 20 s) und dann **Verbinden** daneben. Sie werden gespeichert und der Ton startet.
- **Verbinden:** Gespeicherte Geräte verbinden sich nur, wenn du in ihrer Zeile **Verbinden** drückst. Im Hintergrund verbindet sich nichts.
- **Trennen:** beendet die Verbindung; das Gerät bleibt gespeichert. Kommen die Kopfhörer zurück ins Etui oder aus der Reichweite, zeigt die Seite **Nicht verbunden**, bis du erneut Verbinden drückst.
- **Entfernen:** löscht das Gerät samt Kopplungsschlüssel.
- **Lautstärke:** der Verstärkungsregler (Software-Verstärkung bis 500 %) und die Kopfhörer-Lautstärke (AVRCP absolute Lautstärke, folgt auch den Tasten der Kopfhörer). **Stumm** und **Testton** helfen beim Prüfen.
- **HearBridge beenden** beendet den Payload sauber. Vor dem erneuten Laden der ELF verwenden.

Die Seite gibt es in 11 Sprachen, darunter Hebräisch und Arabisch (von rechts nach links).

## Hinweise

- Nutzt das eingebaute Bluetooth der PS5; der DualSense funktioniert weiter. Immer nur einen Payload mit Bluetooth gleichzeitig ausführen.
- Nur SBC-Codec, ein Gerät zur Zeit, kein Mikrofon. Der Fernseher gibt den Ton weiterhin aus.
- Getestet mit Sony WF-1000XM6, OnePlus Buds Ace 2 und Xbox Wireless Headset.
- Einstellungen, gespeicherte Geräte und das Log liegen in `/data/hearbridge/` (`hearbridge.log` protokolliert jeden Schritt).

## Bauen

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make ps5
make test
```

## Lizenz

GPL-3.0-or-later. Siehe [LICENSE](LICENSE) und [NOTICE](NOTICE).

---

<p align="center">Developed by X-F1REBALL-X</p>
