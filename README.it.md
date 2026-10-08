<div align="center">

# HearBridge PS5

**Cuffie Bluetooth per una PS5 con jailbreak: audio dei giochi e del sistema, senza dongle.**

Sviluppato da **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 **Italiano** · 🇮🇱 [עברית](README.he.md)

</div>

HearBridge PS5 è un payload (ELF) per una PS5 con jailbreak. Cattura l'audio della console e lo trasmette tramite la radio Bluetooth della console stessa a normali cuffie o altoparlanti Bluetooth (A2DP, SBC). Si controlla da una pagina web servita dalla console. Non modifica i giochi, il firmware né il jailbreak.

<p align="center"><img src="docs/img/ui-en.png" alt="Pagina web di HearBridge PS5" width="520"></p>

## Requisiti

- Una PS5 con jailbreak e un loader ELF in ascolto sulla porta **9021** (elfldr).
- Cuffie o un altoparlante Bluetooth con **A2DP** (SBC).
- Il browser della PS5, un telefono o un PC sulla stessa rete.

## Installazione e avvio

1. Scarica **HearBridge-PS5-1.0.1.elf** dalla [release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.1).
2. Invialo al loader della console, ad esempio:
   `socat -u FILE:HearBridge-PS5-1.0.1.elf TCP:<console-ip>:9021`
3. Apri **http://&lt;console-ip&gt;:8090** (o il riquadro **HearBridge** aggiunto alla schermata iniziale al primo avvio).

## Uso

- **Abbinare:** metti le cuffie in modalità di abbinamento, premi **Cerca dispositivi** (circa 20 s), poi **Connetti** accanto alle cuffie. Vengono salvate e l'audio parte.
- **Connetti:** i dispositivi salvati si connettono solo quando premi **Connetti** sulla loro riga. Niente si connette in background.
- **Disconnetti:** chiude il collegamento; il dispositivo resta salvato. Se le cuffie tornano nella custodia o escono dal raggio, la pagina mostra **Non connesso** finché non premi di nuovo Connetti.
- **Dimentica:** rimuove il dispositivo e la sua chiave di abbinamento.
- **Volume:** il cursore di amplificazione (guadagno software, fino al 500 %) e il volume delle cuffie (volume assoluto AVRCP, segue anche i tasti delle cuffie). **Muto** e **Tono di prova** aiutano nei controlli.
- **Arresta HearBridge** chiude il payload in modo pulito. Usalo prima di ricaricare l'ELF.

La pagina è disponibile in 11 lingue, tra cui ebraico e arabo (da destra a sinistra).

## Note

- Usa il Bluetooth integrato della PS5; il DualSense continua a funzionare. Esegui un solo payload che usa il Bluetooth alla volta.
- Solo codec SBC, un dispositivo alla volta, nessun microfono. Anche la TV continua a riprodurre l'audio.
- Testato con Sony WF-1000XM6, OnePlus Buds Ace 2 e Xbox Wireless Headset.
- Impostazioni, dispositivi salvati e log si trovano in `/data/hearbridge/` (`hearbridge.log` registra ogni passaggio).

## Risoluzione dei problemi / segnalare un problema

**Testato su:** HearBridge è stato sviluppato e testato su una PS5 fat (modello originale, CFI-10xx) con firmware 10.20. Altri modelli (Slim, Pro, revisioni fat successive) e altri firmware non sono testati e potrebbero usare un chip Bluetooth diverso, quindi le segnalazioni da questi sono benvenute.

Problemi comuni:

- **Nessuna icona di HearBridge nella schermata iniziale** dopo aver avviato l'ELF.
- **La pagina non si apre** (http://&lt;console-ip&gt;:8090).
- **Le cuffie non vengono trovate** quando premi Cerca dispositivi.
- **Nessun suono** anche se le cuffie sono connesse.

**Firmware 13.60:** prova **HearBridge-PS5-1.0.1-fw13.60.elf** dalla [release v1.0.1](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.1). È una build sperimentale pensata per correggere l'icona mancante nella schermata iniziale e aggiunge la diagnostica. Non è ancora stata testata su una console reale.

**Ottenere il log:**

1. Invia un payload server FTP (ad esempio [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv)) con lo stesso loader che usi per HearBridge.
2. Collegati con FileZilla all'IP della console sulla porta indicata dal server (ftpsrv di solito usa la **2121**).
3. Scarica `/data/hearbridge/hearbridge.log` e, con la build fw13.60, anche `/data/hearbridge/diag.txt`.

Con la build fw13.60 puoi anche aprire **http://&lt;console-ip&gt;:8090/api/diag** e copiare il testo.

**Apri una [issue su GitHub](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose)** indicando:

- il modello della console (numero CFI, ad es. CFI-1016A)
- la versione del firmware
- il loader usato
- se è apparsa la notifica **"HearBridge 1.0.1: http://…"**
- se la pagina si apre
- i file di log (`hearbridge.log`, `diag.txt`)

## Compilazione

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make ps5
make test
```

## Licenza

GPL-3.0-or-later. Vedi [LICENSE](LICENSE) e [NOTICE](NOTICE).

---

<p align="center">Developed by X-F1REBALL-X</p>
