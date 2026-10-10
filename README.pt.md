<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**Fones Bluetooth para um PS5 desbloqueado: áudio dos jogos e do sistema, sem dongle.**

Desenvolvido por **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 **Português** · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

O HearBridge PS5 é um payload (ELF) que envia o áudio do console pelo Bluetooth do próprio PS5 para fones ou caixas de som Bluetooth comuns. Você controla tudo por uma página web servida pelo console. Ele não mexe nos jogos, no firmware nem no jailbreak.

<p align="center"><img src="docs/img/ui-tv.png" alt="Página web do HearBridge PS5" width="900"></p>

## Requisitos

- Um PS5 desbloqueado com um carregador de ELF na porta **9021** (elfldr).
- Fones ou uma caixa de som Bluetooth com A2DP (SBC, 48 kHz estéreo).
- Um navegador na mesma rede (PS5, celular ou PC).

## Instalar e executar

1. Baixe **HearBridge-PS5-1.3.0.elf** da [versão mais recente](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. Envie para o carregador: `socat -u FILE:HearBridge-PS5-1.3.0.elf TCP:<console-ip>:9021`
3. Abra **http://&lt;console-ip&gt;:8090** ou o bloco **HearBridge** na tela inicial.
4. Abra **Definições** e depois **Fones**. Coloque os fones em modo de pareamento, toque em **Procurar dispositivos** (cerca de 12 s) e depois em **Conectar**.

Fechar a página deixa o HearBridge rodando. Enviar o ELF de novo substitui a cópia em execução, e **Parar o HearBridge** encerra.

## A página

- **Fone:** bateria em porcentagem (lida pela ligação mãos livres), sinal e qualidade da ligação. **Trocar de fone** troca para outro fone salvo com um toque.
- **Som:** equalizador de 5 bandas com predefinições, reforço até 500 %, volume do fone que acompanha os botões dele, **Silenciar** e **Modo noturno** (explosões mais baixas, vozes mais claras).
- **Conexão:** codec e um controle de latência de 40 a 200 ms com estimativa do atraso ao vivo.
- **Registo:** cada passo em cores: verde deu certo, vermelho falhou, azul para o que você tocou.

**Definições** abre um menu lateral:

- **Início:** volta para a página principal.
- **Fones:** procurar, conectar, desconectar e esquecer.
- **Som e jogos:** Tom de teste, Som limpo, **Próxima/anterior no fone muda o volume** (para fones sem botões de volume) e seus jogos salvos.
- **Detalhes:** formato, bateria, AVRCP, detalhamento da latência e o chip Bluetooth.
- **Cópia e restauro:** fones salvos com o pareamento, ajustes e perfis de jogo num só arquivo, num pendrive, no console ou no aparelho em que você está navegando.

### Perfis de jogo

Abra um jogo, ajuste o som do seu jeito e salve. Os perfis ficam por jogo e por fone, ligam sozinhos quando o jogo começa e devolvem seu som de sempre quando ele fecha. **Atualizar perfil do jogo** no painel Som salva as mudanças.

### Conexão automática e modo de repouso

Fones salvos conectam sozinhos quando você liga ou tira do estojo. Antes do modo de repouso o HearBridge desliga o fone de forma limpa e reconecta depois que o console acorda.

### Codecs

**Auto** é SBC comum e funciona com tudo. **SBC HQ** e **SBC-XQ** podem ser escolhidos à mão quando o fone suporta. AAC, aptX e LDAC não são suportados.

## Qual chip eu tenho?

Um só arquivo funciona com os dois chips Bluetooth. O HearBridge detecta o chip sozinho e mostra em **Detalhes**.

| Modelo | Chip Bluetooth |
|---|---|
| CFI-10xx (lançamento) | Marvell/NXP |
| CFI-11xx, CFI-12xx (fat) | Marvell/NXP ou MediaTek |
| CFI-20xx, CFI-21xx (Slim) | Marvell/NXP ou MediaTek |
| CFI-70xx, CFI-71xx (Pro) | MediaTek |

Testado no PS5 CFI-10xx (Marvell/NXP) e no PS5 Slim CFI-2008 (MediaTek). Relatos de outros modelos são bem-vindos.

Fontes: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights desmontagem do PS5 Pro](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## Limitações conhecidas

- Um fone por vez, sem microfone. A TV continua tocando o som também.
- Rode só um payload de Bluetooth por vez. O DualSense continua funcionando.
- Fones testados: Sony WF-1000XM6, OnePlus Buds Ace 2, Xbox Wireless Headset.

## Relatar um problema

Quando o payload roda, aparece a notificação **"HearBridge &lt;version&gt;: starting"**. Se não aparecer, o carregador não o executou.

1. Pegue `/data/hearbridge/hearbridge.log` e `diag.txt` por FTP (por exemplo [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv), porta 2121). O `diag.txt` também está em http://&lt;console-ip&gt;:8090/api/diag.
2. Abra uma [issue no GitHub](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) com o modelo do console, firmware, carregador, versão do HearBridge e os arquivos de log.

## Compilar

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test     # host tests: cc, ffmpeg, python3 + numpy (node optional)
make send PS5_HOST=<console-ip>
```

## Créditos

Pausa da busca no MediaTek, correção do pipe USB e ferramenta de depuração HCI, por [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## Licença

GPL-3.0-or-later. Veja [LICENSE](LICENSE) e [NOTICE](NOTICE).
