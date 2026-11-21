"""renut-nx (2026-11-22): an emulator's game_root packed as the web installer packs it (worker.js: LZ4 HC 9 in 256 KB
blocks as NAME.lz4b, version 1; the music as NAME.opcm), for testing the port's readers. Game data: never commit or
share what it writes.

  python tools/pack_game_root.py pack GAME_ROOT BACKUP [--opcm NAME=FILE.opcm ...]   (each original moved to BACKUP)
  python tools/pack_game_root.py restore GAME_ROOT BACKUP                           (the .lz4b / .opcm removed, the
                                                                                     originals moved back)
The rules are worker.js's: every file but the ASF movies (30 26 B2 75), the compressed bundles (0F F5 12 ED),
default.xex / default.xexp, $SystemUpdate and files under 4 KB; a block that does not get smaller is stored as it is.
"""
import os
import shutil
import struct
import sys
from concurrent.futures import ProcessPoolExecutor

import lz4.block

BLOCK = 256 * 1024
SKIP = (bytes.fromhex('3026b275'), bytes.fromhex('0ff512ed'))


def pack_one(job):
    src, dst = job
    size = os.path.getsize(src)
    count = (size + BLOCK - 1) // BLOCK
    sizes = []
    with open(src, 'rb') as f, open(dst + '.part', 'wb') as out:
        out.write(struct.pack('<8sIIQII', b'RXLZ4BLK', 1, BLOCK, size, count, 0))
        offset = 32
        for _ in range(count):
            b = f.read(BLOCK)
            c = lz4.block.compress(b, mode='high_compression', compression=9, store_size=False)
            if len(c) >= len(b):
                c = b
            out.write(c)
            sizes.append(len(c))
            offset += len(c)
        out.write(struct.pack(f'<{count}I', *sizes))
        out.write(struct.pack('<Q8s', offset, b'RXLZ4END'))
    os.replace(dst + '.part', dst)
    return size, os.path.getsize(dst)


def wanted(root, rel):
    if rel in ('default.xex', 'default.xexp') or rel.startswith('$SystemUpdate/'):
        return False
    p = os.path.join(root, rel)
    if os.path.getsize(p) < 4096:
        return False
    with open(p, 'rb') as f:
        return f.read(4) not in SKIP


def main():
    mode, root, backup = sys.argv[1:4]
    # renut-nx (2026-11-22): the emulator SD's game_root is a link to the master game copy: refuse a linked root (or
    # one under a link), so a test never changes the master's files; pack a separate copy instead.
    if os.path.realpath(root) != os.path.abspath(root) or os.path.islink(root):
        raise SystemExit(f'{root} is (or is under) a link to {os.path.realpath(root)}: pack a separate copy')
    opcm = dict(a.split('=', 1) for a in sys.argv[5:]) if '--opcm' in sys.argv else {}
    files = []
    for d, _, names in os.walk(root):
        for n in names:
            rel = os.path.relpath(os.path.join(d, n), root).replace('\\', '/')
            files.append(rel)
    if mode == 'restore':
        moved = 0
        for d, _, names in os.walk(backup):
            for n in names:
                rel = os.path.relpath(os.path.join(d, n), backup).replace('\\', '/')
                for suffix in ('.lz4b', '.opcm'):
                    if os.path.exists(os.path.join(root, rel + suffix)):
                        os.remove(os.path.join(root, rel + suffix))
                os.makedirs(os.path.dirname(os.path.join(root, rel)), exist_ok=True)
                os.replace(os.path.join(backup, rel), os.path.join(root, rel))
                moved += 1
        print(f'restored {moved} files')
        return
    jobs = []
    for rel in files:
        if rel.endswith('.lz4b') or rel.endswith('.opcm') or rel in opcm:
            continue
        if wanted(root, rel):
            jobs.append(rel)
    with ProcessPoolExecutor(6) as ex:
        res = list(ex.map(pack_one, [(os.path.join(root, r), os.path.join(root, r) + '.lz4b') for r in jobs], chunksize=8))
    for rel in jobs + list(opcm):
        os.makedirs(os.path.dirname(os.path.join(backup, rel)) or backup, exist_ok=True)
        os.replace(os.path.join(root, rel), os.path.join(backup, rel))
    for rel, src in opcm.items():
        shutil.copyfile(src, os.path.join(root, rel) + '.opcm')
    n = sum(r[0] for r in res)
    o = sum(r[1] for r in res)
    print(f'packed {len(jobs)} files: {n / 1e6:.1f} MB -> {o / 1e6:.1f} MB; {len(opcm)} .opcm; originals in {backup}')


if __name__ == '__main__':
    main()
