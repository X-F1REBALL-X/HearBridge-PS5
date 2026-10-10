<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**Fones Bluetooth para um PS5 desbloqueado: áudio dos jogos e do sistema, sem dongle.**

Desenvolvido por **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 **Português** · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

O HearBridge PS5 é um payload (ELF) que leva o áudio dos jogos e do sistema do seu PS5 para fones Bluetooth comuns, usando o Bluetooth do próprio console. Você controla tudo por uma página web servida pelo console.

<p align="center"><img src="docs/img/ui-tv.png" alt="Página web do HearBridge PS5" width="900"></p>

## Destaques

- Um só arquivo para qualquer PS5, com Bluetooth Marvell/NXP ou MediaTek.
- Perfis de jogo: seu som acompanha o jogo e o fone.
- Bateria em porcentagem, modo noturno e troca de fone com um toque.

## Instalar

1. Baixe **HearBridge-PS5-1.3.0.elf** da [versão mais recente](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Envie para o seu carregador de ELF na porta 9021.
3. Abra **http://&lt;console-ip&gt;:8090** ou o bloco **HearBridge** e pareie seus fones em **Definições**.

Todo o resto, de funções a solução de problemas, está no **[guia](https://x-f1reball-x.github.io/HearBridge-PS5/)**.

## Compilar

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
```

## Créditos

Pausa da busca no MediaTek, correção do pipe USB e ferramenta de depuração HCI, por [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## Licença

GPL-3.0-or-later. Veja [LICENSE](LICENSE) e [NOTICE](NOTICE).
