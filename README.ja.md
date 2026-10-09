<div align="center">

# HearBridge PS5

**脱獄した PS5 で Bluetooth ヘッドホン：ゲームとシステムの音声、ドングル不要。**

開発：**X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 **日本語** · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md) · 🇮🇱 [עברית](README.he.md)

</div>

HearBridge PS5 は脱獄した PS5 用のペイロード（ELF）です。本体の音声を PS5 自身の Bluetooth で普通の Bluetooth ヘッドホンやスピーカー（A2DP）に送ります。操作は本体が提供する Web ページから行います。ゲーム、ファームウェア、脱獄には手を加えません。

<p align="center"><img src="docs/img/ui-en.png" alt="HearBridge PS5 の Web ページ" width="900"></p>

## 必要なもの

- ポート **9021** で ELF ローダー（elfldr）が動いている脱獄済み PS5。
- A2DP（SBC、48 kHz ステレオ）対応の Bluetooth ヘッドホンまたはスピーカー。
- 同じネットワーク上のブラウザ（PS5、スマホ、PC）。

## インストールと起動

1. [最新リリース](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest)から **HearBridge-PS5-1.1.0.elf** をダウンロード。
2. ローダーに送信：`socat -u FILE:HearBridge-PS5-1.1.0.elf TCP:<console-ip>:9021`
3. **http://&lt;console-ip&gt;:8090** を開くか、ホーム画面の **HearBridge** タイルを開く。

ELF をもう一度送ると、動作中のものと入れ替わります。ページの **Stop HearBridge** で終了します。

## どのチップ？

PS5 の Bluetooth チップは Marvell/NXP か MediaTek のどちらかです。初期型（CFI-11xx/12xx）と Slim（CFI-20xx/21xx）は、同じ型番でもどちらのチップの場合もあります。

| モデル | ダウンロード |
|---|---|
| CFI-10xx (発売時モデル) | [1.1.0](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0) |
| CFI-11xx, CFI-12xx (初期型) | チップを確認: Marvell/NXP → [1.1.0](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0), MediaTek → [mediatek test](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0-mtk-test) |
| CFI-20xx, CFI-21xx (Slim) | チップを確認: Marvell/NXP → [1.1.0](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0), MediaTek → [mediatek test](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0-mtk-test) |
| CFI-70xx, CFI-71xx (Pro、常に MediaTek) | [mediatek test](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.1.0-mtk-test) |

mediatek test ビルドはまだテストされていません。1.1.0 は CFI-10xx でのみテスト済みです。結果を報告してください。

**確認方法:** 1.1.0 を起動し、**Status** パネル下部の **Chip** 行を見ます。`Marvell/NXP (1286:…)` → 1.1.0 のまま。`MediaTek (0e8d:…)` → mediatek test を使用（ページにオレンジ色の案内も出ます）。または `/data/hearbridge/hearbridge.log` を開いて `usb: /dev/ugen0.2 is 1286:2059 …` を探します（ugen の番号は違うことがあります）。"is" の後の最初の 4 文字がチップです：`1286` = Marvell/NXP、`0e8d` = MediaTek。

出典: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights PS5 Pro teardown](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## 機能

- **ペアリング：** ヘッドホンをペアリングモードにして **Scan for devices**（20 秒）を押し、**Connect** を押します。
- **自動接続：** 保存済みのヘッドホンは電源を入れたりケースから出したりすると自動で接続します。別の保存済みヘッドホンを出すとそちらに切り替わります。手動で **Disconnect** した後は **Connect** を待ちます。
- **Disconnect / Forget：** Disconnect は接続を切り、ヘッドホンは保存されたままです。Forget は切断して削除します。
- **音量：** ブースト（ソフトウェアゲイン、最大 500 %、初期値 250 %）とヘッドホン音量（AVRCP、初期値 50 %）。変更するとヘッドホンごとに保存されます。確認用に **Mute** と **Test tone**。
- **イコライザー：** 5 バンド（±12 dB）、プリセット付き、ヘッドホンごとに保存。リミッター付きでブーストしても音割れしません。
- **遅延：** バッファ目標 60〜200 ms（初期値 200 ms）と遅延のライブ推定値。
- **Clean sound：** イコライザーをオフにし、ブーストを 250 %、バッファを 200 ms に戻します。
- ページに全ステップの**ログ**を色分けで表示（緑は成功、赤は失敗、青はボタン操作）。

## コーデック

- **Auto：** 通常の SBC。どれでも動きます。
- **SBC HQ** と **SBC-XQ：** 手動で選択。ヘッドホンが対応している場合のみ。非対応のものはグレー表示です。

AAC、aptX、LDAC には対応していません。

## 既知の制限

- 同時に 1 台のみ、マイクなし。テレビからも音が出続けます。
- Bluetooth を使うペイロードは 1 つだけ実行してください。DualSense はそのまま使えます。
- テストは fw 10.20（PS5 fat、CFI-10xx）のみ。他のモデルやファームウェアは未テストです。
- テスト済みヘッドホン：Sony WF-1000XM6、OnePlus Buds Ace 2、Xbox Wireless Headset。

## トラブルシューティング / 問題の報告

ペイロードを実行すると **「HearBridge &lt;version&gt;: starting」** の通知が出るはずです。出ない場合、ローダーが実行していません。

設定、保存済みヘッドホン、ログは `/data/hearbridge/` にあります。問題を報告するには：

1. FTP で `/data/hearbridge/hearbridge.log` と `diag.txt` を取得します（例：[ftpsrv](https://github.com/ps5-payload-dev/ftpsrv)、ポート 2121）。`diag.txt` は http://&lt;console-ip&gt;:8090/api/diag でも見られます。
2. 本体モデル、ファームウェア、ローダー、HearBridge のバージョン、ログを添えて [GitHub の issue](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) を作成してください。

## ビルド

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
make send PS5_HOST=<console-ip>
```

## ライセンス

GPL-3.0-or-later。[LICENSE](LICENSE) と [NOTICE](NOTICE) を参照。
