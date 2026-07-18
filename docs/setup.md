# 環境セットアップ手順(cardputer-adv-printer)

M5Stack **Cardputer ADV** 用スケッチをビルド・書き込みできる状態にするまでの手順。
操作はすべて `arduino-cli` で行う(Arduino IDE は使わない)。

## 前提

- Windows + PowerShell を想定。
- `arduino-cli` が導入済みであること(動作確認は 1.5.1)。
- Node.js が導入済みであること。

## 手順

### 1〜4. ボードパッケージ

```powershell
arduino-cli config init   # 既にあるならスキップ(--overwrite は使わない)
arduino-cli config add board_manager.additional_urls https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
arduino-cli core update-index
arduino-cli core install m5stack:esp32
```

### 5. FQBN の確認

```powershell
arduino-cli board listall m5stack
```

Cardputer の FQBN は **`m5stack:esp32:m5stack_cardputer`**。

> **Cardputer ADV 専用のボード定義は無い**(公式ドキュメントも Cardputer /
> Cardputer-Adv 共通で `M5Cardputer` を選ばせている)。ADV かどうかは
> M5Unified が実行時に判別する。専用ボードを探して時間を使わないこと。

> **ビルドには必ずオプション付き FQBN を使う**:
> `m5stack:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=default_8MB`
> ボード既定は 4MB フラッシュ / 1.2MB app 想定だが、ADV の実装は 8MB。
> 既定のままだと日本語フォント+画像埋め込みで app 領域が溢れる。
> npm script 経由なら `tools/fqbn.mjs` が自動で付ける。

### 6. ライブラリのインストール

```powershell
arduino-cli lib install M5Cardputer
```

M5Cardputer(キーボードドライバ TCA8418 込み)が入り、依存の M5Unified / M5GFX も
自動で入る。

### 7. コンパイル確認

```powershell
cd cardputer-adv-printer
npm install
copy contents\print.md.example contents\print.md
npm run build
```

これが通れば環境構築は完了。

### 8. 書き込みとポートについて

Cardputer ADV には USB-UART ブリッジチップが**無く**、ESP32-S3 自身が
**Espressif USB Serial/JTAG(VID `0x303A` / PID `0x1001`)**として列挙される。
Windows 10/11 は標準ドライバで認識するので、ドライバの追加インストールは不要。

```powershell
arduino-cli board list    # COMxx (VID 0x303A) が Cardputer
npm run flash             # ポートは自動検出
```

> **ポートが見えないときは**: 一度でも印刷(またはスキャン)を実行した後は
> USB が OTG ホストモードに切り替わっており、PC からは何も見えない。
> **ケーブルを抜いた状態で電源スイッチを切って入れ直してから** PC につなぐ。
> PC につないだままスイッチを切っても USB 給電で動き続けるので効かない。

## 動作確認済みの構成(2026-07-18)

| 項目 | バージョン |
|---|---|
| arduino-cli | 1.5.1 |
| ボードパッケージ `m5stack:esp32` | 3.3.8 |
| M5Cardputer | 1.1.1 |
| M5Unified | 0.2.18 |
| M5GFX | 0.2.25 |
| FQBN | `m5stack:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=default_8MB` |
| プリンタ | Phomemo M220(USB, VID 0x0483 / PID 0x5740, class 07/01/02) |

## ハードウェア仕様(Cardputer ADV)

| 項目 | 内容 |
|---|---|
| SoC | ESP32-S3FN8(デュアルコア 240MHz、フラッシュ 8MB 内蔵) |
| PSRAM | **なし**(SRAM 512KB のみ。設計に効く — design.md 参照) |
| RTC | **なし**(時刻は電源断で消える) |
| ディスプレイ | 1.14インチ ST7789V2、240×135 |
| キーボード | 56 キー(TCA8418 I2C) |
| USB | USB-C ×1(Serial/JTAG と OTG ホストを共用・排他) |
| バッテリー | 1750mAh、物理電源スイッチあり |

プリンタとの接続には USB-A(メス)→USB-C(オス)変換コネクタ + USB A-C ケーブルを使う。
Cardputer 側が VBUS(5V)を供給できることは実機確認済み。

## 次にやること

[README.md](../README.md) の「はじめかた」へ。
