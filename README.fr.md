<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**Casque Bluetooth pour une PS5 jailbreakée : son des jeux et du système, sans dongle.**

Développé par **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 **Français** · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 est un payload (ELF) qui envoie le son des jeux et du système de ta PS5 vers un casque Bluetooth ordinaire, via le Bluetooth intégré de la console. On le contrôle depuis une page web servie par la console.

<p align="center"><img src="docs/img/ui-tv.png" alt="Page web de HearBridge PS5" width="900"></p>

## Points forts

- Un seul fichier pour toutes les PS5, Bluetooth Marvell/NXP ou MediaTek.
- Profils de jeu : ton son suit le jeu et le casque.
- Batterie en pourcentage, mode nuit et changement de casque en un geste.

## Installer

1. Télécharge **HearBridge-PS5-1.3.0.elf** depuis la [dernière version](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Envoie-le à ton chargeur d'ELF sur le port 9021.
3. Ouvre **http://&lt;console-ip&gt;:8090** ou la tuile **HearBridge**, puis appaire ton casque dans **Réglages**.

Tout le reste, des fonctions au dépannage, est dans le **[guide](https://x-f1reball-x.github.io/HearBridge-PS5/)**.

## Compiler

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
```

## Crédits

Pause du scan MediaTek, correctif du pipe USB et outil de débogage HCI, par [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## Licence

GPL-3.0-or-later. Voir [LICENSE](LICENSE) et [NOTICE](NOTICE).
