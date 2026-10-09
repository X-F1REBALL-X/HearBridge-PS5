<div align="center">

# HearBridge PS5

**Casque Bluetooth pour une PS5 jailbreakée : son des jeux et du système, sans dongle.**

Développé par **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 **Français** · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md) · 🇮🇱 [עברית](README.he.md)

</div>

HearBridge PS5 est un payload (ELF) pour une PS5 jailbreakée. Il diffuse le son de la console via le Bluetooth de la PS5 elle-même vers un casque ou une enceinte Bluetooth ordinaire (A2DP). On le contrôle depuis une page web servie par la console. Il ne touche ni aux jeux, ni au firmware, ni au jailbreak.

<p align="center"><img src="docs/img/ui-en.png" alt="Page web de HearBridge PS5" width="520"></p>

## Prérequis

- Une PS5 jailbreakée avec un chargeur ELF sur le port **9021** (elfldr). Testé sur une PS5 fat (CFI-10xx) en firmware **10.20**.
- Un casque ou une enceinte Bluetooth A2DP (SBC, 48 kHz stéréo).
- Un navigateur sur le même réseau (PS5, téléphone ou PC).

## Installer et lancer

1. Téléchargez **HearBridge-PS5-1.1.0.elf** depuis la [dernière version](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Envoyez-le au chargeur : `socat -u FILE:HearBridge-PS5-1.1.0.elf TCP:<console-ip>:9021`
3. Ouvrez **http://&lt;console-ip&gt;:8090**, ou l'icône **HearBridge** de l'écran d'accueil.

Renvoyer l'ELF remplace la copie en cours. **Stop HearBridge** sur la page l'arrête.

## Fonctions

- **Appairer :** mettez le casque en mode appairage, appuyez sur **Scan for devices** (20 s), puis **Connect**.
- **Connexion automatique :** les casques enregistrés se connectent seuls quand on les allume ou qu'on les sort du boîtier. Sortir une autre paire enregistrée bascule vers elle. Après un **Disconnect** manuel, ils attendent **Connect**.
- **Disconnect / Forget :** Disconnect coupe la liaison et garde le casque enregistré. Forget le déconnecte et le supprime.
- **Volume :** boost (gain logiciel jusqu'à 500 %, démarre à 250 %) et volume du casque (AVRCP, démarre à 50 %). Enregistrés par casque une fois modifiés. **Mute** et **Test tone** pour les vérifications.
- **Égaliseur :** 5 bandes (±12 dB) avec préréglages, enregistré par casque, avec un limiteur pour que le boost ne sature pas.
- **Latence :** cible de tampon de 60 à 200 ms (200 ms par défaut) et une estimation en direct du délai.
- **Clean sound :** coupe l'égaliseur, remet le boost à 250 % et le tampon à 200 ms.
- **Journal** sur la page avec chaque étape. La page existe en 11 langues.

## Codecs

- **Auto :** SBC simple, marche avec tout.
- **SBC HQ** et **SBC-XQ :** à choisir à la main, seulement si le casque les prend en charge. Ceux non pris en charge sont grisés.

AAC, aptX et LDAC ne sont pas pris en charge.

## Limites connues

- Un casque à la fois, pas de micro. La TV continue aussi à jouer le son.
- Le casque doit accepter le SBC en 48 kHz stéréo.
- Lancez un seul payload Bluetooth à la fois. La DualSense continue de fonctionner.
- Testé uniquement en fw 10.20 (PS5 fat). Les autres modèles et firmwares ne sont pas testés ; la PS5 Pro et certaines configurations 13.x ne démarreraient pas ([#1](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/1), [#2](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/2)).
- Casques testés : Sony WF-1000XM6, OnePlus Buds Ace 2, Xbox Wireless Headset.

## Dépannage / signaler un problème

Au lancement du payload, la notification **"HearBridge &lt;version&gt;: starting"** doit apparaître. Sinon, le chargeur ne l'a pas lancé.

Les réglages, les casques enregistrés et le journal sont dans `/data/hearbridge/`. Pour signaler un problème :

1. Récupérez `/data/hearbridge/hearbridge.log` et `diag.txt` par FTP (par exemple [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv), port 2121). `diag.txt` est aussi sur http://&lt;console-ip&gt;:8090/api/diag.
2. Ouvrez une [issue GitHub](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) avec le modèle de console, le firmware, le chargeur, la version de HearBridge et les journaux.

## Compiler

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
make send PS5_HOST=<console-ip>
```

## Licence

GPL-3.0-or-later. Voir [LICENSE](LICENSE) et [NOTICE](NOTICE).

---

<p align="center">Developed by X-F1REBALL-X</p>
