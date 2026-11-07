# Avernum: Escape From the Pit 日本語表示MOD（試作）

## 仕組み
- `dsound.dll`（ゲームフォルダに置く）が起動時に読み込まれ、`Avernum.exe` の
  「枠内にテキストを描く関数」(VA 0x493e80) を自前の実装に差し替えます。
- テキストは EUC-JP（既定）または UTF-8 として解釈します（`avernum_jp.ini` の `[text] encoding`）。英数字は従来どおりゲーム内フォント
  （`G290.png`）で描き、日本語はWindowsのフォント（既定: メイリオ）で
  文字画像を作り、ゲーム本来の描画関数で描きます。
- 日本語はどの文字の間でも改行できます（簡易禁則処理あり）。`|` は強制改行です。
- 日本語の字の大きさは、英字フォントの高さと行の高さのうち小さいほうに合わせます（`line_gap` で行間を確保）。
  小さいフォント（行の高さ13px）では英字の枠が14pxあり、そのまま描くと上下の行が重なるためです。
- `DirectSoundCreate` はシステムの dsound.dll にそのまま渡すため、音はそのまま出ます。

## 使い方
1. `build.bat` で `dsound.dll` をビルドする（VS2022 Build Tools, x86）
2. `dsound.dll` をゲームフォルダ（Avernum.exe と同じ場所）にコピーする
3. 翻訳ファイルを UTF-8 で書き、`python -I tools/jpconv.py 翻訳フォルダ "../Avernum Files/Scripts"` で EUC-JP に変換してゲームに入れる（文字数の上限も同時に検査する）
4. **Steamから起動する**（exeを直接起動すると SteamAPI_Init エラーになる。これは元からの仕様）

フォントなどの設定は、ゲームフォルダの `avernum_jp.ini` で変更できます。
動作ログは `avernum_jp.log` に出力されます。

## 元に戻す
- ゲームフォルダの `dsound.dll`、`avernum_jp.ini`、`avernum_jp_exe.tsv` を削除する
- `_backup_original/Scripts/*.txt` を `Avernum Files/Scripts/` に、`_backup_original/AScenData.dat` を `Avernum Files/` に、`_backup_original/Avernum Graphics Core/*` を `Avernum Files/Avernum Graphics Core/` に上書きコピーする
  （もしくは Steam の「ゲームファイルの整合性を確認」を実行する）

## 調べてわかったこと（主なアドレス、ASLR の影響を除いたVA）
| VA | 内容 |
|---|---|
| 0x493e80 | 枠内テキスト描画関数（ecx=モード 0:折返し/1:中央/2:1行、edx=文字列、引数: フォントコード, r, g, b, rect{top,left}, rect{bottom,right}）。ここを差し替える |
| 0x496500 | ASCII 1行描画関数（cdecl: str, r, g, b。描画位置は 0x6a9b94/0x6a9b98） |
| 0x406a90 | テクスチャ矩形の描画関数（edi=テクスチャオブジェクト、7引数） |
| 0x5ec0c8 + n*0x310 | フォント5種の字形テーブル |
| 0x5ed020 | 文字→字形番号の変換表（short[256]） |
| 0x6a9da0 | フォントのテクスチャオブジェクト（0x170バイト、+0x2c=GL ID） |
| 0x5ec09c | 現在バインド中のGLテクスチャのキャッシュ |

## 文字列の長さの上限（exe の解析と実機テストで確認済み）
| 対象 | 上限 | EUC-JP での全角文字数 | 超えた場合 |
|---|---|---|---|
| スクリプトとテキストライブラリの `"..."` 1つ | 510バイト | 255文字 | **そのファイル全体が読み込まれず、全文字列が空になる**（エラー表示なし）。実機で確認済み |
| `set_name(n, "...")` のクリーチャー名 | 19バイト | 9文字 | 固定長の領域にそのままコピーされ、メモリが壊れる |
| `cr_name`（定義ファイル） | 19バイト | 9文字 | 切り捨て |
| `it_name` | 25バイト | 12文字 | 切り捨て |
| `ab_name`、`ob_name` | 19バイト（安全側の値） | 9文字 | 切り捨て |
| 頭上に浮かぶテキスト | 39バイト | 19文字 | 表示されない |

- UTF-8（1文字3バイト）だと、クリーチャー名は6文字、アイテム名は8文字しか入らない。そのため EUC-JP を標準にした。
- EUC-JP は2バイトとも 0xA1 以上で、ゲームが特別扱いする ASCII 記号（`_`、`|` など）と衝突しない。英語原文の MacRoman 文字は単独の上位バイトでしか現れないので、区別できる。
- 英語原文の最長は 424 バイト（textdocs）。日本語訳は文字数がおよそ半分になるので、EUC-JP ならまず収まる。UTF-8 だと 300 バイトを超える英文（20件程度）があふれる見込み。
- `tools/jpconv.py` が以上をまとめて検査する。

## スクリプト読み込みの修正
日本語の多いスクリプトを読み込むと、ゲームの字句数の見積もり（0x46ecc0）が16ビットを超えて強制終了します。
DLL は 0x46ed1a にパッチを当て、0x80 以上のバイトを英字と同じ扱いにして見積もりを正しくしています。
また、CRT のエラー表示（MessageBoxA/W）の直前に、スタック上の exe 内アドレスを `avernum_jp.log` に記録します。

`print_named_str(who, "文")` は exe（0x47bcfc）で `sprintf(buf, "%s %s", 名前, 文)` としてつながります。
DLL はこの呼び出し（0x47bd02）を差し替え、文が日本語で始まるときは空白を入れずに「名前が〜」とつなぎます
（バッファは ebp-108h から260バイトで、末尾に /GS クッキーがあるため、DLL は256バイトまでしか書きません）。

## 本文の翻訳
手順とツールは [TRANSLATION.md](TRANSLATION.md) を参照。

## 未対応・今後の課題
- スクリプト本文は全訳済み（[TRANSLATION.md](TRANSLATION.md)）
- exe 内のUI文字列は814件を翻訳済み（[EXE_STRINGS.md](EXE_STRINGS.md)）
- `Avernum Files/AScenData.dat` の文字列（町・区画の名前、エリア名、看板、地図ラベル）は翻訳済み。形式と手順は `tools/scen_extract.py`・`tools/scen_tool.py`、`trans_scen/SCEN_BRIEF.md`。
  元のファイルは `_backup_original/AScenData.dat`。元に戻すときはこれをコピーする
- 画像の文字は13枚を日本語化済み（`tools/make_images.py`。元画像は `_backup_original/Avernum Graphics Core`、生成物は `img_out/`）:
  G158 タイトルメニュー、G203/G202 メニュー表、G142 ステータス画面、G128 パネル見出し・タブ、G133 購入ボタン・種族名・読み込み中、
  G167/G168/G169/G188/G196/G216 操作説明、G288 続編の告知。
  世界地図3枚（G200 と、その左右半分の G176・G177）の地名35か所も、ゲーム内の訳に合わせて日本語化済み（`tools/make_maps.py`）。
  未対応: 読み込み画面の挿絵（G400〜G427）の書き文字
- 文字列を組み合わせるメッセージ（"You find %s" のような形式）で、組み立て用バッファの大きさは未調査
- 定義ファイルの `ab_name`、`ob_name` の正確な上限は未確定（19バイトは安全側の値）
- 0x493e80 以外の経路で描かれるテキスト（名前入力欄など）があるかどうかは未調査
