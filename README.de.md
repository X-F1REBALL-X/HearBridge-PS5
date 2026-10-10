<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**Bluetooth-Kopfhörer für eine gejailbreakte PS5: Spiel- und Systemton, ohne Dongle.**

Entwickelt von **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 **Deutsch** · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 ist ein Payload (ELF), der den Ton der Konsole über das eigene Bluetooth der PS5 an normale Bluetooth-Kopfhörer oder Lautsprecher überträgt. Gesteuert wird er über eine Webseite, die die Konsole bereitstellt. Spiele, Firmware und Jailbreak bleiben unberührt.

<p align="center"><img src="docs/img/ui-tv.png" alt="Webseite von HearBridge PS5" width="900"></p>

## Voraussetzungen

- Eine gejailbreakte PS5 mit einem ELF-Loader auf Port **9021** (elfldr).
- Bluetooth-Kopfhörer oder ein Lautsprecher mit A2DP (SBC, 48 kHz Stereo).
- Ein Browser im selben Netzwerk (PS5, Handy oder PC).

## Installieren und starten

1. Lade **HearBridge-PS5-1.3.0.elf** aus dem [neuesten Release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest) herunter.
2. Schick die Datei an den Loader: `socat -u FILE:HearBridge-PS5-1.3.0.elf TCP:<console-ip>:9021`
3. Öffne **http://&lt;console-ip&gt;:8090** oder die Kachel **HearBridge** auf dem Startbildschirm.
4. Öffne **Einstellungen**, dann **Kopfhörer**. Versetze die Kopfhörer in den Kopplungsmodus, drücke **Nach Geräten suchen** (etwa 12 s) und dann **Verbinden**.

Wenn du die Seite schließt, läuft HearBridge weiter. Erneutes Senden der ELF ersetzt die laufende Kopie, **HearBridge beenden** beendet sie.

## Die Seite

- **Kopfhörer:** Akkustand in Prozent (über die Freisprechverbindung), Signal und Verbindungsqualität. **Kopfhörer wechseln** wechselt mit einem Tipp zu anderen gespeicherten Kopfhörern.
- **Klang:** 5-Band-Equalizer mit Presets, Verstärkung bis 500 %, Kopfhörer-Lautstärke, die den Tasten der Kopfhörer folgt, **Stumm** und **Nachtmodus** (leisere Explosionen, klarere Stimmen).
- **Verbindung:** Codec und ein Latenzregler von 40 bis 200 ms mit Live-Schätzung der Verzögerung.
- **Protokoll:** jeder Schritt in Farbe: grün hat geklappt, rot fehlgeschlagen, blau für deine Eingaben.

**Einstellungen** öffnet ein Seitenmenü:

- **Startseite:** zurück zur Hauptseite.
- **Kopfhörer:** suchen, verbinden, trennen und vergessen.
- **Klang und Spiele:** Testton, Klarer Klang, **Weiter/Zurück am Ohrhörer ändert die Lautstärke** (für Ohrhörer ohne Lautstärketasten) und deine gespeicherten Spiele.
- **Details:** Format, Akku, AVRCP, Aufschlüsselung der Latenz und der Bluetooth-Chip.
- **Sichern & wiederherstellen:** gespeicherte Kopfhörer samt Kopplung, Einstellungen und Spielprofile in einer Datei, auf einem USB-Stick, der Konsole oder dem Gerät, mit dem du surfst.

### Spielprofile

Starte ein Spiel, stell den Klang ein und speichere ihn. Profile gelten pro Spiel und pro Kopfhörer, schalten sich beim Spielstart von selbst ein und geben beim Beenden deinen normalen Klang zurück. **Spielprofil aktualisieren** im Bereich Klang speichert Änderungen.

### Automatisch verbinden und Ruhemodus

Gespeicherte Kopfhörer verbinden sich von selbst, wenn du sie einschaltest oder aus dem Etui nimmst. Vor dem Ruhemodus trennt HearBridge die Kopfhörer sauber und verbindet sie nach dem Aufwachen wieder.

### Codecs

**Auto** ist normales SBC und funktioniert mit allem. **SBC HQ** und **SBC-XQ** lassen sich von Hand wählen, wenn die Kopfhörer sie unterstützen. AAC, aptX und LDAC werden nicht unterstützt.

## Welchen Chip habe ich?

Eine Datei funktioniert mit beiden Bluetooth-Chips. HearBridge erkennt den Chip selbst und zeigt ihn unter **Details**.

| Modell | Bluetooth-Chip |
|---|---|
| CFI-10xx (Launch) | Marvell/NXP |
| CFI-11xx, CFI-12xx (Fat) | Marvell/NXP oder MediaTek |
| CFI-20xx, CFI-21xx (Slim) | Marvell/NXP oder MediaTek |
| CFI-70xx, CFI-71xx (Pro) | MediaTek |

Getestet auf PS5 CFI-10xx (Marvell/NXP) und PS5 Slim CFI-2008 (MediaTek). Berichte von anderen Modellen sind willkommen.

Quellen: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights PS5 Pro Teardown](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## Bekannte Grenzen

- Immer nur ein Kopfhörer, kein Mikrofon. Der Fernseher spielt den Ton weiter ab.
- Immer nur einen Bluetooth-Payload gleichzeitig ausführen. Der DualSense funktioniert weiter.
- Getestete Kopfhörer: Sony WF-1000XM6, OnePlus Buds Ace 2, Xbox Wireless Headset.

## Ein Problem melden

Wenn der Payload läuft, siehst du die Benachrichtigung **"HearBridge &lt;version&gt;: starting"**. Wenn nicht, hat der Loader ihn nicht gestartet.

1. Hol dir `/data/hearbridge/hearbridge.log` und `diag.txt` per FTP (zum Beispiel [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv), Port 2121). `diag.txt` gibt es auch unter http://&lt;console-ip&gt;:8090/api/diag.
2. Eröffne ein [GitHub-Issue](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) mit Konsolenmodell, Firmware, Loader, HearBridge-Version und den Logdateien.

## Bauen

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test     # host tests: cc, ffmpeg, python3 + numpy (node optional)
make send PS5_HOST=<console-ip>
```

## Danksagung

MediaTek Scan-Pause und USB-Pipe-Fix sowie HCI-Debug-Tool von [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## Lizenz

GPL-3.0-or-later. Siehe [LICENSE](LICENSE) und [NOTICE](NOTICE).
