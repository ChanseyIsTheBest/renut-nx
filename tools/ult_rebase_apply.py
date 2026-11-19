#!/usr/bin/env python3
"""Put banjo-nx's layer back on the ultimate base (2026-11-19; docs/ultimate-rebase-plan.md).

Run in the worktree after the commit of the base's tree. For each path, by the classes of
tools/ult_rebase_classify.py (out/ult_rebase_classes.tsv) and the rules below, it writes one of:

  T          the base's tip as it is (already in the tree)
  B          banjo-nx master's file
  M3:T,S,B   three-way: the base's tip, with banjo-nx's work since the fork (ancestor S, the base's first commit)
  M3:B,K,T   three-way: banjo-nx's file, with only the ultimate ports' changes (ancestor K, katamari-nx 1.0.1):
             for the per-XDK files and the files the Katamari era made Katamari's
  DEL        removed (Katamari's own layer)

Conflicts are left as diff3 blocks; out/ult_rebase_apply.txt lists every path with its action and conflict count.
  python tools/ult_rebase_apply.py [--group NAME] [--dry-run]
"""
import argparse
import fnmatch
import os
import subprocess
import sys
import tempfile

REVS = {'S': '83b4ae77', 'K': '701c9d32', 'T': 'refs/ultbase/ultimate', 'B': 'master'}

# Katamari's own layer: removed.
DEL = [
    'app/katamari_switch.toml', 'app/katamari_switch_defaults.toml', 'app/src/katamari_keys.cpp',
    'app/src/katamari_builtin_config.h.in', 'app/src/cpu/katamari_stage_diag.cpp', 'app/overrides.toml',
    'pgo/*', 'pgo-inplace/*', 'dist/switch/katamari-nx/*', 'dist/switch/README.txt',
    'tools/forwarder/katamari_forwarder.json', 'tools/forwarder/katamari_main.npdm',
    'tools/katamari_files.py', 'tools/katamari_shader_files.py', 'tools/make_katamari_tomls.py',
    'tools/renut_device_offsets_katamari.py', 'tools/renut_hook_map_katamari.txt', 'tools/renut_hook_remap_katamari.py',
    'tools/switch/icon/katamari_icon.jpg',
    # translated code of banjo-nx's game, never built (commented out of banjo-nx's CMakeLists too): the base removed it
    'app/src/renut_engine/fixes/mullhwucrash.cpp',
]

# Banjo's game layer: banjo-nx's file as it is.
TAKE_B = [
    'app/config/*', 'app/indirect_promotions.txt', 'app/renut_config.toml', 'README.md', 'app/icon/*',
    'app/src/cpu/hot/hand.txt', 'app/src/cpu/hot/localized.txt', 'app/src/cpu/hot/localized_bulk.txt',
    'dist/switch/*', 'tools/switch/icon/banjo_icon.jpg',
    # The Katamari era gated these (N&B's own functions); no port changed them since.
    'app/src/renut_engine/FPS.cpp', 'app/src/renut_engine/mnk_controls.cpp',
    'app/src/renut_engine/gpu/guest/widescreen.cpp', 'app/src/renut_engine/gpu/guest/photo_capture.cpp',
    'app/src/renut_engine/dlc_install.cpp', 'app/src/me/me_ring_wait.cpp',
]

# Banjo's file plus the ultimate ports' changes only (per-XDK, and files the Katamari era made Katamari's).
M3_BKT = [
    'app/src/renut_engine/gpu/d3d/hooks/*', 'app/src/renut_engine/gpu/d3d/d3d.h',
    'app/src/renut_engine/gpu/d3d/d3d_native.h', 'app/src/renut_engine/gpu/d3d/d3d_native_state.cpp',
    'app/src/renut_engine/gpu/banjo_shared_hooks.h', 'app/src/renut_engine/gpu/render/draw.h',
    'app/src/renut_engine/gpu/guest/command_buffers.cpp', 'app/CMakeLists.txt',
    'app/src/cpu/hot/banjo_hand_natives.h', 'app/src/cpu/banjo_hot_guest.cpp',
]


def match(path, pats):
    return any(fnmatch.fnmatchcase(path, p) for p in pats)


def show(rev, path):
    r = subprocess.run(['git', 'show', '%s:%s' % (REVS[rev], path)], capture_output=True)
    if r.returncode != 0:
        return None
    if b'\0' in r.stdout[:8000]:
        return r.stdout  # binary: as it is
    return r.stdout.replace(b'\r\n', b'\n')


def merge3(ours, base, theirs, labels):
    with tempfile.TemporaryDirectory() as d:
        names = []
        for i, data in enumerate((ours, base, theirs)):
            p = os.path.join(d, str(i))
            open(p, 'wb').write(data if data is not None else b'')
            names.append(p)
        r = subprocess.run(['git', 'merge-file', '-p', '--diff3', '-L', labels[0], '-L', labels[1], '-L', labels[2]]
                           + names, capture_output=True)
        return r.stdout, (r.returncode if r.returncode > 0 else 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--only', nargs='*', help='only these path patterns')
    a = ap.parse_args()
    rows = [l.rstrip('\n').split('\t') for l in open('out/ult_rebase_classes.tsv', encoding='utf-8')][1:]
    report = []
    for path, cls, have, kat in rows:
        if path.startswith('docs/') or path.startswith('tools/ult_rebase_'):
            continue  # the docs are their own step
        if a.only and not match(path, a.only):
            continue
        if match(path, DEL):
            action = 'DEL'
        elif match(path, TAKE_B):
            action = 'B' if 'B' in have else 'DEL'
        elif match(path, M3_BKT):
            action = 'M3:B,K,T' if ('B' in have and 'T' in have) else ('B' if 'B' in have else 'T')
        elif cls in ('same', 'base', 'base-new', 'banjo-del', 'conflict-del'):
            action = 'T'
        elif cls in ('banjo', 'banjo-new'):
            action = 'B'
        elif cls == 'merge':
            action = 'M3:T,S,B'
        elif cls == 'base-del':
            action = 'B'
        else:
            action = 'T'
        conflicts = 0
        if action == 'T':
            continue
        out = None
        if action == 'DEL':
            out = None
        elif action == 'B':
            out = show('B', path)
        elif action == 'M3:T,S,B':
            out, conflicts = merge3(show('T', path), show('S', path), show('B', path), ('base tip', 'fork', 'banjo-nx'))
        elif action == 'M3:B,K,T':
            out, conflicts = merge3(show('B', path), show('K', path), show('T', path),
                                    ('banjo-nx', 'katamari-nx 1.0.1', 'base tip'))
        report.append('%s\t%s\t%d\t%s' % (action, cls, conflicts, path))
        if a.dry_run:
            continue
        full = os.path.join(*path.split('/'))
        if out is None:
            if os.path.exists(full):
                os.remove(full)
        else:
            os.makedirs(os.path.dirname(full) or '.', exist_ok=True)
            # The working tree's line ends: CRLF for text, as git checks out here (core.autocrlf true).
            text = b'\0' not in out[:8000]
            open(full, 'wb').write(out.replace(b'\n', b'\r\n') if text else out)
    with open('out/ult_rebase_apply.txt', 'w', encoding='utf-8') as f:
        f.write('\n'.join(report) + '\n')
    n = {}
    for r in report:
        k = r.split('\t')[0]
        n[k] = n.get(k, 0) + 1
    print(n, 'conflicted files:', sum(1 for r in report if r.split('\t')[2] != '0'))
    for r in report:
        if r.split('\t')[2] != '0':
            print('  ', r)


if __name__ == '__main__':
    sys.exit(main())
