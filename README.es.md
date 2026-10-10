<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**Auriculares Bluetooth para una PS5 con jailbreak: audio de juegos y del sistema, sin dongle.**

Desarrollado por **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 **Español** · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 es un payload (ELF) que lleva el audio de juegos y del sistema de tu PS5 a auriculares Bluetooth normales, usando el Bluetooth de la propia consola. Se controla desde una página web que sirve la consola.

<p align="center"><img src="docs/img/ui-tv.png" alt="Página web de HearBridge PS5" width="900"></p>

## Lo más destacado

- Un solo archivo para cualquier PS5, con Bluetooth Marvell/NXP o MediaTek.
- Perfiles de juego: tu sonido sigue al juego y a los auriculares.
- Batería en porcentaje, modo nocturno y cambio de auriculares con un toque.

## Instalar

1. Descarga **HearBridge-PS5-1.3.0.elf** de la [última versión](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Envíalo a tu cargador de ELF en el puerto 9021.
3. Abre **http://&lt;console-ip&gt;:8090** o el icono **HearBridge** y empareja tus auriculares en **Ajustes**.

Todo lo demás, de funciones a solución de problemas, está en la **[guía](https://x-f1reball-x.github.io/HearBridge-PS5/)**.

## Compilar

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
```

## Créditos

Pausa del escaneo en MediaTek, arreglo de la tubería USB y herramienta de depuración HCI, por [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## Licencia

GPL-3.0-or-later. Ver [LICENSE](LICENSE) y [NOTICE](NOTICE).
