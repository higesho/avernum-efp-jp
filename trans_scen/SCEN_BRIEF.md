# シナリオデータ（AScenData.dat）の文字列の翻訳手順

PC用RPG「Avernum: Escape From the Pit」の、町や野外エリアのデータに入っている文字列を日本語にします。
作業フォルダ: `L:\SteamLibrary\steamapps\common\Avernum Escape From the Pit\_jp_mod`（以下 `_jp_mod`）

## 最初に読むもの
1. `_jp_mod\trans\STYLE.md` — 翻訳方針（特に 3章の記号、5章の固有名詞、6章の共通語彙、7章の決定事項）
2. `_jp_mod\trans_scen\jobs\<担当名>.json` — `glossary`（対訳表。**ここにある語は必ずこの訳を使う**）
3. `_jp_mod\trans_scen\chunks\<担当名>.json` — 翻訳する文字列。各要素: `id`、`kind`、`en`（原文）、`max_bytes`（上限。全角1文字＝2バイト、半角1文字＝1バイト）

## 種類ごとの訳し方
| kind | 画面での使われ方 | 訳し方 |
|---|---|---|
| town name / outdoor name | 町・野外区画の名前。「入場: 〜」の後ろや地図に出る | 地名。対訳表に従う（例: Fort Avernum →「アヴァーナム砦」）。**上限が小さい（町名は全角12文字、区画名は14文字）** |
| town area / outdoor area | その場所に入ったときに出る場所の名前（「Small Park」「Winding Tunnel」） | 体言止めの短い地名・部屋名 |
| town label / outdoor label | 地図上のラベル（「Job Board」「Trapdoor to Sewers」） | 体言止めの短い語 |
| town sign / outdoor sign | 看板・張り紙・壁の文字を読んだときの文 | 看板らしい文。店名・告知は体言止めや です・ます、落書きは原文の調子に合わせる |

- **`max_bytes` を必ず守る**。超えるときは言い換えて短くする。
- `|` は改行の記号。原文と同じ数を同じ位置関係で残す（`||` は空行）。
- 原文の `_` で囲まれた部分は「」にする。`_` を残さない。
- 店の名前は、本文の翻訳で使った店名に合わせる（例: Anastasia's Armor →「アナスタシアの防具店」、Efram's Forge →「エフラムの鍛冶場」のように「〜の〜店」「〜の鍛冶場」）。
- 次の地名は exe の翻訳で決まっているので合わせる: Healing Monastery＝治癒の僧院、Gemstone Mine＝宝石鉱山、Sathis's Shop＝サティスの店、
  Shaynee's Store＝シェイニーの店、Dharmon Armor＝ダーモン防具店、Silverlocke's Hut＝シルヴァーロックの小屋。
- わざと綴りを間違えた落書き（ゴブリンやオーガの看板など: "we will eet you if we donut like yu"）は、ひらがな多めのたどたどしい日本語にする。
- 方角: North＝北、South＝南、East＝東、West＝西。Stairs/Stairway＝階段、Ladder＝はしご、Trapdoor＝落とし戸、Sewers＝下水道。

## 出力と検査
1. `_jp_mod\trans_scen\ja\<担当名>.json` に UTF-8 の JSON で書く（Write ツール）: `{"name0001": "訳", ...}`。キーは `id`。全 `id` を含める。
2. Bash で検査する:
   `cd "/l/SteamLibrary/steamapps/common/Avernum Escape From the Pit/_jp_mod" && python -I tools/scen_tool.py check trans_scen <担当名>`
   エラー（E）が0になるまで直す。警告（W）も、訳し忘れや対訳表の不使用なら直す。
3. 最後に、処理件数、残った警告、自分で決めた訳語、判断に迷った箇所（最大5件）を短く報告する。
