<div align="center">

# HearBridge PS5

**脱獄済み PS5 で Bluetooth ヘッドホン：ゲームとシステムの音声を、ドングルなしで。**

開発: **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 **日本語** · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md) · 🇮🇱 [עברית](README.he.md)

</div>

HearBridge PS5 は脱獄済み PS5 用のペイロード（ELF）です。本体の音声をキャプチャし、本体内蔵の Bluetooth 無線で一般的な Bluetooth ヘッドホンやスピーカーへ送ります（A2DP、SBC）。操作は本体が提供する Web ページから行います。ゲーム、ファームウェア、脱獄環境は変更しません。

<p align="center"><img src="docs/img/ui-en.png" alt="HearBridge PS5 の Web ページ" width="520"></p>

## 必要なもの

- ポート **9021** で待ち受ける ELF ローダー（elfldr）がある脱獄済み PS5。
- **A2DP**（SBC）対応の Bluetooth ヘッドホンまたはスピーカー。
- 同じネットワーク上の PS5 ブラウザ、スマートフォン、または PC。

## インストールと起動

1. [リリース](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.0)から **HearBridge-PS5-1.0.0.elf** をダウンロードします。
2. 本体のローダーに送信します。例:
   `socat -u FILE:HearBridge-PS5-1.0.0.elf TCP:<console-ip>:9021`
3. **http://&lt;console-ip&gt;:8090** を開きます（初回起動時にホーム画面に追加される **HearBridge** タイルからも開けます）。

## 使い方

- **ペアリング:** ヘッドホンをペアリングモードにして **デバイスを検索**（約 20 秒）を押し、表示されたヘッドホンの横の **接続** を押します。保存され、音声が流れ始めます。
- **接続:** 保存済みデバイスは、その行の **接続** を押したときだけ接続します。バックグラウンドでは何も接続しません。
- **切断:** 接続を切ります。デバイスは保存されたままです。ヘッドホンをケースに戻したり範囲外に出たりすると、もう一度接続を押すまでページに **未接続** と表示されます。
- **削除:** デバイスとそのペアリングキーを削除します。
- **音量:** ブーストのスライダー（ソフトウェアゲイン、最大 500 %）とヘッドホンの音量（AVRCP 絶対音量。ヘッドホン本体のボタンにも追従）。**ミュート** と **テストトーン** は確認に便利です。
- **HearBridge を停止** でペイロードを正常に終了します。ELF を再読み込みする前に使ってください。

ページは 11 言語に対応しています（右から左に書くヘブライ語とアラビア語を含む）。

## 注意事項

- PS5 内蔵の Bluetooth を使います。DualSense はそのまま使えます。Bluetooth を使うペイロードは一度に一つだけ実行してください。
- SBC コーデックのみ、一度に一台、マイクなし。テレビからも音は出続けます。
- Sony WF-1000XM6、OnePlus Buds Ace 2、Xbox Wireless Headset で動作確認済み。
- 設定、保存済みデバイス、ログは `/data/hearbridge/` にあります（`hearbridge.log` に各手順が記録されます）。

## ビルド

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make ps5
make test
```

## ライセンス

GPL-3.0-or-later。[LICENSE](LICENSE) と [NOTICE](NOTICE) を参照してください。

---

<p align="center">Developed by X-F1REBALL-X</p>
