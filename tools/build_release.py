"""Assemble the release ZIP.

usage:
    python -I build_release.py ORIGINAL_ROOT GAME_ROOT OUT_DIR VERSION PAGE_URL

ORIGINAL_ROOT: a game-folder-shaped copy of the untouched files ("Avernum Files/...").
GAME_ROOT:     the game folder with the Japanese files built in (and dsound.dll etc.).
Writes OUT_DIR/AvernumEFP_JP_<VERSION>.zip.
"""
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
MOD = os.path.dirname(HERE)
SRC = os.path.join(MOD, 'dist_src')


def main(argv):
    if len(argv) != 5:
        print(__doc__)
        return 2
    orig, game, out, version, page = argv
    name = 'AvernumEFP_JP_' + version
    stage = os.path.join(out, name)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, 'tools'))
    os.makedirs(os.path.join(stage, 'data'))
    subprocess.check_call([sys.executable, '-I', os.path.join(HERE, 'make_patch.py'), orig, game,
                           os.path.join(stage, 'data', 'jp_patch.bin')])
    for f in ('dsound.dll', 'avernum_jp_exe.tsv', 'avernum_jp.ini'):
        shutil.copy2(os.path.join(game, f), os.path.join(stage, 'data', f))

    def text(src, dst, crlf=True, bom=True):
        t = open(src, encoding='utf-8').read().replace('__VERSION__', version).replace('__PAGE_URL__', page)
        if crlf:
            t = t.replace('\r\n', '\n').replace('\n', '\r\n')
        with open(dst, 'w', encoding='utf-8-sig' if bom else 'utf-8', newline='') as fh:
            fh.write(t)

    # Windows PowerShell 5.1 reads a .ps1 without BOM as the ANSI code page, so the BOM is required
    text(os.path.join(SRC, 'tools', 'installer.ps1'), os.path.join(stage, 'tools', 'installer.ps1'))
    text(os.path.join(SRC, 'README.txt'), os.path.join(stage, 'README.txt'))
    for f in ('install.bat', 'uninstall.bat'):
        shutil.copy2(os.path.join(SRC, f), os.path.join(stage, f))

    zpath = os.path.join(out, name + '.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dirpath, _, files in os.walk(stage):
            for fn in sorted(files):
                p = os.path.join(dirpath, fn)
                z.write(p, os.path.relpath(p, out).replace('\\', '/'))
    print('wrote', zpath, os.path.getsize(zpath), 'bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
