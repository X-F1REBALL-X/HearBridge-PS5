<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**Casque Bluetooth pour une PS5 jailbreakée : son des jeux et du système, sans dongle.**

Développé par **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 **Français** · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 est un payload (ELF) qui envoie le son de la console par le Bluetooth intégré de la PS5 vers un casque ou une enceinte Bluetooth ordinaire. On le contrôle depuis une page web servie par la console. Il ne touche ni aux jeux, ni au firmware, ni au jailbreak.

<p align="center"><img src="docs/img/ui-tv.png" alt="Page web de HearBridge PS5" width="900"></p>

## Prérequis

- Une PS5 jailbreakée avec un chargeur d'ELF sur le port **9021** (elfldr).
- Un casque ou une enceinte Bluetooth compatible A2DP (SBC, 48 kHz stéréo).
- Un navigateur sur le même réseau (PS5, téléphone ou PC).

## Installer et lancer

1. Télécharge **HearBridge-PS5-1.3.0.elf** depuis la [dernière version](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Envoie-le au chargeur : `socat -u FILE:HearBridge-PS5-1.3.0.elf TCP:<console-ip>:9021`
3. Ouvre **http://&lt;console-ip&gt;:8090** ou la tuile **HearBridge** de l'écran d'accueil.
4. Ouvre **Réglages**, puis **Casques**. Mets le casque en mode appairage, appuie sur **Rechercher des appareils** (environ 12 s), puis **Connecter**.

Fermer la page laisse HearBridge tourner. Renvoyer l'ELF remplace la copie en cours, et **Arrêter HearBridge** l'arrête.

## La page

- **Casque** : batterie en pourcentage (lue par la liaison mains libres), signal et qualité de liaison. **Changer de casque** passe à un autre casque enregistré d'un seul geste.
- **Son** : égaliseur 5 bandes avec préréglages, amplification jusqu'à 500 %, volume du casque qui suit ses boutons, **Couper le son** et **Mode nuit** (explosions plus douces, voix plus claires).
- **Connexion** : codec et un curseur de latence de 40 à 200 ms avec une estimation du délai en direct.
- **Journal** : chaque étape en couleur : vert réussi, rouge échoué, bleu pour tes actions.

**Réglages** ouvre un menu latéral :

- **Accueil** : retour à la page principale.
- **Casques** : rechercher, connecter, déconnecter et oublier.
- **Son et jeux** : Son de test, Son net, **Suivant/précédent sur l'écouteur change le volume** (pour les écouteurs sans touches de volume) et tes jeux enregistrés.
- **Détails** : format, batterie, AVRCP, détail de la latence et puce Bluetooth.
- **Sauvegarde et restauration** : casques enregistrés avec leur appairage, réglages et profils de jeu dans un seul fichier, sur une clé USB, la console ou l'appareil depuis lequel tu navigues.

### Profils de jeu

Lance un jeu, règle le son qui te plaît et enregistre-le. Les profils sont gardés par jeu et par casque, s'activent tout seuls au lancement du jeu et rendent ton son habituel à sa fermeture. **Mettre à jour le profil du jeu** dans le panneau Son enregistre tes changements.

### Connexion automatique et mode repos

Les casques enregistrés se connectent tout seuls quand tu les allumes ou les sors de leur boîtier. Avant le mode repos, HearBridge arrête proprement le casque et le reconnecte au réveil.

### Codecs

**Auto** est du SBC standard et marche avec tout. **SBC HQ** et **SBC-XQ** se choisissent à la main quand le casque les prend en charge. AAC, aptX et LDAC ne sont pas pris en charge.

## Quelle puce ai-je ?

Un seul fichier fonctionne avec les deux puces Bluetooth. HearBridge détecte la puce tout seul et l'affiche dans **Détails**.

| Modèle | Puce Bluetooth |
|---|---|
| CFI-10xx (lancement) | Marvell/NXP |
| CFI-11xx, CFI-12xx (fat) | Marvell/NXP ou MediaTek |
| CFI-20xx, CFI-21xx (Slim) | Marvell/NXP ou MediaTek |
| CFI-70xx, CFI-71xx (Pro) | MediaTek |

Testé sur PS5 CFI-10xx (Marvell/NXP) et PS5 Slim CFI-2008 (MediaTek). Les retours sur d'autres modèles sont les bienvenus.

Sources: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights démontage de la PS5 Pro](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## Limites connues

- Un casque à la fois, pas de micro. La TV continue aussi à jouer le son.
- Ne lance qu'un seul payload Bluetooth à la fois. La DualSense continue de fonctionner.
- Casques testés : Sony WF-1000XM6, OnePlus Buds Ace 2, Xbox Wireless Headset.

## Signaler un problème

Quand le payload démarre, tu dois voir la notification **"HearBridge &lt;version&gt;: starting"**. Sinon, le chargeur ne l'a pas lancé.

1. Récupère `/data/hearbridge/hearbridge.log` et `diag.txt` par FTP (par exemple [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv), port 2121). `diag.txt` est aussi à http://&lt;console-ip&gt;:8090/api/diag.
2. Ouvre une [issue GitHub](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) avec le modèle de console, le firmware, le chargeur, la version de HearBridge et les fichiers de log.

## Compiler

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test     # host tests: cc, ffmpeg, python3 + numpy (node optional)
make send PS5_HOST=<console-ip>
```

## Crédits

Pause du scan MediaTek, correctif du pipe USB et outil de débogage HCI, par [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## Licence

GPL-3.0-or-later. Voir [LICENSE](LICENSE) et [NOTICE](NOTICE).
