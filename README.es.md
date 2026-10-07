<div align="center">

# HearBridge PS5

**Auriculares Bluetooth para una PS5 con jailbreak: audio de juegos y del sistema, sin adaptador.**

Desarrollado por **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 **Español** · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md) · 🇮🇱 [עברית](README.he.md)

</div>

HearBridge PS5 es un payload (ELF) para una PS5 con jailbreak. Captura el audio de la consola y lo transmite por la propia radio Bluetooth de la consola a auriculares o altavoces Bluetooth normales (A2DP, SBC). Se controla desde una página web que sirve la consola. No modifica los juegos, el firmware ni el jailbreak.

<p align="center"><img src="docs/img/ui-en.png" alt="Página web de HearBridge PS5" width="520"></p>

## Requisitos

- Una PS5 con jailbreak y un cargador de ELF escuchando en el puerto **9021** (elfldr).
- Auriculares o un altavoz Bluetooth con **A2DP** (SBC).
- El navegador de la PS5, un móvil o un PC en la misma red.

## Instalación y ejecución

1. Descarga **HearBridge-PS5-1.0.0.elf** desde la [versión](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.0).
2. Envíalo al cargador de la consola, por ejemplo:
   `socat -u FILE:HearBridge-PS5-1.0.0.elf TCP:<console-ip>:9021`
3. Abre **http://&lt;console-ip&gt;:8090** (o el icono **HearBridge** que se añade a la pantalla de inicio la primera vez).

## Uso

- **Emparejar:** pon los auriculares en modo de emparejamiento, pulsa **Buscar dispositivos** (unos 20 s) y luego **Conectar** junto a ellos. Se guardan y empieza el audio.
- **Conectar:** los dispositivos guardados solo se conectan cuando pulsas **Conectar** en su fila. Nada se conecta en segundo plano.
- **Desconectar:** corta la conexión; el dispositivo sigue guardado. Si los auriculares vuelven a su estuche o salen de alcance, la página muestra **No conectado** hasta que pulses Conectar de nuevo.
- **Olvidar:** elimina el dispositivo y su clave de emparejamiento.
- **Volumen:** el control de refuerzo (ganancia por software, hasta 500 %) y el volumen de los auriculares (volumen absoluto AVRCP, también sigue sus propios botones). **Silenciar** y **Tono de prueba** ayudan a comprobar.
- **Detener HearBridge** termina el payload limpiamente. Úsalo antes de volver a cargar el ELF.

La página está disponible en 11 idiomas, incluidos hebreo y árabe (de derecha a izquierda).

## Notas

- Usa el Bluetooth integrado de la PS5; el DualSense sigue funcionando. Ejecuta solo un payload que use Bluetooth a la vez.
- Solo códec SBC, un dispositivo a la vez, sin micrófono. El televisor también sigue sonando.
- Probado con Sony WF-1000XM6, OnePlus Buds Ace 2 y Xbox Wireless Headset.
- Los ajustes, los dispositivos guardados y el registro están en `/data/hearbridge/` (`hearbridge.log` registra cada paso).

## Compilación

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make ps5
make test
```

## Licencia

GPL-3.0-or-later. Consulta [LICENSE](LICENSE) y [NOTICE](NOTICE).

---

<p align="center">Developed by X-F1REBALL-X</p>
