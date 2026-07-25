# cardputer-adv-printer

M5Stack Cardputer ADV から USB 接続のサーマルプリンタ(Phomemo M220)へ、
Markdown テンプレートに**キーボードで打った文字を差し込んで**印刷します。

テンプレート(ロゴ画像や定型文)は `contents/print.md` に書いてビルド時に埋め込み、
`{{input}}` と書いた場所に、その場でキーボード入力した文字列が入ります。
イベント受付で名前入りのチケットを刷るような用途を想定しています。

- 画像(PNG/JPEG)は自動で印字幅に縮小し、白黒 2 値に変換
- 入力した文字は画面に表示され、Enter で印刷(Shift で大文字・記号も入力可)
- del で 1 文字削除、esc(左上のキー)で全消去
- **印刷が正常に終わると入力は自動でクリア**され、次の入力を待つ
- 画面右上にバッテリー残量(電圧から推定。充電中かどうかは表示できません)
- プリンタの設定ファイルは不要(USB プリンタクラスのデバイスを自動検出)

## 必要なもの

- M5Stack Cardputer ADV([製品ページ](https://docs.m5stack.com/en/core/Cardputer-Adv))
- USB 接続のサーマルプリンタ(動作確認済み: Phomemo M220。USB プリンタクラス
  (class 07)で列挙され、Phomemo 系プロトコルを話す機種なら動く見込み)
- **USB-A(メス)→ USB-C(オス)変換コネクタ** と **USB A-C ケーブル**
  (Cardputer にプリンタをつなぐ用)
- Node.js と `arduino-cli`(→ [環境セットアップ](docs/setup.md))

## はじめかた

環境が未構築なら先に [docs/setup.md](docs/setup.md) を済ませてください。

```powershell
npm install
```

### 1. テンプレートを用意する

```powershell
copy contents\print.md.example contents\print.md
```

`contents/print.md` を編集します。`{{input}}` と書いた場所にキーボード入力が入ります。
画像は `contents/images/` に置いてください。書式の詳細は [docs/content.md](docs/content.md) を参照。

### 2. 書き込む

Cardputer を USB ケーブルで PC につないで:

```powershell
npm run flash
```

コンテンツの生成・コンパイル・書き込みまで一気に行います。ポートは自動検出します。

## 使いかた

1. Cardputer の**電源は入れたまま** PC からケーブルを抜く
2. 変換コネクタを Cardputer に挿し、A-C ケーブルでプリンタとつなぐ
3. プリンタの電源を入れる
4. 文字を打って **Enter** → テンプレートに差し込まれて印刷され、入力はクリアされる

| キー | 動作 |
|---|---|
| 文字キー | 入力(Shift 併用で大文字・記号) |
| Enter | 印刷(成功すると入力クリア) |
| del | 1 文字削除 |
| esc(左上 `` ` `` キー) | 全消去 |

入力できるのはキーボードにある ASCII 文字のみです(日本語入力はありません。
テンプレート側には日本語を書けます)。

## ⚠️ USB の流儀(重要)

ESP32-S3 は **書き込み用 USB(Serial/JTAG)とプリンタ接続用 USB(OTG ホスト)が排他**です。

- **一度印刷すると**(= USB ホスト開始)、PC からの書き込みが効かなくなる
- 戻すには: **ケーブルを抜いた状態で電源スイッチを切って入れ直す** → PC に接続

「書き込めない・ポートが見えない」ときは、まずこの電源入れ直しを疑ってください。

## コマンド一覧

| コマンド | 内容 |
|---|---|
| `npm run flash` | 生成 → コンパイル → 書き込み |
| `npm run build` | 生成 → コンパイル(書き込みなし) |
| `npm run gen` | 生成のみ |
| `npm run scan` | USB ディスクリプタビューアを書き込み(プリンタ調査用) |

ポートを明示指定する場合は `npm run flash -- COM10` のように渡します。

## git 管理について

印刷テンプレート(`contents/`)はイベントごとのデータなのでリポジトリに含めていません。
`.example` ファイルをコピーして使ってください。

## ドキュメント

- [docs/setup.md](docs/setup.md) — arduino-cli の環境構築手順
- [docs/content.md](docs/content.md) — print.md の書式、`{{input}}`、画像の扱い、階調モード
- [docs/design.md](docs/design.md) — 設計と、触ると壊れる箇所(**編集前に必読**)
