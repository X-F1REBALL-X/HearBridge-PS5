<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**脱獄済み PS5 で Bluetooth ヘッドホン：ゲームとシステムの音を、ドングルなしで。**

開発： **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 **日本語** · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 は、本体の音を PS5 内蔵の Bluetooth で普通の Bluetooth ヘッドホンやスピーカーに送るペイロード（ELF）です。操作は本体が提供する Web ページから行います。ゲーム、ファームウェア、脱獄には手を加えません。

<p align="center"><img src="docs/img/ui-tv.png" alt="HearBridge PS5 の Web ページ" width="900"></p>

## 必要なもの

- ポート **9021** で ELF ローダー（elfldr）が動いている脱獄済み PS5。
- A2DP（SBC、48 kHz ステレオ）対応の Bluetooth ヘッドホンまたはスピーカー。
- 同じネットワーク上のブラウザ（PS5、スマホ、PC）。

## インストールと起動

1. [最新リリース](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest)から **HearBridge-PS5-1.3.0.elf** をダウンロードします。
2. ローダーに送ります： `socat -u FILE:HearBridge-PS5-1.3.0.elf TCP:<console-ip>:9021`
3. **http://&lt;console-ip&gt;:8090** を開くか、ホーム画面の **HearBridge** タイルを開きます。
4. **設定** を開き、**ヘッドセット** を選びます。ヘッドホンをペアリングモードにして **デバイスを検索**（約 12 秒）を押し、**接続** を押します。

ページを閉じても HearBridge は動き続けます。ELF をもう一度送ると動作中のコピーと入れ替わり、**HearBridge を停止** で終了します。

## ページ

- **ヘッドセット**：バッテリー残量（ハンズフリー接続で取得）、電波、リンク品質。**ヘッドセットを切り替え** で保存済みの別のヘッドセットにワンタップで切り替え。
- **サウンド**：プリセット付き 5 バンドイコライザー、最大 500 % のブースト、ヘッドセットのボタンに追従する音量、**ミュート**、**ナイトモード**（爆発音は控えめ、声ははっきり）。
- **接続**：コーデックと 40 から 200 ms のレイテンシスライダー、遅延のライブ推定。
- **ログ**：すべての手順を色分け表示。緑は成功、赤は失敗、青は自分の操作。

**設定** でサイドメニューが開きます：

- **ホーム**：メインページに戻る。
- **ヘッドセット**：検索、接続、切断、削除。
- **サウンドとゲーム**：テストトーン、クリアな音、**イヤホンの次へ/前へで音量を変える**（音量ボタンのないイヤホン向け）、保存したゲーム。
- **詳細**：フォーマット、バッテリー、AVRCP、レイテンシの内訳、Bluetooth チップ。
- **バックアップと復元**：保存したヘッドセット（ペアリング情報込み）、設定、ゲームプロファイルを 1 つのファイルに。USB ドライブ、本体、閲覧中のデバイスに保存できます。

### ゲームプロファイル

ゲームを起動し、好みの音にして保存します。プロファイルはゲームごと、ヘッドセットごとに保存され、ゲーム開始時に自動で有効になり、終了するといつもの音に戻ります。サウンド パネルの **ゲームプロファイルを更新** で変更を保存します。

### 自動接続とレストモード

保存したヘッドセットは、電源を入れたりケースから出したりすると自動で接続します。レストモードの前に HearBridge はヘッドセットをきれいに停止し、復帰後に再接続します。

### コーデック

**自動** は通常の SBC で、どの機器でも使えます。**SBC HQ** と **SBC-XQ** はヘッドセットが対応していれば手動で選べます。 AAC、aptX、LDAC には対応していません。

## 自分のチップはどれ？

1 つのファイルで両方の Bluetooth チップに対応します。HearBridge がチップを自動で判別し、**詳細** に表示します。

| モデル | Bluetooth チップ |
|---|---|
| CFI-10xx (初期型) | Marvell/NXP |
| CFI-11xx, CFI-12xx (通常型) | Marvell/NXP または MediaTek |
| CFI-20xx, CFI-21xx (Slim) | Marvell/NXP または MediaTek |
| CFI-70xx, CFI-71xx (Pro) | MediaTek |

PS5 CFI-10xx（Marvell/NXP）と PS5 Slim CFI-2008（MediaTek）で動作確認済み。他のモデルの報告も歓迎します。

出典: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights PS5 Pro 分解](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## 既知の制限

- ヘッドセットは同時に 1 台、マイクは使えません。テレビからも音が出続けます。
- Bluetooth を使うペイロードは同時に 1 つだけ動かしてください。DualSense はそのまま使えます。
- 確認済みヘッドホン：Sony WF-1000XM6、OnePlus Buds Ace 2、Xbox Wireless Headset。

## 問題の報告

ペイロードが動くと **"HearBridge &lt;version&gt;: starting"** という通知が出ます。出ない場合、ローダーが実行していません。

1. `/data/hearbridge/hearbridge.log` と `diag.txt` を FTP で取得します（例：[ftpsrv](https://github.com/ps5-payload-dev/ftpsrv)、ポート 2121）。`diag.txt` は http://&lt;console-ip&gt;:8090/api/diag でも取得できます。
2. 本体のモデル、ファームウェア、ローダー、HearBridge のバージョン、ログファイルを添えて [GitHub の issue](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) を開いてください。

## ビルド

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test     # host tests: cc, ffmpeg, python3 + numpy (node optional)
make send PS5_HOST=<console-ip>
```

## クレジット

MediaTek のスキャン一時停止と USB パイプの修正、HCI デバッグツール： [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## ライセンス

GPL-3.0-or-later。[LICENSE](LICENSE) と [NOTICE](NOTICE) を参照。
