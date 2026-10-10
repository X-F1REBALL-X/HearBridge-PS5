<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**Auriculares Bluetooth para una PS5 con jailbreak: audio de juegos y del sistema, sin dongle.**

Desarrollado por **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 **Español** · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 es un payload (ELF) que envía el audio de la consola por el Bluetooth propio de la PS5 a auriculares o altavoces Bluetooth normales. Se controla desde una página web que sirve la consola. No toca los juegos, el firmware ni el jailbreak.

<p align="center"><img src="docs/img/ui-tv.png" alt="Página web de HearBridge PS5" width="900"></p>

## Requisitos

- Una PS5 con jailbreak y un cargador de ELF en el puerto **9021** (elfldr).
- Auriculares o un altavoz Bluetooth con A2DP (SBC, 48 kHz estéreo).
- Un navegador en la misma red (PS5, móvil o PC).

## Instalar y ejecutar

1. Descarga **HearBridge-PS5-1.3.0.elf** de la [última versión](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Envíalo al cargador: `socat -u FILE:HearBridge-PS5-1.3.0.elf TCP:<console-ip>:9021`
3. Abre **http://&lt;console-ip&gt;:8090** o el icono **HearBridge** de la pantalla de inicio.
4. Abre **Ajustes** y luego **Auriculares**. Pon los auriculares en modo de emparejamiento, pulsa **Buscar dispositivos** (unos 12 s) y después **Conectar**.

Cerrar la página deja HearBridge funcionando. Volver a enviar el ELF sustituye la copia en marcha y **Detener HearBridge** la termina.

## La página

- **Auriculares:** porcentaje de batería (leído por el enlace manos libres), señal y calidad del enlace. **Cambiar auriculares** pasa a otros auriculares guardados con un toque.
- **Sonido:** ecualizador de 5 bandas con preajustes, refuerzo hasta 500 %, volumen de los auriculares que sigue sus botones, **Silenciar** y **Modo nocturno** (explosiones más suaves, voces más claras).
- **Conexión:** códec y un control de latencia de 40 a 200 ms con una estimación del retardo en vivo.
- **Registro:** cada paso en color: verde funcionó, rojo falló, azul para lo que pulsas.

**Ajustes** abre un menú lateral:

- **Inicio:** vuelve a la página principal.
- **Auriculares:** buscar, conectar, desconectar y olvidar.
- **Sonido y juegos:** Tono de prueba, Sonido limpio, **Siguiente/anterior del auricular cambia el volumen** (para auriculares sin teclas de volumen) y tus juegos guardados.
- **Detalles:** formato, batería, AVRCP, desglose de la latencia y el chip Bluetooth.
- **Copia y restauración:** auriculares guardados con su emparejamiento, ajustes y perfiles de juego en un archivo, en una memoria USB, la consola o el dispositivo desde el que navegas.

### Perfiles de juego

Inicia un juego, ajusta el sonido que te guste y guárdalo. Los perfiles se guardan por juego y por auriculares, se activan solos cuando empieza el juego y devuelven tu sonido habitual al cerrarlo. **Actualizar perfil del juego** en el panel Sonido guarda tus cambios.

### Conexión automática y modo de reposo

Los auriculares guardados se conectan solos al encenderlos o sacarlos del estuche. Antes del modo de reposo HearBridge desconecta los auriculares limpiamente y los vuelve a conectar al despertar.

### Códecs

**Auto** es SBC normal y funciona con todo. **SBC HQ** y **SBC-XQ** se pueden elegir a mano si los auriculares los admiten. AAC, aptX y LDAC no son compatibles.

## ¿Qué chip tengo?

Un solo archivo funciona con los dos chips Bluetooth. HearBridge detecta el chip solo y lo muestra en **Detalles**.

| Modelo | Chip Bluetooth |
|---|---|
| CFI-10xx (lanzamiento) | Marvell/NXP |
| CFI-11xx, CFI-12xx (fat) | Marvell/NXP o MediaTek |
| CFI-20xx, CFI-21xx (Slim) | Marvell/NXP o MediaTek |
| CFI-70xx, CFI-71xx (Pro) | MediaTek |

Probado en PS5 CFI-10xx (Marvell/NXP) y PS5 Slim CFI-2008 (MediaTek). Se agradecen informes de otros modelos.

Fuentes: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights desmontaje de PS5 Pro](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## Límites conocidos

- Unos auriculares a la vez, sin micrófono. La TV sigue sonando también.
- Ejecuta solo un payload que use Bluetooth a la vez. El DualSense sigue funcionando.
- Auriculares probados: Sony WF-1000XM6, OnePlus Buds Ace 2, Xbox Wireless Headset.

## Informar de un problema

Cuando el payload arranca deberías ver la notificación **"HearBridge &lt;version&gt;: starting"**. Si no aparece, el cargador no lo ejecutó.

1. Descarga `/data/hearbridge/hearbridge.log` y `diag.txt` por FTP (por ejemplo [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv), puerto 2121). `diag.txt` también está en http://&lt;console-ip&gt;:8090/api/diag.
2. Abre un [issue en GitHub](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) con el modelo de consola, firmware, cargador, versión de HearBridge y los archivos de log.

## Compilar

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test     # host tests: cc, ffmpeg, python3 + numpy (node optional)
make send PS5_HOST=<console-ip>
```

## Créditos

Pausa del escaneo en MediaTek, arreglo de la tubería USB y herramienta de depuración HCI, por [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## Licencia

GPL-3.0-or-later. Ver [LICENSE](LICENSE) y [NOTICE](NOTICE).
