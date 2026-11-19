#!/usr/bin/env python3
"""banjo-nx (2026-10-31): run deko3d_glsl_opt.py passes over the shaders of an installed deko3d_glsl.bin (BDKG version 2
with a dictionary, tools/deko3d_glsl_compress.py; the package beside the NRO on the SD) and write it again: the shaders
a pass changes are compressed again with the package's own dictionary, every other byte stays as it was.

    python tools/deko3d_glsl_patch.py IN OUT [--passes fetchspec]

Layout (banjo-nx-installer lib/package.js, render/banjo_renut_deko3d.cpp): header (32 bytes: "BDKG", 2, count,
dictionary size, table offset, data offset, info offset, info size), the dictionary at 32, the table (56 bytes a shader,
sorted by hash: u64 hash, u32 offset from the data, compressed size, size, stage, 8 x u32 bool mask), the info section,
the data (one zstd frame a shader, 8-byte aligned). The table's bool masks come from the GLSL's 1000-1255 defines; the
passes used here add none. Python 3.14's compression.zstd. Game data: write under the build tree or the SD only.
"""
import argparse
import os
import struct
import sys
from compression import zstd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deko3d_glsl_opt as opt  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('out')
    ap.add_argument('--passes', default='fetchspec')
    ap.add_argument('--level', type=int, default=19)
    a = ap.parse_args()
    data = open(a.src, 'rb').read()
    magic, version, count, dict_size, table_off, data_off, info_off, info_size = struct.unpack_from('<4sIIIIIII',
                                                                                                    data, 0)
    if magic != b'BDKG' or version != 2 or not dict_size:
        sys.exit(f'deko3d_glsl_patch: {a.src}: not a version 2 package with a dictionary ({magic}, {version})')
    zdict = zstd.ZstdDict(data[32:32 + dict_size])
    entries = [list(struct.unpack_from('<QIIII8I', data, table_off + 56 * i)) for i in range(count)]
    out_data = bytearray()
    changed = []
    for e in entries:
        h, off, csize, size, stage = e[:5]
        frame = data[data_off + off:data_off + off + csize]
        if stage == 0:
            text = zstd.decompress(frame, zstd_dict=zdict).decode('utf-8')
            new = opt.optimise(text, a.passes, 'vert', f'{h:016X}')
            if new != text:
                raw = new.encode('utf-8')
                frame = zstd.compress(raw, level=a.level, zstd_dict=zdict)
                e[3] = len(raw)
                changed.append(f'vs_{h:016X}')
        e[1] = len(out_data)
        e[2] = len(frame)
        out_data += frame
        out_data += b'\0' * (-len(out_data) % 8)
    out = bytearray(data[:data_off])
    for i, e in enumerate(entries):
        struct.pack_into('<QIIII8I', out, table_off + 56 * i, *e)
    out += out_data
    open(a.out + '.tmp', 'wb').write(out)
    os.replace(a.out + '.tmp', a.out)
    print(f'{len(changed)} of {count} shaders changed ({", ".join(changed)}); {len(data)} -> {len(out)} bytes')


if __name__ == '__main__':
    main()
