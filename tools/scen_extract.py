"""Extract the player-visible strings of Avernum Files/AScenData.dat.

usage:
    python -I scen_extract.py ORIGINAL_ASCENDATA OUT_DIR

File layout (from Avernum.exe 0x430240..0x4304d6):
    0x000000  header, 0x174 bytes
    0x000174  42 outdoor sections, 0x5218 bytes each
    0x0d7964  100 towns, 0x1a7c8 bytes each
String fields used here (record-relative offset, count, stride, field size):
    outdoor  name  +0x0008  1        30      area names +0x35c0 16 x 0x100   signs +0x29b8 8 x 0x100
             labels +0x5060 11 x 0x28
    town     name  +0x0002  1        26      area names +0x55e0 16 x 0x100   signs +0x6658 15 x 0x100
             labels +0x7ff8 12 x 0x28        labels2 +0x1018c 10 x 0x28
(designer notes at outdoor +0x4610 / town +0x76a8 and script names are not shown in game.)

Writes OUT_DIR/units.json: [{"id", "kind", "en", "max_bytes", "offsets": [file offsets]}]
with identical (kind, English) entries merged.
"""
import json
import os
import re
import sys

sys.stdout.reconfigure(encoding='utf-8')

OUT = (0x174, 0x5218, 42)
TOWN = (0xd7964, 0x1a7c8, 100)
# kind, record set, first offset, count, stride, field size, max bytes for a translation
FIELDS = [
    ('outdoor name', OUT, 0x8, 1, 0, 30, 29),
    ('outdoor area', OUT, 0x35c0, 16, 0x100, 0x100, 60),
    ('outdoor sign', OUT, 0x29b8, 8, 0x100, 0x100, 255),
    ('outdoor label', OUT, 0x5060, 11, 0x28, 0x28, 39),
    ('town name', TOWN, 0x2, 1, 0, 26, 25),
    ('town area', TOWN, 0x55e0, 16, 0x100, 0x100, 60),
    ('town sign', TOWN, 0x6658, 15, 0x100, 0x100, 255),
    ('town label', TOWN, 0x7ff8, 12, 0x28, 0x28, 39),
    ('town label', TOWN, 0x1018c, 10, 0x28, 0x28, 39),
]
TEXT = re.compile(rb'[\x20-\x7e]*[A-Za-z]{2}[\x20-\x7e]*')


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    src, out = argv
    d = open(src, 'rb').read()
    units = {}
    for kind, (base, size, n), first, count, stride, fsize, maxb in FIELDS:
        for r in range(n):
            for k in range(count):
                off = base + r * size + first + k * stride
                field = d[off:off + fsize]
                z = field.find(b'\0')
                if z <= 0:
                    continue
                s = field[:z]
                if not TEXT.fullmatch(s):
                    continue
                en = s.decode('ascii')
                key = (kind.split()[1], en)
                u = units.get(key)
                if u is None:
                    u = units[key] = {'id': f'{kind.split()[1]}{len(units):04d}', 'kind': kind, 'en': en,
                                      'max_bytes': maxb, 'fields': []}
                u['max_bytes'] = min(u['max_bytes'], maxb)
                u['fields'].append([off, fsize])
    os.makedirs(out, exist_ok=True)
    lst = list(units.values())
    with open(os.path.join(out, 'units.json'), 'w', encoding='utf-8') as f:
        json.dump(lst, f, ensure_ascii=False, indent=0)
    by = {}
    for u in lst:
        by[u['kind']] = by.get(u['kind'], 0) + 1
    print(len(lst), 'unique strings', sum(len(u['fields']) for u in lst), 'fields', by,
          sum(len(u['en']) for u in lst), 'chars')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
