<div align="center">

# HearBridge PS5

**Fones Bluetooth para um PS5 desbloqueado: áudio dos jogos e do sistema, sem dongle.**

Desenvolvido por **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 **Português** · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md) · 🇮🇱 [עברית](README.he.md)

</div>

HearBridge PS5 é um payload (ELF) para um PS5 desbloqueado. Ele transmite o áudio do console pelo Bluetooth do próprio PS5 para fones ou caixas de som Bluetooth comuns (A2DP). O controle é feito por uma página web servida pelo console. Não mexe em jogos, firmware nem no desbloqueio.

<p align="center"><img src="docs/img/ui-en.png" alt="Página web do HearBridge PS5" width="900"></p>

## Requisitos

- Um PS5 desbloqueado com um carregador ELF na porta **9021** (elfldr). Testado em um PS5 fat (CFI-10xx) com firmware **10.20**.
- Fones ou caixa de som Bluetooth com A2DP (SBC, 48 kHz estéreo).
- Um navegador na mesma rede (PS5, celular ou PC).

## Instalar e executar

1. Baixe **HearBridge-PS5-1.1.0.elf** da [versão mais recente](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Envie para o carregador: `socat -u FILE:HearBridge-PS5-1.1.0.elf TCP:<console-ip>:9021`
3. Abra **http://&lt;console-ip&gt;:8090**, ou o ícone **HearBridge** na tela inicial.

Enviar o ELF de novo substitui a cópia em execução. **Stop HearBridge** na página encerra.

## Qual chip eu tenho?

Procure seu modelo na tabela. Os fat e os Slim vêm com um de dois chips Bluetooth, então nesses confira o log.

| Modelo | [1.1.0](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0) | [mediatek test](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0-mtk-test) |
|---|---|---|
| CFI-10xx (lançamento) | ✅ funciona (testado, fw 10.20) | não precisa |
| CFI-11xx, CFI-12xx (fat) | ✅ se o chip for Marvell/NXP | ⚠️ se o chip for MediaTek (não testado) |
| CFI-20xx, CFI-21xx (Slim, a maioria dos Slim tem MediaTek*) | ✅ se o chip for Marvell/NXP | ⚠️ se o chip for MediaTek (não testado) |
| CFI-70xx, CFI-71xx (Pro) | ❌ não funciona (MediaTek) | ✅ use esta (não testado) |

\* segundo fóruns de reparo, não confirmado pela Sony

**Como verificar:** rode o HearBridge e veja **Chip** embaixo em **Status** na página. Ou abra `/data/hearbridge/hearbridge.log` e procure a linha `usb: /dev/ugen0.2 is XXXX:YYYY` (o número do ugen pode mudar). `1286` = Marvell/NXP, use **[1.1.0](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0)**. `0e8d` = MediaTek, use **[mediatek test](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0-mtk-test)**.

Fontes: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights PS5 Pro teardown](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## Recursos

- **Parear:** coloque os fones em modo de pareamento, toque em **Scan for devices** (20 s) e depois em **Connect**.
- **Conexão automática:** fones salvos conectam sozinhos ao ligar ou ao tirar do estojo. Tirar outro par salvo troca para ele. Depois de um **Disconnect** manual eles esperam o **Connect**.
- **Disconnect / Forget:** Disconnect desliga a conexão e mantém os fones salvos. Forget desconecta e remove.
- **Volume:** boost (ganho por software até 500 %, começa em 250 %) e volume do fone (AVRCP, começa em 50 %). Salvos por fone depois que você muda. **Mute** e **Test tone** para testes.
- **Equalizador:** 5 bandas (±12 dB) com presets, salvo por fone, com limitador para o boost não distorcer.
- **Latência:** alvo de buffer de 60 a 200 ms (padrão 200 ms) e uma estimativa ao vivo do atraso.
- **Clean sound:** desliga o equalizador, volta o boost para 250 % e o buffer para 200 ms.
- **Log** na página com cada passo, em cores: verde deu certo, vermelho falhou, azul para o que você toca. A página está em 11 idiomas.
- **Chip:** embaixo em **Status** aparece o seu chip Bluetooth. Com chip MediaTek aparece um link para a versão mediatek test.

## Codecs

- **Auto:** SBC simples, funciona com tudo.
- **SBC HQ** e **SBC-XQ:** escolha manual, só quando os fones suportam. Os não suportados ficam cinza.

AAC, aptX e LDAC não são suportados.

## Limites conhecidos

- Um fone por vez, sem microfone. A TV continua tocando o som também.
- Os fones precisam aceitar SBC a 48 kHz estéreo.
- Rode só um payload de Bluetooth por vez. O DualSense continua funcionando.
- Testado só no fw 10.20 (PS5 fat, CFI-10xx). Outros modelos e firmwares não foram testados. Há relatos de que o PS5 Pro e algumas configurações 13.x não iniciavam com versões anteriores ([#1](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/1), [#2](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/2)); com chip MediaTek, teste a versão mediatek test.
- Fones testados: Sony WF-1000XM6, OnePlus Buds Ace 2, Xbox Wireless Headset.

## Solução de problemas / relatar um problema

Ao rodar o payload deve aparecer a notificação **"HearBridge &lt;version&gt;: starting"**. Se não aparecer, o carregador não o executou.

Configurações, fones salvos e o log ficam em `/data/hearbridge/`. Para relatar um problema:

1. Baixe `/data/hearbridge/hearbridge.log` e `diag.txt` por FTP (por exemplo [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv), porta 2121). `diag.txt` também está em http://&lt;console-ip&gt;:8090/api/diag.
2. Abra uma [issue no GitHub](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) com o modelo do console, firmware, carregador, versão do HearBridge e os logs.

## Compilar

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
make send PS5_HOST=<console-ip>
```

## Licença

GPL-3.0-or-later. Veja [LICENSE](LICENSE) e [NOTICE](NOTICE).

---

<p align="center">Developed by X-F1REBALL-X</p>
