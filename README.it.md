<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**Cuffie Bluetooth per una PS5 con jailbreak: audio di giochi e sistema, senza dongle.**

Sviluppato da **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 **Italiano**

</div>

HearBridge PS5 è un payload (ELF) che invia l'audio della console tramite il Bluetooth della PS5 a normali cuffie o altoparlanti Bluetooth. Lo controlli da una pagina web servita dalla console. Non tocca i giochi, il firmware o il jailbreak.

<p align="center"><img src="docs/img/ui-tv.png" alt="Pagina web di HearBridge PS5" width="900"></p>

## Requisiti

- Una PS5 con jailbreak e un loader di ELF sulla porta **9021** (elfldr).
- Cuffie o un altoparlante Bluetooth con A2DP (SBC, 48 kHz stereo).
- Un browser sulla stessa rete (PS5, telefono o PC).

## Installare e avviare

1. Scarica **HearBridge-PS5-1.3.0.elf** dall'[ultima release](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Invialo al loader: `socat -u FILE:HearBridge-PS5-1.3.0.elf TCP:<console-ip>:9021`
3. Apri **http://&lt;console-ip&gt;:8090** o il riquadro **HearBridge** nella schermata Home.
4. Apri **Impostazioni**, poi **Cuffie**. Metti le cuffie in modalità di associazione, premi **Cerca dispositivi** (circa 12 s), poi **Connetti**.

Chiudere la pagina lascia HearBridge in esecuzione. Inviare di nuovo l'ELF sostituisce la copia in esecuzione, e **Arresta HearBridge** la termina.

## La pagina

- **Cuffie:** batteria in percentuale (letta tramite il collegamento vivavoce), segnale e qualità del collegamento. **Cambia cuffie** passa ad altre cuffie salvate con un tocco.
- **Suono:** equalizzatore a 5 bande con preset, amplificazione fino al 500 %, volume delle cuffie che segue i loro tasti, **Muto** e **Modalità notte** (esplosioni più morbide, voci più chiare).
- **Connessione:** codec e un cursore di latenza da 40 a 200 ms con una stima del ritardo dal vivo.
- **Registro:** ogni passo a colori: verde riuscito, rosso fallito, blu per le tue azioni.

**Impostazioni** apre un menu laterale:

- **Home:** torna alla pagina principale.
- **Cuffie:** cerca, connetti, disconnetti e dimentica.
- **Suono e giochi:** Tono di prova, Suono pulito, **Avanti/indietro sull'auricolare cambia il volume** (per auricolari senza tasti del volume) e i tuoi giochi salvati.
- **Dettagli:** formato, batteria, AVRCP, ripartizione della latenza e chip Bluetooth.
- **Backup e ripristino:** cuffie salvate con la loro associazione, impostazioni e profili di gioco in un unico file, su una chiavetta USB, sulla console o sul dispositivo da cui navighi.

### Profili di gioco

Avvia un gioco, regola il suono che ti piace e salvalo. I profili sono per gioco e per cuffie, si attivano da soli quando il gioco parte e ti restituiscono il suono di sempre quando si chiude. **Aggiorna profilo gioco** nel pannello Suono salva le modifiche.

### Connessione automatica e modalità riposo

Le cuffie salvate si connettono da sole quando le accendi o le togli dalla custodia. Prima della modalità riposo HearBridge ferma le cuffie in modo pulito e le riconnette al risveglio.

### Codec

**Auto** è SBC normale e funziona con tutto. **SBC HQ** e **SBC-XQ** si scelgono a mano quando le cuffie li supportano. AAC, aptX e LDAC non sono supportati.

## Che chip ho?

Un solo file funziona con entrambi i chip Bluetooth. HearBridge riconosce il chip da solo e lo mostra in **Dettagli**.

| Modello | Chip Bluetooth |
|---|---|
| CFI-10xx (lancio) | Marvell/NXP |
| CFI-11xx, CFI-12xx (fat) | Marvell/NXP o MediaTek |
| CFI-20xx, CFI-21xx (Slim) | Marvell/NXP o MediaTek |
| CFI-70xx, CFI-71xx (Pro) | MediaTek |

Testato su PS5 CFI-10xx (Marvell/NXP) e PS5 Slim CFI-2008 (MediaTek). Le segnalazioni su altri modelli sono benvenute.

Fonti: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights smontaggio della PS5 Pro](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## Limiti noti

- Una cuffia alla volta, niente microfono. Anche la TV continua a suonare.
- Esegui un solo payload Bluetooth alla volta. Il DualSense continua a funzionare.
- Cuffie testate: Sony WF-1000XM6, OnePlus Buds Ace 2, Xbox Wireless Headset.

## Segnalare un problema

Quando il payload parte dovresti vedere la notifica **"HearBridge &lt;version&gt;: starting"**. Se non compare, il loader non l'ha avviato.

1. Scarica `/data/hearbridge/hearbridge.log` e `diag.txt` via FTP (ad esempio [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv), porta 2121). `diag.txt` è anche su http://&lt;console-ip&gt;:8090/api/diag.
2. Apri una [issue su GitHub](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) con modello della console, firmware, loader, versione di HearBridge e i file di log.

## Compilare

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test     # host tests: cc, ffmpeg, python3 + numpy (node optional)
make send PS5_HOST=<console-ip>
```

## Crediti

Pausa della scansione MediaTek, correzione della pipe USB e strumento di debug HCI, di [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## Licenza

GPL-3.0-or-later. Vedi [LICENSE](LICENSE) e [NOTICE](NOTICE).
