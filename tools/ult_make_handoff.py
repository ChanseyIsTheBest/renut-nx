#!/usr/bin/env python3
"""banjo-nx (2026-11-19): the handoff patch for the ultimate base, from this tree's fixes that are not Banjo's own.

Files whose only difference from the base's tip is the fix are taken as they are here; files that also hold Banjo's
own lines get only the fix, applied to the base's copy (asserted replacements). The patch is against the base's
tree (git apply in D:/renut-ultimate/base); it is written to out/ultimate_handoff_2026-11-19.patch.

  python tools/ult_make_handoff.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

BASE_REV = 'refs/ultbase/ultimate'
OUT = os.path.join('out', 'ultimate_handoff_2026-11-19.patch')
# The fix alone (checked: `git diff --ignore-cr-at-eol refs/ultbase/ultimate -- <file>` is only it).
WHOLE = ['tools/indirect_calls.py', 'tools/indirect_promotions_rekey.py', 'tools/reg_liveness.py',
         'tools/cc_entry_census.py', 'tools/cc_exit_census.py', 'tools/cc_exit_reads.py', 'tools/r1_preserve.py',
         'tools/lr_preserve.py', 'app/src/cpu/banjo_codegen_checks.cpp', 'app/src/game_menu.h',
         'app/src/game_menu.cpp', 'tools/guest_neighbours.py', 'tools/cvar_default_diff.py']


def base_text(path):
    r = subprocess.run(['git', 'show', '%s:%s' % (BASE_REV, path)], capture_output=True)
    if r.returncode != 0:
        return None
    return r.stdout.decode('utf-8').replace('\r\n', '\n')


def edit(text, pairs, path):
    for a, b in pairs:
        if text.count(a) != 1:
            sys.exit('%s: %d anchors for %r' % (path, text.count(a), a[:60]))
        text = text.replace(a, b)
    return text


PORT87 = [(
    "  constexpr u32 kSkipped[4] = {0x2A30, 0x2A34, d3d::kDeviceOffsetPredication, d3d::kDeviceOffsetPredication + 4};",
    "  // banjo-nx (2026-11-19, its rebase on the base): the XDK's own offsets (d3d_native.h). Port 87 wrote\n"
    "  // vivapinata-nx's 0x2A30 / 0x2A34, which are other fields at N&B's and Katamari's XDKs (TilePredication 0x2AB4).\n"
    "  constexpr u32 kSkipped[4] = {native::off::TilePredication, native::off::RunPredication, d3d::kDeviceOffsetPredication,\n"
    "                               d3d::kDeviceOffsetPredication + 4};\n"
    "  static_assert(native::off::TilePredication < native::off::RunPredication &&\n"
    "                    native::off::RunPredication < d3d::kDeviceOffsetPredication,\n"
    "                \"kSkipped ascending\");"), (
    "  const u32 tiles = *reinterpret_cast<const be_u32 *>(device + 0x30B8);  // native::off::TileCount\n"
    "  if (!(device[0x2A38] & 0x20) || tiles < 2 || tiles > 16)  // native::off::Flags",
    "  // banjo-nx (2026-11-19, its rebase on the base): native::off's TileCount and Flags (the byte BeginTiling sets 0x20\n"
    "  // in, hooks/draw.cpp), not port 87's vivapinata-nx offsets 0x30B8 / 0x2A38, other fields at this XDK (its Flags is\n"
    "  // TilePredication + 8, as here).\n"
    "  const u32 tiles = *reinterpret_cast<const be_u32 *>(device + native::off::TileCount);\n"
    "  if (!(device[native::off::Flags] & 0x20) || tiles < 2 || tiles > 16)")]

RUN_PC_QUOTE = [(
    "        @($Extra -split ' ' | Where-Object { $_ })\n$style = ",
    "        @($Extra -split ' ' | Where-Object { $_ })\n"
    "# banjo-nx (2026-11-19): Start-Process joins -ArgumentList's elements with spaces and quotes none of them, so a path\n"
    "# with a space (\"D:\\Banjo kazooie port\\...\") split into two arguments and the run wrote no log.\n"
    "$args = @($args | ForEach-Object { if ($_ -match ' ' -and $_ -notmatch '^\".*\"$') { '\"' + $_ + '\"' } else { $_ } })\n"
    "$style = ")]


def main():
    tmp = tempfile.mkdtemp(prefix='ult_handoff_')
    a, b = os.path.join(tmp, 'a'), os.path.join(tmp, 'b')
    try:
        def put(root, path, text):
            full = os.path.join(root, *path.split('/'))
            os.makedirs(os.path.dirname(full), exist_ok=True)
            open(full, 'w', encoding='utf-8', newline='\n').write(text)

        for path in WHOLE:
            old = base_text(path)
            new = open(path, encoding='utf-8').read().replace('\r\n', '\n')
            if old is not None:
                put(a, path, old)
            put(b, path, new)
        for path, pairs in (('app/src/renut_engine/gpu/guest/command_buffers.cpp', PORT87),
                            ('tools/run_pc.ps1', RUN_PC_QUOTE)):
            old = base_text(path)
            put(a, path, old)
            put(b, path, edit(old, pairs, path))
        r = subprocess.run(['git', 'diff', '--no-index', '--no-color', 'a', 'b'], cwd=tmp, capture_output=True)
        patch = r.stdout.decode('utf-8')
        # The paths as the base's tree has them.
        patch = patch.replace('a/a/', 'a/').replace('b/b/', 'b/').replace(' a/b/', ' a/').replace('--- a/dev/null',
                                                                                                  '--- /dev/null')
        os.makedirs('out', exist_ok=True)
        open(OUT, 'w', encoding='utf-8', newline='\n').write(patch)
        print('%s: %d files, %d lines' % (OUT, patch.count('diff --git'), patch.count('\n')))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
