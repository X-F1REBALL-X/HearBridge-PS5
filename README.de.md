<div align="center">

# HearBridge PS5

**Bluetooth-Kopfhörer für eine gejailbreakte PS5: Spiel- und Systemton, ohne Dongle.**

Entwickelt von **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 **Deutsch** · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md) · 🇮🇱 [עברית](README.he.md)

</div>

HearBridge PS5 ist ein Payload (ELF) für eine gejailbreakte PS5. Er überträgt den Ton der Konsole über das eigene Bluetooth der PS5 an normale Bluetooth-Kopfhörer oder -Lautsprecher (A2DP). Gesteuert wird er über eine Webseite, die die Konsole bereitstellt. Spiele, Firmware und Jailbreak bleiben unberührt.

<p align="center"><img src="docs/img/ui-en.png" alt="Webseite von HearBridge PS5" width="900"></p>

## Voraussetzungen

- Eine gejailbreakte PS5 mit ELF-Loader auf Port **9021** (elfldr).
- Bluetooth-Kopfhörer oder -Lautsprecher mit A2DP (SBC, 48 kHz Stereo).
- Ein Browser im selben Netzwerk (PS5, Handy oder PC).

## Installieren und starten

1. Lade **HearBridge-PS5-1.1.0.elf** aus dem [neuesten Release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest) herunter.
2. Schicke es an den Loader: `socat -u FILE:HearBridge-PS5-1.1.0.elf TCP:<console-ip>:9021`
3. Öffne **http://&lt;console-ip&gt;:8090** oder die **HearBridge**-Kachel auf dem Startbildschirm.

Erneutes Senden des ELF ersetzt die laufende Kopie. **Stop HearBridge** auf der Seite beendet es.

## Welchen Chip habe ich?

PS5-Konsolen nutzen einen von zwei Bluetooth-Chips: Marvell/NXP oder MediaTek. Fat- (CFI-11xx/12xx) und Slim-Konsolen (CFI-20xx/21xx) können jeden der beiden haben, auch bei gleicher Modellnummer.

| Modell | Download |
|---|---|
| CFI-10xx (Launch) | [1.1.0](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0) |
| CFI-11xx, CFI-12xx (Fat) | Chip prüfen: Marvell/NXP → [1.1.0](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0), MediaTek → [mediatek test](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0-mtk-test) |
| CFI-20xx, CFI-21xx (Slim) | Chip prüfen: Marvell/NXP → [1.1.0](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0), MediaTek → [mediatek test](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0-mtk-test) |
| CFI-70xx, CFI-71xx (Pro, immer MediaTek) | [mediatek test](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0-mtk-test) |

Der mediatek test Build ist noch ungetestet. 1.1.0 ist nur auf einer CFI-10xx getestet. Sag Bescheid, wie es läuft.

**So prüfst du es:** 1.1.0 starten und unten im **Status**-Bereich die Zeile **Chip** ansehen. `Marvell/NXP (1286:…)` → bei 1.1.0 bleiben. `MediaTek (0e8d:…)` → mediatek test nehmen (die Seite zeigt dann auch einen orangen Hinweis). Oder `/data/hearbridge/hearbridge.log` öffnen und `usb: /dev/ugen0.2 is 1286:2059 …` suchen (die ugen-Nummer kann abweichen). Die ersten vier Zeichen nach "is" sind der Chip: `1286` = Marvell/NXP, `0e8d` = MediaTek.

Quellen: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights PS5 Pro teardown](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## Funktionen

- **Koppeln:** Kopfhörer in den Kopplungsmodus versetzen, **Scan for devices** drücken (20 s), dann **Connect**.
- **Auto-Verbindung:** gespeicherte Kopfhörer verbinden sich von selbst, wenn du sie einschaltest oder aus dem Case nimmst. Ein anderes gespeichertes Paar herausnehmen wechselt zu diesem. Nach manuellem **Disconnect** warten sie auf **Connect**.
- **Disconnect / Forget:** Disconnect trennt die Verbindung, die Kopfhörer bleiben gespeichert. Forget trennt und löscht sie.
- **Lautstärke:** Boost (Software-Verstärkung bis 500 %, Start bei 250 %) und Kopfhörerlautstärke (AVRCP, Start bei 50 %). Pro Kopfhörer gespeichert, sobald du sie änderst. **Mute** und **Test tone** zum Prüfen.
- **Equalizer:** 5 Bänder (±12 dB) mit Presets, pro Kopfhörer gespeichert, mit Limiter, damit der Boost nicht übersteuert.
- **Latenz:** Pufferziel von 60 bis 200 ms (Standard 200 ms) und eine Live-Schätzung der Verzögerung.
- **Clean sound:** Equalizer aus, Boost zurück auf 250 %, Puffer zurück auf 200 ms.
- **Log** auf der Seite mit jedem Schritt, farbig: grün geklappt, rot fehlgeschlagen, blau für deine Klicks.

## Codecs

- **Auto:** einfaches SBC, funktioniert mit allem.
- **SBC HQ** und **SBC-XQ:** manuell wählbar, nur wenn die Kopfhörer sie unterstützen. Nicht unterstützte sind ausgegraut.

AAC, aptX und LDAC werden nicht unterstützt.

## Bekannte Grenzen

- Ein Kopfhörer gleichzeitig, kein Mikrofon. Der Fernseher spielt den Ton weiter.
- Immer nur einen Bluetooth-Payload laufen lassen. Der DualSense funktioniert weiter.
- Nur auf fw 10.20 (PS5 fat, CFI-10xx) getestet. Andere Modelle und Firmwares sind ungetestet.
- Getestete Kopfhörer: Sony WF-1000XM6, OnePlus Buds Ace 2, Xbox Wireless Headset.

## Fehlerbehebung / Problem melden

Beim Start des Payloads sollte die Meldung **"HearBridge &lt;version&gt;: starting"** erscheinen. Wenn nicht, hat der Loader ihn nicht gestartet.

Einstellungen, gespeicherte Kopfhörer und das Log liegen in `/data/hearbridge/`. So meldest du ein Problem:

1. Lade `/data/hearbridge/hearbridge.log` und `diag.txt` per FTP herunter (z. B. [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv), Port 2121). `diag.txt` gibt es auch unter http://&lt;console-ip&gt;:8090/api/diag.
2. Öffne ein [GitHub-Issue](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) mit Konsolenmodell, Firmware, Loader, HearBridge-Version und den Logdateien.

## Bauen

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
make send PS5_HOST=<console-ip>
```

## Lizenz

GPL-3.0-or-later. Siehe [LICENSE](LICENSE) und [NOTICE](NOTICE).
