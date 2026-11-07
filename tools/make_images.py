"""Redraw the English text in the Avernum UI images in Japanese.

usage:
    python -I make_images.py ORIGINAL_DIR OUTPUT_DIR [PREVIEW_DIR] [NAME ...]

ORIGINAL_DIR: the untouched images (_backup_original/Avernum Graphics Core).
Each image keeps its size, format and (for 8-bit BMPs) its palette; only the boxes
listed in SPEC are erased and redrawn, so clickable areas stay where they were.
"""
import os
import sys

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONTS = 'C:/Windows/Fonts/'
MINCHO_B = FONTS + 'yumindb.ttf'       # Yu Mincho Demibold
GOTHIC_B = FONTS + 'YuGothB.ttc'       # Yu Gothic Bold
UD_GOTHIC_B = FONTS + 'BIZ-UDGothicB.ttc'

INK = (24, 16, 12)          # black-brown text of the table images
RED_INK = (92, 20, 16)      # dark red of the tutorial images
STONE_INK = (40, 32, 30)    # stats screen labels

# erase: 'clear' (transparent), 'fill' (median border colour), 'inpaint' (stone texture)
# text entries: (box, lines, font, max_size, colour, align)
SPEC = {
    'G158.png': {
        'erase': 'clear-all',
        'style': 'gold',
        'texts': [
            ((15, 12, 318, 43), ['新しいゲーム'], MINCHO_B, 30, None, 'center'),
            ((61, 55, 273, 87), ['ロード'], MINCHO_B, 30, None, 'center'),
            ((11, 115, 324, 146), ['キャラクターエディター'], MINCHO_B, 30, None, 'center'),
            ((54, 158, 281, 189), ['操作説明'], MINCHO_B, 30, None, 'center'),
            ((92, 201, 243, 239), ['設定'], MINCHO_B, 30, None, 'center'),
            ((30, 244, 304, 275), ['勲章と統計'], MINCHO_B, 30, None, 'center'),
            ((124, 287, 209, 325), ['終了'], MINCHO_B, 30, None, 'center'),
        ]},
    'G203.png': {
        'erase': 'fill', 'pad': 3, 'fill_colour': (0, 0, 0, 0),
        'texts': [((x0, y0 - 2, x1, y1 + 2), [t], MINCHO_B, 17, INK, 'center') for (x0, y0, x1, y1), t in [
            ((20, 7, 258, 24), '兵士'), ((20, 36, 258, 53), '狂戦士'), ((20, 65, 258, 82), '司祭'),
            ((20, 93, 258, 110), '妖術師'), ((20, 122, 258, 143), 'ならず者'), ((20, 151, 258, 168), '射手'),
            ((20, 180, 258, 197), '反逆者'), ((20, 209, 258, 230), '田舎魔術師'), ((20, 237, 258, 254), 'シャーマン'),
            ((20, 266, 258, 283), 'カスタム'),
            ((285, 7, 551, 24), 'ゲームに戻る'), ((285, 65, 551, 82), 'ゲームをセーブ'),
            ((285, 93, 551, 110), 'セーブデータをロード'), ((285, 152, 551, 172), '設定'),
            ((285, 180, 551, 201), 'キーボードショートカット'), ((285, 209, 551, 226), 'アヴァーナムについて'),
            ((285, 237, 551, 254), 'メイン画面に戻る'), ((285, 266, 551, 287), '終了')]]},
    'G202.bmp': {
        'erase': 'fill', 'pad': 3,
        'texts': [((3, y0 - 1, 219, y1 + 1), [t], MINCHO_B, 16, INK, 'left') for (y0, y1), t in [
            ((5, 23), '新しい職業'), ((31, 45), '名前の変更'), ((58, 72), '能力値の編集'),
            ((84, 98), '特性の編集'), ((111, 125), '削除'), ((137, 151), '新しいキャラクター')]]},
    'G167.png': {
        'erase': 'fill', 'clear_rects': [(206, 10, 385, 145)],
        'texts': [((214, y0 - 1, 382, y1 + 1), [t], GOTHIC_B, 13, INK, 'left') for (y0, y1), t in [
            ((21, 33), 'A - 地形'), ((41, 53), 'B - メンバー一覧'), ((61, 76), 'C - 自動地図'),
            ((81, 94), 'D - ゲームボタン'), ((102, 114), 'E - サイドボタン'), ((122, 134), 'F - テキスト欄')]]},
    'G168.bmp': {
        'erase': 'fill',
        'texts': [
            ((84, 15, 172, 43), ['既定の攻撃', '（sで切替）'], GOTHIC_B, 12, INK, 'left'),
            ((84, 52, 180, 79), ['魔術・祈祷', '（m / p）'], GOTHIC_B, 12, INK, 'left'),
            ((84, 87, 172, 114), ['戦闘修練', '（d）'], GOTHIC_B, 12, INK, 'left'),
            ((211, 17, 257, 33), ['拾う（g）'], GOTHIC_B, 12, INK, 'left'),
            ((211, 47, 257, 75), ['日誌と', 'クエスト'], GOTHIC_B, 12, INK, 'left'),
            ((299, 12, 369, 40), ['クイック呪文', '（F5〜F8）'], GOTHIC_B, 12, INK, 'left'),
            ((299, 47, 369, 75), ['クイック使用', '（F9〜F12）'], GOTHIC_B, 12, INK, 'left'),
        ]},
    'G169.bmp': {
        'erase': 'fill',
        'texts': [
            ((4, 2, 191, 20), ['既定の攻撃ボタン：'], MINCHO_B, 16, RED_INK, 'center'),
            ((44, 86, 90, 100), ['近接'], GOTHIC_B, 11, RED_INK, 'center'),
            ((91, 86, 143, 100), ['飛び道具'], GOTHIC_B, 11, RED_INK, 'center'),
        ]},
    'G196.bmp': {
        'erase': 'fill',
        'texts': [
            ((6, 4, 142, 27), ['戦い方'], MINCHO_B, 20, RED_INK, 'left'),
            ((12, 28, 210, 69), ['1. このボタンで攻撃の', '種類を切り替えます', '（近接武器か飛び道具）'], GOTHIC_B, 13, RED_INK, 'left'),
            ((194, 86, 330, 118), ['2. 敵をクリックすると', '攻撃します。'], GOTHIC_B, 13, RED_INK, 'left'),
        ]},
    'G216.bmp': {
        'erase': 'fill',
        'texts': [
            ((6, 3, 197, 47), ['1. このボタンで攻撃の', '種類を切り替えます', '（近接武器か飛び道具）'], GOTHIC_B, 13, RED_INK, 'left'),
        ]},
    'G288.png': {
        'erase': 'fill',
        'style': 'blue',
        'texts': [((12, 2, 380, 30), ['冒険は続く……'], MINCHO_B, 24, None, 'left')]},
    'G142.jpg': {
        'erase': 'inpaint',
        'texts': [((x0, y0, x1, y1), [t], MINCHO_B, 15, STONE_INK, al) for (x0, y0, x1, y1), t, al in [
            ((92, 15, 140, 34), 'レベル', 'center'), ((166, 15, 260, 35), '経験値', 'center'),
            ((104, 81, 192, 100), '能力値', 'center'),
            ((40, 122, 139, 140), '筋力:', 'right'), ((40, 144, 139, 162), '器用さ:', 'right'),
            ((40, 164, 139, 183), '知力:', 'right'), ((40, 185, 139, 204), '持久力:', 'right'),
            ((46, 224, 107, 243), '体力', 'center'), ((174, 224, 283, 243), '呪文力', 'center'),
            ((101, 286, 201, 305), '耐性', 'center'),
            ((40, 326, 105, 344), '防御:', 'right'), ((40, 347, 105, 365), '魔法:', 'right'),
            ((40, 368, 105, 386), '火炎:', 'right'), ((40, 389, 105, 407), '冷気:', 'right'),
            ((150, 326, 215, 344), '毒:', 'right'), ((150, 347, 215, 365), '酸:', 'right'),
            ((150, 368, 215, 386), '精神:', 'right'), ((150, 389, 215, 407), '呪い:', 'right')]]},
    'G128.png': {
        'erase': 'inpaint',
        'texts': [
            ((733, 6, 773, 24), ['地図'], MINCHO_B, 15, STONE_INK, 'center'),
            ((738, 306, 799, 324), ['メンバー'], MINCHO_B, 15, STONE_INK, 'center'),
            ((925, 310, 988, 329), ['戦闘'], MINCHO_B, 15, STONE_INK, 'center'),
            ((904, 346, 1008, 366), ['魔法・その他'], MINCHO_B, 15, STONE_INK, 'center'),
            ((928, 383, 975, 402), ['特性'], MINCHO_B, 15, STONE_INK, 'center'),
        ]},
    'G133.png': {
        'erase': 'inpaint',
        'texts': [
            ((45, 3, 77, 18), ['購入'], GOTHIC_B, 12, (60, 36, 20), 'center'),
            ((86, 3, 118, 18), ['購入'], GOTHIC_B, 12, (110, 100, 90), 'center'),
            ((2, 116, 40, 129), ['人間'], GOTHIC_B, 11, (255, 255, 255), 'center', 'outline'),
            ((45, 115, 90, 130), ['ネフィル'], GOTHIC_B, 11, (255, 255, 255), 'center', 'outline'),
            ((100, 116, 135, 129), ['スリス'], GOTHIC_B, 11, (255, 255, 255), 'center', 'outline'),
            ((147, 294, 216, 312), ['読み込み中'], MINCHO_B, 14, STONE_INK, 'center'),
        ]},
    'G188.bmp': {
        'erase': 'fill',
        'texts': [
            ((43, 11, 101, 24), ['ファイル'], GOTHIC_B, 11, RED_INK, 'center'),
            ((38, 46, 104, 59), ['操作説明'], GOTHIC_B, 11, RED_INK, 'center'),
            ((141, 11, 202, 24), ['世界地図'], GOTHIC_B, 11, RED_INK, 'center'),
            ((144, 46, 201, 59), ['編成'], GOTHIC_B, 11, RED_INK, 'center'),
            ((266, 23, 316, 46), ['戦闘の', '開始/終了'], GOTHIC_B, 11, RED_INK, 'center'),
        ]},
}


def fit_font(path, lines, box, max_size):
    w, h = box[2] - box[0], box[3] - box[1]
    for size in range(max_size, 7, -1):
        f = ImageFont.truetype(path, size)
        widths = [f.getbbox(t)[2] for t in lines]
        lh = size + max(1, size // 8)
        if max(widths) <= w and lh * len(lines) - (lh - size) <= h + 2:
            return f, size, lh
    f = ImageFont.truetype(path, 8)
    return f, 8, 9


def text_layer(size, box, lines, font_path, max_size, align):
    """Alpha mask (L) of the text placed in box."""
    f, s, lh = fit_font(font_path, lines, box, max_size)
    mask = Image.new('L', size, 0)
    d = ImageDraw.Draw(mask)
    total = lh * len(lines) - (lh - s)
    y = box[1] + (box[3] - box[1] - total) / 2
    for t in lines:
        bb = f.getbbox(t)
        tw = bb[2]
        if align == 'center':
            x = box[0] + (box[2] - box[0] - tw) / 2
        elif align == 'right':
            x = box[2] - tw
        else:
            x = box[0]
        d.text((x, y - bb[1] + (s - (bb[3] - bb[1])) / 2), t, font=f, fill=255)
        y += lh
    return mask


def border_colour(arr, box):
    x0, y0, x1, y1 = box
    edge = np.concatenate([arr[y0, x0:x1], arr[y1, x0:x1], arr[y0:y1, x0], arr[y0:y1, x1]])
    if (edge[:, 3] < 128).mean() > 0.5:      # transparent background
        return (0, 0, 0, 0)
    edge = edge[edge[:, 3] >= 128]
    return tuple(int(v) for v in np.median(edge, axis=0))


def gradient_fill(size, mask, top, bottom, box):
    w, h = size
    grad = Image.new('RGB', size)
    gd = ImageDraw.Draw(grad)
    y0, y1 = box[1], box[3]
    for y in range(h):
        t = min(1, max(0, (y - y0) / max(1, y1 - y0)))
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        gd.line([(0, y), (w, y)], fill=c)
    out = Image.new('RGBA', size, (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    return out


def render(name, src_dir):
    spec = SPEC[name]
    src = Image.open(os.path.join(src_dir, name))
    palette_img = src if src.mode == 'P' else None
    img = src.convert('RGBA')
    size = img.size
    arr = np.array(img)

    if spec['erase'] == 'clear-all':
        img = Image.new('RGBA', size, (0, 0, 0, 0))
    elif spec['erase'] == 'fill':
        pad = spec.get('pad', 0)
        out_arr = arr.copy()
        for t in spec['texts']:
            x0, y0, x1, y1 = t[0]
            x0, y0 = max(0, x0 - pad), max(0, y0 - pad)
            x1, y1 = min(size[0] - 1, x1 + pad), min(size[1] - 1, y1 + pad)
            c = spec.get('fill_colour') or border_colour(arr, (x0, y0, x1, y1))
            blk = arr[y0:y1 + 1, x0:x1 + 1]
            sub = (blk[:, :, :3].astype(int).sum(axis=2) < 300) & (blk[:, :, 3] > 128)
            grid_rows = sub.mean(axis=1) > 0.7     # table rules: keep them
            grid_cols = sub.mean(axis=0) > 0.7
            for yy in range(y0, y1 + 1):
                if grid_rows[yy - y0]:
                    continue
                for xx in range(x0, x1 + 1):
                    if not grid_cols[xx - x0]:
                        out_arr[yy, xx] = c
        img = Image.fromarray(out_arr, 'RGBA')
    elif spec['erase'] == 'inpaint':
        alpha = np.array(img)[:, :, 3]
        rgb = cv2.cvtColor(np.array(img.convert('RGB')), cv2.COLOR_RGB2BGR)
        m = np.zeros(rgb.shape[:2], np.uint8)
        gray = cv2.cvtColor(rgb, cv2.COLOR_BGR2GRAY)
        for t in spec['texts']:
            x0, y0, x1, y1 = t[0]
            light = len(t) > 6 and t[6] == 'outline'
            sub = gray[y0:y1, x0:x1]
            hit = (sub > 200) | (sub < 60) if light else (sub < 120)
            m[y0:y1, x0:x1] = hit.astype(np.uint8) * 255
        m = cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=1)
        rgb = cv2.inpaint(rgb, m, 4, cv2.INPAINT_TELEA)
        out = np.dstack([cv2.cvtColor(rgb, cv2.COLOR_BGR2RGB), alpha])
        img = Image.fromarray(out.astype(np.uint8), 'RGBA')

    for x0, y0, x1, y1 in spec.get('clear_rects', []):
        ImageDraw.Draw(img).rectangle([x0, y0, x1 - 1, y1 - 1], fill=(0, 0, 0, 0))
    style = spec.get('style')
    for t in spec['texts']:
        box, lines, font_path, max_size, colour, align = t[:6]
        mask = text_layer(size, box, lines, font_path, max_size, align)
        if len(t) > 6 and t[6] == 'outline':
            edge = mask.filter(ImageFilter.MaxFilter(3))
            img.alpha_composite(Image.composite(Image.new('RGBA', size, (0, 0, 0, 255)),
                                                Image.new('RGBA', size, (0, 0, 0, 0)), edge))
            img.alpha_composite(Image.composite(Image.new('RGBA', size, colour + (255,)),
                                                Image.new('RGBA', size, (0, 0, 0, 0)), mask))
            continue
        if style == 'gold':
            # dark outline + shadow, then gold gradient (colours sampled from the original menu)
            shadow = mask.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(1.5))
            img.alpha_composite(Image.composite(Image.new('RGBA', size, (20, 12, 4, 230)),
                                                Image.new('RGBA', size, (0, 0, 0, 0)), shadow.point(lambda v: min(255, v * 2))),
                                (1, 2))
            edge = mask.filter(ImageFilter.MaxFilter(3))
            img.alpha_composite(Image.composite(Image.new('RGBA', size, (58, 40, 18, 255)),
                                                Image.new('RGBA', size, (0, 0, 0, 0)), edge))
            img.alpha_composite(gradient_fill(size, mask, (250, 236, 190), (176, 136, 70), box))
        elif style == 'blue':
            img.alpha_composite(gradient_fill(size, mask, (110, 120, 200), (40, 44, 110), box))
        else:
            layer = Image.new('RGBA', size, colour + (255,))
            img.alpha_composite(Image.composite(layer, Image.new('RGBA', size, (0, 0, 0, 0)), mask))

    if palette_img is not None:
        out = img.convert('RGB').quantize(palette=palette_img, dither=Image.Dither.NONE)
    elif name.lower().endswith('.jpg'):
        out = img.convert('RGB')
    else:
        out = img
    return out


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    src_dir, out_dir = argv[0], argv[1]
    preview = argv[2] if len(argv) > 2 and not argv[2].startswith('G') else None
    names = [a for a in argv[2:] if a.startswith('G')] or list(SPEC)
    for name in names:
        out = render(name, src_dir)
        p = os.path.join(out_dir, name)
        if name.lower().endswith('.jpg'):
            out.save(p, quality=95)
        else:
            out.save(p)
        if preview:
            pv = out.convert('RGBA')
            bg = Image.new('RGBA', pv.size, (90, 90, 90, 255))
            bg.alpha_composite(pv)
            bg.convert('RGB').save(os.path.join(preview, name.rsplit('.', 1)[0] + '_jp.png'))
        print('wrote', p)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
