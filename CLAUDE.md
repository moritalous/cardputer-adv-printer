# cardputer-adv-printer

M5Stack Cardputer ADV から USB サーマルプリンタ(Phomemo M220)へ、
Markdown テンプレートの `{{input}}` にキーボード入力を差し込んで印刷する。

概要とセットアップは @README.md 、テンプレート書式は @docs/content.md 、
設計と注意点は @docs/design.md 。

## Arduino の操作

すべて CLI で行う。Arduino IDE は使わない。

FQBN は `m5stack:esp32:m5stack_cardputer` に **`FlashSize=8M,PartitionScheme=default_8MB` を
必ず付ける**(既定 4MB 想定のままだと日本語フォント+画像で app 領域が溢れる)。
定義は `tools/fqbn.mjs` の 1 箇所だけ。ADV 専用のボード定義は存在しない
(M5Unified が実行時判別)。

## ビルド・書き込み

**`arduino-cli compile` を直接叩かないこと。** `content.h` の生成が走らない。npm script を使う。

```powershell
npm run gen            # 生成のみ
npm run build          # 生成 + コンパイル
npm run flash          # 生成 + コンパイル + 書き込み(ポート自動検出)
npm run flash -- COM10 # ポートを明示指定
npm run scan           # USBディスクリプタビューアを書き込み(調査用)
```

`npm run scan` は**印刷ファームを上書きする**。終わったら `npm run flash` で戻すこと。

## USB の排他(このプロジェクト最大の特性)

ESP32-S3 の USB PHY は 1 つ。**書き込み用 Serial/JTAG と、プリンタ接続用 OTG ホストが排他。**

- スケッチは初回印刷時に初めてホストを開始する(起動直後は書き込み可能)
- **一度印刷したら、電源スイッチ入れ直し(ケーブルを抜いた状態で)まで PC から見えない**
- 「ポートが無い」「書き込めない」の 9 割はこれ。まず電源入れ直し
- ポートは Espressif VID 0x303A(USB Serial/JTAG)。CP210x 等のブリッジは無い

## 触ると壊れるもの

- `sketch/content.h` は生成物。直接編集しない
- `usb_printer.cpp` の `ensureInited()`(GET_DEVICE_ID / GET_PORT_STATUS / bulk IN 読み捨て)を
  削らない。**まっさらな M220 はこのハンドシェイクまでジョブを黙って捨てる**
- ラスタは単一ブロック(`MAX_BLOCK_ROWS = MAX_CANVAS_H`)。**分割するとブロック間で
  紙送りが入り継ぎ目ができる**。240 行分割は過去の誤診なので再導入しない
- 画像は `drawBitmap`。`drawPng` は 1bpp パレットスプライトで全黒になるので使わない
- `CONTENT_W = 384` は `sketch/sketch.ino` と `tools/gen-content.mjs` の両方にある。一致させる
- `drawContent()` に `setClipRect` を足さない。`println("")` の行送り実測を消さない
- キャンバスは 1bpp。印刷はフレームバッファ直読み(ビット反転コピー)でこの前提に依存
- esc は独立キーではなく左上の `` ` `` キー。全消去に割り当て済みなので `` ` `` `~` は入力不可

理由は @docs/design.md に書いてある。

## 環境依存のもの

`contents/` と生成物は git 管理外。クローン直後は `contents/print.md.example` →
`contents/print.md` のコピーが必要(無いと `npm run gen` が止まる)。
プリンタの設定ファイルは**無い**(USB プリンタクラスを自動検出)。

## 時刻

時刻機能は無い(RTC 非搭載のため `{{time}}` ごと廃止)。

## 診断

印刷トラブル時は `scan/`(ディスクリプタビューア)と @docs/design.md の
「診断ツール」の切り分け手順を使う。
