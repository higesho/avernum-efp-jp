# スクリプト本文の翻訳手順

ゲームのスクリプト（`Avernum Files/Scripts/*.txt`、444ファイル）の本文を、対訳表と口調メモで統一しながら翻訳します。
翻訳そのものは Claude Haiku のサブエージェントが担当し、検査と書き戻しはツールで行います。

## ファイル構成（`trans/`）
| パス | 内容 |
|---|---|
| `STYLE.md` | 翻訳方針（文体、記号、長さの制限、固有名詞の規則、共通語彙） |
| `TRANSLATOR_BRIEF.md` | 翻訳担当（Haiku）への作業手順 |
| `units/<file>.json` | 抽出した翻訳単位（全13,663件、約120万字） |
| `chunks/<file>__NN.json` | 翻訳単位を約6,000字ごとに分けたもの（464チャンク） |
| `jobs.json` | チャンクを担当ごと（約3万字）にまとめた一覧（48担当） |
| `jobs/<担当名>.json` | 担当に渡す情報（チャンク一覧、関係する対訳表、口調メモ） |
| `ja/<file>__NN.json` | 訳文 `{"literal番号": "訳", "_voices": {...}}` |
| `glossary_candidates.json` | 対訳表の候補（2,001語、種類と文字数制限つき） |
| `gloss_out/*.json` | Haiku が訳した対訳表。`zz_fix.json` は手による修正で、常に優先される |
| `glossary.json` | 統合した対訳表（`merge_glossary.py` が作る） |
| `voices.json` | 人物ごとの口調メモ（`merge_voices.py` が `_voices` から集める） |

## 流れ
1. `python -I tools/extract.py ../_backup_original/Scripts trans` — 文字列を抽出（名前欄、会話、ライブラリ、print_str など）
2. `python -I tools/make_chunks.py trans` — チャンクと担当に分ける。名前欄（cr_name、it_name など）はチャンクに入れず、対訳表から埋める
3. 対訳表: `python -I tools/glossary_candidates.py trans` → Haiku が `gloss_out/` に訳す → `python -I tools/merge_glossary.py trans`
   （文字数超過と表記ゆれを報告する。直すときは `gloss_out/zz_fix.json` に追記）
4. 担当の準備: `python -I tools/prep_job.py trans <担当名> <チャンク...>`
5. Haiku に `TRANSLATOR_BRIEF.md` と担当名を渡して翻訳させる。担当者は `tools/check_trans.py` で自分の出力を検査し、エラーを直す
6. `python -I tools/check_trans.py trans` — 全体の検査（記号、空白、長さ、EUC-JP、対訳表の使用）
7. `python -I tools/merge_voices.py trans` — 口調メモを集め、次の担当に引き継ぐ
8. `python -I tools/build_scripts.py ../_backup_original/Scripts trans "../Avernum Files/Scripts"` — EUC-JP でゲームに書き戻す
   （訳のないファイルは原文のまま書き出す。切り分け用に `--no-names`、`--no-body` がある）

## 試験翻訳（済み）
序盤の3担当（導入スライド・ヒント・ダイアログ文、アヴァーナム砦の会話、砦のスクリプト）の704件。
検査エラー0。ゲーム内で、導入スライド、チュートリアル、イベント会話の表示を確認済み。

## 進み具合
- **スクリプト本文は全訳済み（2026-11-07）**: 464チャンク・11,978件、名前欄1,685件。Haiku の担当49本（試験3＋47）で翻訳。
- 最終検査: エラー0、警告29（すべて意図的: クレジットの人名・合言葉 Kai・ゲーム名を英字のまま残したもの、マニュアルの「魔術」「祈祷」など）。
- 字句数の見積もり（DLL のパッチ適用後）は最大 29,818（avitemschars.txt、上限 32,765）。
- ゲーム内の確認: 第1陣までは町の読み込みとイベント会話の表示を確認。全訳を入れた状態の起動確認は未実施。
- 人物の口調メモ 366 人分（`trans/voices.json`）、訳し済みの決まった文 10,484 件（`trans/known.json`）。

## 仕上げ
- `python -I tools/polish.py trans` — 途中で決めた規則をまとめて適用する（台詞の句点、選択肢の語尾、操作説明の文体、表記）。
- 訳を直したら `unify_dupes.py` → `check_trans.py` → `build_scripts.py` の順に実行する。

## 注意
- スクリプトの読み込み時、ゲーム（0x46ecc0）は字句数を見積もって配列を確保する。英字以外のバイトを1字句ずつ数えるため、
  日本語の多い会話ファイルで16ビットの上限を超えて強制終了していた。DLL が見積もりを修正している（0x46ed1a のパッチ）。
- `print_named_str` は exe（0x47bcfc）で `"%s %s"` により「名前 文」とつながる。DLL がこの sprintf を差し替え、文が日本語で始まるときは空白を入れない（ゲーム内で「ブリッサが応じない。」と表示されることを確認済み）。続き行の `print_str_color("  …")` は先頭の空白2つが字下げとして表示される。
