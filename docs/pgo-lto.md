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

## The third profile (per-draw night, 2026-10-28)

The record-time renderer (the fast draw path, the replay stream, the record-time objects, tonight's rounds) was all
written after the second profile, so none of it had profile data (a function new since the profile is compiled with
GCC's guessed profile; one changed since loses its counts). Taken again from the night's source (e1-e9): instrumented
build in app/out/switch-pgo-gen (configured as switch-dist but `BANJO_PGO=generate`, LTO off, and the dist shader
cache source), then the same 5-minute Ryujinx session with every profiler off (scratchpad ryu/pgo_run.toml, now made
from the current emulator base toml: the 10-22 one had `banjo_renut_record_worker = false`, which the shipped toml does
not set), in the emulator data folder with the current shader files (banjo2). The second profile is kept outside the
repository (D:/Banjo kazooie port/pgo_profile_36z6).

Pitfall: every build folder writes the SDK's static archives to the one folder sdk/out/switch-arm64; an instrumented
build started while the dist build linked failed on a locked archive. The SDK objects have the same flags in every
folder (no LTO, no profile flags), so only never run two of these builds at once.

Also: ninja does not see the .gcda files as inputs, so a new profile with unchanged sources rebuilds nothing.
Apply it with a clean build of the release folder (`cmake --build <folder> --clean-first`; scratchpad
build_dist_clean.bat).


Later that night the profile was taken twice more, each time from the source as it then stood (cycles 3 to 5: the
f items, then the g items), so the package's hottest code has counts from its own form. A cycle is one script run
start to finish (instrumented build, the 380 s session, the merge, the clean release build: about 30 minutes), the
previous profile folder moved aside first (the merge refuses a folder that already holds .gcda files). Changes made
after a profile still build against it: GCC drops the counts of a function whose form changed (a mismatch warning,
not an error, with the project's flags) and guesses for it.

Pitfall: a background waiter that grepped the chain's output for "cycle done" matched the copy of the previous
cycle's last lines the chain prints at its start, and started an incremental build in the release folder while the
clean build was linking. Wait on a count of markers, or a marker only the awaited step prints.

## The title update's profile (2026-10-29)

Title update 3 recompiled the whole game (docs/title-update-3.md): every generated function has new code and most
have new names, so the night_final profile matched only the renderer, the app and plume. Taken again: the base
profile moved out of the repository (`pgo_profile_night_final_base` next to it; `pgo_merge.py` adds to files already
there), the instrumented folder pointed at the distribution build's shader package (`BANJO_DEKO3D_GLSL_CPP` in its
CMakeCache: the update's 23 new runtime shaders), a 15-minute Ryujinx session of the scripted route (one 4.9 MB
stream, 315 `.gcda`; at 0.3-3 fps the route covers boot, the movies, the title and the first menus), then
the release folder rebuilt with every profiled source touched (ninja does not follow the `.gcda` files). A console
session of the instrumented NRO merged in later would cover the levels.
