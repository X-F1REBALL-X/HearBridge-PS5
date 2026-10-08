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

1. [リリース](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.1)から **HearBridge-PS5-1.0.1.elf** をダウンロードします。
2. 本体のローダーに送信します。例:
   `socat -u FILE:HearBridge-PS5-1.0.1.elf TCP:<console-ip>:9021`
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

## トラブルシューティング / 問題の報告

**動作確認環境:** HearBridge は PS5 初期型（オリジナルモデル、CFI-10xx）、ファームウェア 10.20 で開発・テストされています。ほかのモデル（Slim、Pro、後期の初期型リビジョン）やほかのファームウェアは未テストで、別の Bluetooth チップを使っている可能性があります。それらでの報告を歓迎します。

よくある問題:

- ELF を実行しても **ホーム画面に HearBridge のアイコンが出ない**。
- **ページが開かない**（http://&lt;console-ip&gt;:8090）。
- **デバイスを検索** を押しても **ヘッドホンが見つからない**。
- ヘッドホンは接続されているのに **音が出ない**。

**ファームウェア 13.60:** [v1.0.1 リリース](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.1)の **HearBridge-PS5-1.0.1-fw13.60.elf** を試してください。ホーム画面のアイコンが出ない問題の修正を目的とし、診断情報を追加した実験的なビルドです。まだ実機ではテストされていません。

**ログの取得:**

1. HearBridge と同じローダーで FTP サーバーのペイロード（例: [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv)）を送信します。
2. FileZilla で、サーバーが表示するポートを使って本体の IP に接続します（ftpsrv は通常 **2121**）。
3. `/data/hearbridge/hearbridge.log` と、fw13.60 ビルドの場合は `/data/hearbridge/diag.txt` もダウンロードします。

fw13.60 ビルドでは **http://&lt;console-ip&gt;:8090/api/diag** を開いてテキストをコピーすることもできます。

**[GitHub の Issue](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) を作成し**、次の情報を含めてください:

- 本体のモデル（CFI 番号、例: CFI-1016A）
- ファームウェアのバージョン
- 使用したローダー
- **「HearBridge 1.0.1: http://…」** の通知が表示されたか
- ページが開くか
- ログファイル（`hearbridge.log`、`diag.txt`）

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
