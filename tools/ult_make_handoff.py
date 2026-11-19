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
         'app/src/game_menu.cpp', 'tools/guest_neighbours.py', 'tools/cvar_default_diff.py',
         # docs/ultimate-rebase-plan.md list (c): banjo-nx's general work the base lacks, grafted.
         'app/thirdparty/plume/plume_deko3d.h', 'app/thirdparty/plume/plume_deko3d.cpp',
         'app/src/renut_engine/gpu/render/pipeline.cpp', 'app/src/renut_engine/gpu/backend/memory_report.cpp',
         'sdk/src/ui/switch_perf.cpp',
         # The forms self-check under near-exact #2 (found in N&B's Ryujinx run).
         'app/src/cpu/banjo_forms_selfcheck.cpp',
         # 2026-11-20, the console regression: the cache tool's round guess, census::On() inline, and the tools.
         'tools/deko3d_variant_cache.py', 'app/src/renut_engine/gpu/stage_census.h', 'tools/deko3d_miss_coverage.py',
         'tools/profile_threads.py', 'tools/deko3d_package_extract.py', 'tools/deko3d_glsl_compare.py']


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

# banjo_renut_deko3d.cpp: the GLSL loader (UnpackGlsl, CreateGameShader) is the handoff; CompressInstalledPackage is
# tied to banjo-nx-installer's raw package (the base's installer makes its own), so it is taken out of the copy.
def without_installer_compression(text):
    a = text.index("// renut-nx (2026-11-01, the public build, the user's idea)")
    b = text.index('InstalledPackage &OpenInstalled() {')
    text = text[:a] + text[b:]
    return edit(text, [('#include <chrono>  // renut-nx (2026-11-01)\n', ''),
                       ("    CompressInstalledPackage(path);  // renut-nx (2026-11-01): the web installer's raw package, once\n",
                        '')], 'banjo_renut_deko3d.cpp')


# app/CMakeLists.txt: BANJO_GENERATED_NO_TRAPPING_MATH in blur-nx's form (the FPSCR writers keep -ftrapping-math). The
# three blocks are taken from this tree's file and put at the same anchors in the base's.
def notrap_pairs():
    ours = open('app/CMakeLists.txt', encoding='utf-8').read().replace('\r\n', '\n')

    def span(start, end):
        a = ours.index(start)
        return ours[a:ours.index(end, a) + len(end)]

    hot = '    set_property(SOURCE ${BANJO_HOT_SOURCES} APPEND PROPERTY COMPILE_OPTIONS -fno-math-errno)\n'
    rounds = 'message(STATUS "banjo: sa2-nx\'s CPU rounds: definitions \'${_banjo_round_definitions}\'")\n'
    return [
        ('    if(BANJO_GENERATED_NO_TRAPPING_MATH)\n        target_compile_options(renut_recomp PRIVATE -fno-trapping-math)\n'
         '    endif()\nendif()\n',
         span("    # banjo-nx (2026-11-19, the rebase; blur-nx's BLUR_NO_TRAPPING_MATH form", 'left off")\nendif()\n')),
        (hot + 'endif()\n', span(hot, '    endif()\nendif()\n')),
        (rounds, span(rounds, '        set_property(SOURCE ${BANJO_HOT_SOURCES} APPEND PROPERTY COMPILE_OPTIONS '
                              '-ftrapping-math)\n    endif()\nendif()\n')),
    ]


# app/src/cpu/banjo_hot_guest.cpp: banjo_hot_bulk_lines (the file also holds Banjo's own lines: the change alone, taken
# from this tree's file and put at the same anchors in the base's).
def bulk_lines_pairs():
    ours = open('app/src/cpu/banjo_hot_guest.cpp', encoding='utf-8').read().replace('\r\n', '\n')

    def span(start, end):
        a = ours.index(start)
        return ours[a:ours.index(end, a) + len(end)]

    count = 'REXCVAR_DEFINE_BOOL(banjo_hot_count, false, "Banjo/Enhancements",\n'
    tally = '  uint32_t reached = 0, on = 0, direct = 0, bulk_total = 0, bulk_reached = 0, bulk_on = 0;\n'
    diff_line = ('      if (d)\n        REXLOG_INFO("[hot] {} (bulk) off after {} difference(s); {} calls compared before", '
                 'h->name, d,\n                    h->compared.load(std::memory_order_relaxed));\n')
    return [
        (count, span('REXCVAR_DEFINE_BOOL(banjo_hot_bulk_lines', count)),
        (tally, span(tally, '(report_number++ % 6) == 0;\n')),
        (diff_line, span(diff_line, 'h->not_comparable.load(std::memory_order_relaxed));\n')),
    ]


CENSUS_ON = [(
    "bool On() {\n  static const bool on = REXCVAR_GET(blur_renut_diag_stage_census);\n  return on;\n}\n",
    "std::atomic<int> g_on{-1};\nbool OnSlow() {\n  const bool on = REXCVAR_GET(blur_renut_diag_stage_census);\n"
    "  g_on.store(on ? 1 : 0, std::memory_order_relaxed);\n  return on;\n}\n")]


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
                            ('tools/run_pc.ps1', RUN_PC_QUOTE), ('app/CMakeLists.txt', notrap_pairs()),
                            ('app/src/cpu/banjo_hot_guest.cpp', bulk_lines_pairs()),
                            ('app/src/renut_engine/gpu/banjo_renut_tuning.cpp', CENSUS_ON)):
            old = base_text(path)
            put(a, path, old)
            put(b, path, edit(old, pairs, path))
        path = 'app/src/renut_engine/gpu/banjo_renut_deko3d.cpp'
        put(a, path, base_text(path))
        put(b, path, without_installer_compression(open(path, encoding='utf-8').read().replace('\r\n', '\n')))
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
