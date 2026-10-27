# Link-time and profile-guided optimisation of the Switch build (2026-10-21)

The release NRO can be built with GCC's link-time optimisation (LTO) and profile-guided optimisation (PGO). The method
and most of the settings come from sa2-nx (its app/CMakeLists.txt, src/sa2_pgo.cpp, tools/pgo_summary.py and
docs/enhancements.md, "Link-time optimisation of a recompiled program"), credited there and in the files. The everyday
build folder (app/out/switch) stays without both: an LTO link of the whole program takes much longer than the
incremental builds of daily work.

## The three steps

1. **Instrumented build** (a folder of its own, e.g. app/out/switch-pgo-gen): configured with `-DBANJO_PGO=generate`
   (no LTO needed: GCC adds its counters before the interprocedural passes, so only the sources and compile options of
   the instrumented build and the optimised one must match). The NRO counts every branch of the game code
   (renut_recomp), the app (renut), Renut's renderer (banjo_renut_gpu) and plume, and src/banjo_pgo.cpp streams the
   counts to `pgo/banjo_pgo_<N>.stream` next to the NRO every 2 minutes, when the game loses focus and at exit.
2. **Play it**, on the console or in Ryujinx (the counts are the game's code paths, not its timing, so the emulator
   gives the same profile for the same play). Then on the PC: `python tools/pgo_merge.py <folder with the streams>`
   merges every session into one `.gcda` per object in `pgo/` at the root (gcov-tool merge-stream) and fills in the
   summary the stream API leaves zero (tools/pgo_summary.py). The streams name the profile folder of the PC that built
   the instrumented NRO: merge on that PC. `pgo/` is not in git (.gitignore).
3. **Optimised build** (another folder, e.g. app/out/switch-pgo-use): `-DBANJO_PGO=use -DBANJO_SWITCH_LTO=ON`. The
   `.gcda` names are relative to the build folder (`-fprofile-prefix-path`), so a different folder from step 1 works.

## What the options do (app/CMakeLists.txt)

- `BANJO_SWITCH_LTO` (OFF): `-flto -fno-fat-lto-objects` on renut_recomp, renut, banjo_renut_gpu and plume (a static
  library: CMake's archiver is `gcc-ar`, which indexes LTO objects); the link `-flto=<BANJO_SWITCH_LTO_JOBS>
  -flto-partition=balanced -ffp-contract=off -fno-strict-aliasing`. The SDK libraries and uam stay out. xxHash is built
  with `XXH_FORCE_MEMORY_ACCESS=0` in those targets (nfsmw-nx's LTO+PGO builds got garbage cache keys from xxHash's
  default, a strict-aliasing violation once inlined). `BANJO_SWITCH_IPA_PTA` (OFF) adds `-fipa-pta` as sa2-nx does.
- `BANJO_PGO=generate`: `-fprofile-generate=<pgo dir> -fprofile-prefix-path=<build dir> -fno-profile-values
  -fprofile-update=single -fprofile-info-section=gcov_info` (counts registered in a section; libgcov's exit-time files
  do not fit the Switch; value profiling is not thread-safe without atomic updates).
- `BANJO_PGO=use`: `-fprofile-use -fprofile-correction -fno-profile-values -fprofile-partial-training
  -Wno-missing-profile -Wno-coverage-mismatch` (code the sessions did not run is optimised as without a profile;
  functions changed since keep none), and for the game code (and src/cpu/banjo_hot_guest.cpp) sa2-nx's inliner limits:
  `max-inline-insns-auto=64`, `inline-unit-growth=12` (`BANJO_SWITCH_INLINE_AUTO`, `BANJO_SWITCH_INLINE_UNIT_GROWTH`).

## Pitfalls carried over from sa2-nx

- `-freorder-blocks-and-partition` fails on the recompiled code (jump-table label difference across sections).
- Native replacements of game functions must have the game code's `-fno-math-errno` under LTO, or GCC will not inline
  between them (src/cpu/banjo_hot_guest.cpp has it).
- The profile is only as good as the play: run the scripted route (renut_auto.toml) and gameplay.

## The first build (package 36t, 2026-10-21)

- Instrumented build (app/out/switch-pgo-gen, `-j 8`): 33 minutes from scratch, NRO 94 MB; 296 objects registered
  (`gcov_info` section: renut_recomp's 215 files, renut, banjo_renut_gpu, plume).
- Profile: one 30-minute Ryujinx session of the scripted route (scratchpad toml = renut_auto.toml with A every 6 s
  until 1500 s): boot, menus, the pizza cutscene and dialogue, then about 15 minutes in Spiral Mountain (A only:
  `input_script` has no stick, so the player jumps in place). One 4.2 MB stream, 296 `.gcda` files, largest arc count
  2.0e10 (the summary's sum_max is a 32-bit field: saturated at 2^32-1, as in sa2-nx). Movement, vehicles and the other
  levels are not in it; `-fprofile-partial-training` leaves that code as without a profile. A console session with the
  instrumented NRO (or a route that moves) merged in later would cover them.
- Optimised build (app/out/switch-pgo-use, `-j 8`, `-flto=4`): 16 minutes from scratch; text 72.1 -> 77.7 MB, NRO
  74.5 -> 80.1 MB. Check that the profile is read: recompile one game file with `-Wmissing-profile -Wcoverage-mismatch`
  in place of the `-Wno-` forms (renut_recomp.37.cpp: no warning).
- Pitfall: GCC's parallel LTRANS stage writes a makefile with the temporary folder's paths unquoted. With TMP in a
  folder whose path has spaces ("Banjo kazooie port") make split every target into three ("ignoring old recipe for
  target 'D:\Banjo'", 511 times, the last partition's job run up to three times at once). That NRO turned out
  byte-identical to a relink with TMP in a folder without spaces (the link alone: 7.5 minutes), but keep the
  temporary folder free of spaces.

## The second profile (package 36z6, 2026-10-22)

The bulk localized copies (docs/enhancements.md, "Bulk localized copies") changed about 600 generated files and added
twelve source files, so the profile of package 36t no longer matched most of the code (a changed function keeps no
profile). Taken again: instrumented build from scratch (app/out/switch-pgo-gen, `-j 9`, 37 minutes, NRO 110 MB), then
a 5-minute Ryujinx session (the user's choice) of the scripted route with every profiler off (the user's request:
`switch_profile_stacks`, `banjo_cpu_profiler`, `banjo_show_profiler`, `banjo_show_fps`, `banjo_stall_watch_seconds = 0`,
`renut_gpu_profile = 0`, `banjo_renut_diag_gpu_shaders`, `banjo_renut_diag_resolve_trace`), the copies called directly
(`banjo_hot_guard_calls = banjo_hot_guard_period = 0`). One 4.5 MB stream, 310 `.gcda` files (the shards included),
largest arc count 1.4e9. The old profile is kept outside the repository (D:/Banjo kazooie port/pgo_profile_36t).
