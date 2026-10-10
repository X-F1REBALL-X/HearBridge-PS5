<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**脱獄済み PS5 で Bluetooth ヘッドホン：ゲームとシステムの音を、ドングルなしで。**

開発： **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 **日本語** · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 は、PS5 のゲームとシステムの音を本体内蔵の Bluetooth で普通の Bluetooth ヘッドホンに流すペイロード（ELF）です。操作は本体が提供する Web ページから行います。

<p align="center"><img src="docs/img/ui-tv.png" alt="HearBridge PS5 の Web ページ" width="440"> <img src="docs/img/ui-settings.png" alt="HearBridge PS5 の Web ページ" width="440"></p>

## ハイライト

- Marvell/NXP でも MediaTek でも、どの PS5 にも 1 つのファイルで対応。
- ゲームプロファイル：音がゲームとヘッドセットに合わせて切り替わります。
- バッテリー残量、ナイトモード、ワンタップのヘッドセット切り替え。

## インストール

1. [最新リリース](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest)から **HearBridge-PS5-1.3.0.elf** をダウンロードします。
2. ポート 9021 の ELF ローダーに送ります。
3. **http://&lt;console-ip&gt;:8090** か **HearBridge** タイルを開き、**設定** でヘッドホンをペアリングします。

機能からトラブルシューティングまで、詳しくは **[ガイド](https://x-f1reball-x.github.io/HearBridge-PS5/)** をどうぞ。

## ビルド

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
```

## クレジット

MediaTek のスキャン一時停止と USB パイプの修正、HCI デバッグツール： [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## ライセンス

GPL-3.0-or-later。[LICENSE](LICENSE) と [NOTICE](NOTICE) を参照。
