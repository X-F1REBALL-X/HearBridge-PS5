<div align="center">

# HearBridge PS5

**Un casque Bluetooth pour une PS5 jailbreakée : le son des jeux et du système, sans dongle.**

Développé par **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 **Français** · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md) · 🇮🇱 [עברית](README.he.md)

</div>

HearBridge PS5 est un payload (ELF) pour une PS5 jailbreakée. Il capture le son de la console et le diffuse via la radio Bluetooth de la console elle-même vers un casque ou une enceinte Bluetooth ordinaire (A2DP, SBC). On le pilote depuis une page web servie par la console. Il ne modifie ni les jeux, ni le firmware, ni le jailbreak.

<p align="center"><img src="docs/img/ui-en.png" alt="Page web de HearBridge PS5" width="520"></p>

## Prérequis

- Une PS5 jailbreakée avec un chargeur ELF à l'écoute sur le port **9021** (elfldr).
- Un casque ou une enceinte Bluetooth compatible **A2DP** (SBC).
- Le navigateur de la PS5, un téléphone ou un PC sur le même réseau.

## Installation et lancement

1. Téléchargez **HearBridge-PS5-1.0.1.elf** depuis la [version](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.1).
2. Envoyez-le au chargeur de la console, par exemple :
   `socat -u FILE:HearBridge-PS5-1.0.1.elf TCP:<console-ip>:9021`
3. Ouvrez **http://&lt;console-ip&gt;:8090** (ou la vignette **HearBridge** ajoutée à l'écran d'accueil au premier lancement).

## Utilisation

- **Appairer :** mettez le casque en mode appairage, appuyez sur **Rechercher des appareils** (environ 20 s), puis sur **Connecter** à côté de celui-ci. Il est enregistré et le son démarre.
- **Connecter :** les appareils enregistrés se connectent uniquement quand vous appuyez sur **Connecter** sur leur ligne. Rien ne se connecte en arrière-plan.
- **Déconnecter :** coupe la liaison ; l'appareil reste enregistré. Si le casque retourne dans son étui ou sort de portée, la page affiche **Non connecté** jusqu'à ce que vous appuyiez à nouveau sur Connecter.
- **Oublier :** supprime l'appareil et sa clé d'appairage.
- **Volume :** le curseur d'amplification (gain logiciel, jusqu'à 500 %) et le volume du casque (volume absolu AVRCP, qui suit aussi les boutons du casque). **Couper le son** et **Son de test** aident aux vérifications.
- **Arrêter HearBridge** termine proprement le payload. Utilisez-le avant de recharger l'ELF.

La page est disponible en 11 langues, dont l'hébreu et l'arabe (de droite à gauche).

## Remarques

- Utilise le Bluetooth intégré de la PS5 ; la DualSense continue de fonctionner. N'exécutez qu'un seul payload utilisant le Bluetooth à la fois.
- Codec SBC uniquement, un appareil à la fois, pas de micro. La télévision continue aussi à émettre le son.
- Testé avec Sony WF-1000XM6, OnePlus Buds Ace 2 et Xbox Wireless Headset.
- Les réglages, les appareils enregistrés et le journal se trouvent dans `/data/hearbridge/` (`hearbridge.log` enregistre chaque étape).

## Compilation

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make ps5
make test
```

## Licence

GPL-3.0-or-later. Voir [LICENSE](LICENSE) et [NOTICE](NOTICE).

---

<p align="center">Developed by X-F1REBALL-X</p>
