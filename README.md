# Avernum: Escape From the Pit 日本語化パッチ

Spiderweb Software の RPG「Avernum: Escape From the Pit」（Steam 版）を日本語で遊べるようにする非公式パッチです。
会話・本・アイテム・画面の文字・看板・地図まで、ゲーム内のほぼすべての文章を日本語にします。

- **紹介ページ**: https://higesho.github.io/avernum-efp-jp/
- **ダウンロード**: [Releases](https://github.com/higesho/avernum-efp-jp/releases/latest) の `AvernumEFP_JP_<版>.zip`

![世界地図](docs/img/worldmap.jpg)

## 使い方
1. ZIP の中身（`dsound.dll`、`avernum_jp.ini`、`avernum_jp` フォルダ）をゲームのフォルダ（`Avernum.exe` のある場所）にコピーする
2. Steam からゲームを起動する

元に戻すときは、コピーしたファイルとフォルダを削除します。詳しくは ZIP 内の `avernum_jp/README.txt` か紹介ページを見てください。

動作確認は Steam 版（Windows）で行っています。GOG 版など他のストアの版でも動くように作ってありますが未確認です（対応できない版では日本語化を自動で止め、英語のまま起動します）。ゲームの元のファイルは書き換えません。初回の起動時に DLL が手元のファイルと差分
（`avernum_jp/jp_patch.bin`）から日本語版を `avernum_jp/cache/` に作り、ゲームが読むファイルをそちらに差し替えます。

## 翻訳の規模
| 対象 | 件数 |
|---|---|
| 会話・本・イベントの文（スクリプト） | 11,978 |
| 人物・アイテム・呪文などの名前 | 1,685 |
| 地名・看板・場所の説明（シナリオデータ） | 1,121 |
| 画面・戦闘メッセージ（実行ファイル内の文字列） | 814 |
| 文字入りの画像・地図 | 16 枚 |

## リポジトリの中身
| パス | 内容 |
|---|---|
| `avernum_jp.c`, `dsound.def`, `build.bat` | 日本語表示 DLL（DirectSound の代理 DLL）。VS2022 Build Tools（x86）でビルド |
| `exe_strings.tsv` | 実行ファイル内の文字列の訳（ゲームには `avernum_jp_exe.tsv` として置く） |
| `trans/` | スクリプト本文の訳（`ja/`）、用語集、口調メモ、文体の決まり（`STYLE.md`） |
| `trans_exe/`, `trans_scen/` | 実行ファイルの文字列とシナリオデータの訳 |
| `tools/` | 抽出・検査・組み込み・画像生成・パッチ作成のツール（Python）。`gen_patterns.py` は DLL が書き換え位置を探すためのパターンを作る |
| `dist_src/` | 配布 ZIP の説明書と設定ファイル |
| `docs/` | 紹介ページ（GitHub Pages） |

開発の詳細は [DEVELOPMENT.md](DEVELOPMENT.md)（DLL のしくみ、調べたアドレス、文字数の上限）、
[TRANSLATION.md](TRANSLATION.md)（翻訳の手順）、[EXE_STRINGS.md](EXE_STRINGS.md)（実行ファイルの文字列）にあります。

ゲームから抽出した英語の原文や元の画像は、このリポジトリに含めていません。翻訳を組み込み直すには、
手元のゲームのファイルから `tools/` のスクリプトで抽出してください。

## 配布 ZIP の作り方
```
python -I tools/build_release.py <元のファイルのフォルダ> <日本語化したファイルのフォルダ> <出力先> v0.9.2 https://higesho.github.io/avernum-efp-jp/
```
`tools/make_patch.py` が元のファイルとの差分（`avernum_jp/jp_patch.bin`）を作ります。文章は差分として翻訳部分だけを、
画像は元の画像との差として持つため、ZIP 単体ではゲームの内容を取り出せません。

## 免責
本パッチは非公式のファン翻訳であり、Spiderweb Software とは関係ありません。使用は自己責任でお願いします。
Avernum は Spiderweb Software の商標です。
