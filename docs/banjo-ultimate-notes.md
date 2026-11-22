# banjo-nx on the ultimate base: notes

What the rebase met while building and testing banjo-nx (Banjo-Kazooie: Nuts & Bolts, title update 3, XDK 2.0.7978)
on renut_nx_ultimate_base 6cd02ae7, with its evidence. The inventory and the method are in
docs/ultimate-rebase-plan.md; the not-exact forms' censuses in docs/banjo-near-exact.md; the order of events in
docs/ultimate-progress-log.md.

## Setting up the worktree (2026-11-19)

A worktree has only the tracked files. What the builds also need, none of it committed:

| What | From | Why |
|---|---|---|
| `app/assets` | a junction to `D:\Banjo kazooie port\game` (default.xex with title update 3's default.xexp) | the manifest reads `assets/default.xex` |
| `app/generated/rexglue.cmake` | the base's working tree (the generator writes it; port 69's PCH byproduct in it) | the app's CMakeLists includes it before the first code generation |
| FFmpeg's configure output (`config.h`, `libavutil/avconfig.h`, `ffversion.h`, the `*_list.c`) | the base's working tree (the same as banjo-nx's) | ignored by FFmpeg's .gitignore; the SDK's FFmpeg build needs them |
| `app/thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin/{bin,lib}` | the base's working tree | DXC for the shader translator (ignored) |
| SDL3's render shaders' .hlsl, spirv-tools' build_overrides | the base's working tree | ignored upstream files the SDK's build may read |
| `out/renut_shader_dump`, `out/glslang` | banjo-nx's out/ | the game's shader containers (game data) and the host glslangValidator |
| `out/pc_user` | `D:\Banjo kazooie port\save\renut` | the user's save, with the DLC installed in it |

The Bash tool's PATH can lack the VS Installer folder, so vcvars64 fails ("'vswhere.exe' is not recognized"): builds run
with `PATH="/c/Program Files (x86)/Microsoft Visual Studio/Installer:$PATH"`.

## Build fixes

### The promoted indirect calls lost their sites (post_codegen stopped)

The first code generation with the base's generator (407 files, 39,926 functions, the same count as banjo-nx's tree)
stopped in post_codegen: "indirect_calls: 19 sites of app/indirect_promotions.txt are not in the generated code
(first: 821B5040 site 0)". tools/indirect_calls.py keyed a `bctrl` site by the last `DEFINE_REX_FUNC(sub_X)` above it,
and banjo-nx's config names 4,669 functions (appMainDraw, the CRT's, rex_XInputGetKeystroke ...), so a named function's
sites were counted as the previous sub_ function's. In banjo-nx's tree appMainDraw (0x821F1460) followed sub_821B5040
(a one-line tail jump) and its virtual call was "821B5040 site 0"; the base's generator lays the files out differently,
so those keys pointed at other functions or at none.

Fix (a handoff): every function is its own key, a named one by its address in `*_init.cpp`'s table
(`{ 0x821F1460, appMainDraw },`). `tools/indirect_promotions_rekey.py` maps an old list's keys through the tree it was
made for (banjo-nx's app/generated: 15,546 tagged sites): 22 of 287 entries moved to their own function, none lost, and
`indirect_calls.py --check` on the new tree promotes all 287 (26 with two or three targets).

### A duplicate after the merge, and a configure before the code generation

- `banjo_renut_tuning.cpp`: two `case 69` (banjo-nx's fetch spec name came in beside the base's, whose fetch spec is
  83); `render/pipeline.cpp`: `FetchSpecialised` twice (both trees had it). One of each kept.
- The first configure ran before any code generation, and `generated/rexglue.cmake` adds the generated sources (the
  recomp target and the include folder of `renut_pch.h`) only when `generated/sources.cmake` exists at configure time:
  the hot copies' files did not find `renut_pch.h`. A configure after the code generation (touching CMakeLists.txt)
  adds them. A new tree: run the code generation (or one failed build) first, then configure again.

## The calling convention (step 3; 2026-11-19, on the first generated tree)

`tools/cc_entry_census.py`: 39,926 functions, 13,994 reached through a pointer. **One** reads a scratch vector register
on entry: **sub_82402910 reads v0**. It is a message handler (a switch on the word at 0(r4)); at 0x82402D60 it stores v0
at +2784(r31), and on the path through 0x82402D48 (`bge cr6`: the halfword at 0(r11) is 3 or more) v0 is not loaded
first, so it stores what its caller left there (and passes r1+80, unwritten on that path, to sub_8254D2C8). Not provably
value-independent, so not left to the convention: `app/config/scratch_vectors_read_on_entry.txt` lists v0, and
tools/reg_liveness.py keeps the listed registers live at every indirect call (and at hook / import calls, which share
INDIRECT_USE), so the machine copies hand v0 over as the generated code does. Exact; a store more at such call sites.
The census names the listed register and then says none.

`tools/cc_exit_census.py`: 16,101 indirect call sites; 776 read an EXIT_DEAD register after the call before writing it
(4,514 reads). `tools/cc_exit_reads.py` (new) names each read's form; every one is value-independent:

| Reads | Form | Why it does not depend on the value |
|---|---|---|
| 3,857 | `twi 31,r0,22` (r0) | the debug trap: ppc_trap only logs (Katamari's six were the same) |
| 96 | `subfe rD,rX,rX` (r5-r10) | ~rX + rX + CA = CA - 1: the carry only |
| 58 | vupkd3d128 (v6-v12, v60-v62) | `REX_VUPKD3D_*`: the whole register from the source; defuse counts the destination as read |
| 42 | vmrghw (v5-v7) | the whole register from two other sources (simde unpack) |
| 3 | vsraw, vctsxs, vexptefp | the same: whole-register results of other sources |
| 1 site | fres | defuse does not know it (answers "every register"); it reads its one float source |
| 7 sites x 64 | `(tools/vec_lvlx_splat.py: ...)` | a post-codegen tool's note in the comments, parsed as an instruction |

So the exit bound holds for N&B with `SASRT_REG_LIVENESS_EXIT_ABI` on (the default). The census now knows these forms
(a handoff): it lists 631 sites, the traps alone.

## The PC port (step 2; 2026-11-19)

**Boot (run boot1, 170 s):** Renut on Vulkan, the user's save, title update 3 mounted ("XEX patch applied ... 0.0.3.1",
the update folder at `D:\Banjo kazooie port\update`), 1.0.0's font and dialogue boxes. A presses 40-94 s: the title, the
menus, the save loaded (93 s, "Saving content"), Showdown Town in the vehicle from about 140 s. The picture as renut-nx
1.0.2's. 138 error lines, all `NtQueryInformationFile(XFileXctdCompressionInformation) unimplemented` (a normal log has
about 127). tools/run_pc.ps1 first wrote no log at all: Start-Process does not quote, and Banjo's folders have spaces
(fixed; a handoff).

**Verify (run verify1, 480 s, tools/run_pc_verify.ps1, the same presses, then input_script_sticks from 160 s):** 533 hand-
picked copies reached (532 on), 487 of 506 bulk copies reached; 261 million calls compared, 134 million not comparable
(both forms stopped with the same stores); the stack forwards 9.3 million checked, 0 differences; the generated-code
self-checks (9.2 billion folded loads, 56.7 million promoted calls, 18 million owners from the PCR) 0 differences; the r1
mirror and link-register write-backs 0; the flush-mode flow 8.4 billion checks, 0 switches. **One copy differed:**
`sub_82C41318` (bulk), `f3: copy=0x8000000000000000 original=0xB6F4000000000000` three passes in a row (inputs r3 40EEF720
..., f1 3F6028F2A0000000), memory the same (204 bytes in 20 ranges): the original returns a negative single-precision
denormal in f3 where the copy returns -0.0. It is reached only through a one-line thunk (sub_82C42018, `b 0x82c41318`)
by pointer, so by the convention f3 is dead to its callers; f3 is in the guard's compare only as reg_liveness's margin
(f1-f4 kept as results). The flush modes before each f3 producer are the same in the two forms; the change since
renut-nx 1.0.2 (whose guard had this copy at 0) is ports 96 / 136's single-precision fused forms (REX_FMADDS_KNOWN). Not
explained, so not shipped: `off` in localized_bulk.txt (the generated original runs). Open: whether it is the guard's
two runs starting in different host flush states, or the fused form.

### Port 87's offsets, and the per-XDK values in shared files

See docs/ultimate-rebase-plan.md, "Found on the way". In short: port 87 (tiles once) now reads `native::off`; port
169's scaler copy is at N&B's +0x359C; the device's own declaration at +0x2FA8; port 138's viewport at 0x3170.

## The hot functions (step 6)

The base's machine copies (sasrt-nx's tools/localize_native.py) with N&B's own lists: app/src/cpu/hot/localized.txt
(the hand-picked copies: 533 reached) and localized_bulk.txt (506 bulk copies in 12 shards, from 1.0.2's console
profile), banjo_hand_natives.h (the XAudio filter's hand-written native) and the blur_* hand helpers. Two bulk
functions cannot be copies (sub_82335C28 reads the time base; sub_828177E8's callee has a switch line the tool does not
know). Checked by the guard twice: verify1 (exact build, 261.0 million calls compared) and verify_ne (the cleared
not-exact codegen, 300.4 million): two bulk copies are off for a single denormal against zero in a volatile float
register (sub_82C41318's f3, sub_8221BFA0's f6), the rest show no difference. On the Switch the copies are in place
(SASRT_HOT_IN_PLACE, the base's default there: no hook, no guard; an `off` copy keeps the generated original).

## PGO and LTO (step 7): ready, no profile yet

pgo/ and pgo-inplace/ are empty: Katamari's profiles went with its layer, and 1.0.2's profile (banjo-nx's untracked
pgo/) is for other code (the base's generator and passes changed most of the game code), so it would mostly be dropped
as mismatched. The tree builds both steps as docs/pgo-lto.md says; for this branch:

1. The instrumented NRO, with the copies in place as the release has them:
   `set BUILD_DIR=app\out\switch-pgo-gen` / `set "CMAKE_EXTRA=-DBANJO_PGO=generate"` / `set IN_PLACE=ON` /
   `tools\build_switch.bat`. The Switch defaults (CG-13, the cleared near-exact set, no trapping math) hold in every
   Switch folder, so the instrumented and the optimised build compile the same code.
2. Play it (the console, or Ryujinx with the banjo-ult data folder: the counts are code paths, not timing): the
   title, the menus, Showdown Town on foot and in the vehicle, a challenge. Then `python tools/pgo_merge.py <the pgo
   folder of streams> pgo-inplace --gen-build app\out\switch-pgo-gen\renut.nro` (one stream unless `--all`).
3. `tools\build_switch_release.bat`: app\out\switch-pgo-use\renut.nro (LTO, the profile, no game shader inside: it
   reads deko3d_glsl.bin beside itself, as the installer's package). Its log must have no "mismatch" line.

The everyday NRO of this package is app\out\switch (no PGO, no LTO, the shaders inside).

## The keys decided from the log, and the stereo check (step 4; 2026-11-19)

From N&B's own PC logs in gameplay windows (cg13_town2, 60 windows; verify_ne, 58: Showdown Town in the vehicle and
on foot), as the base's porting prompt (step 5) says:

| Key (base default) | The line | N&B | Set |
|---|---|---|---|
| `sasrt_gpu_skip_inert_draws` (true) | "[sasrt gpu] GPU round ..., in the last 10 s: sasrt_gpu_skip_inert_draws N" | 0 in every window of both runs | false in N&B's toml |
| `banjo_renut_tags_check` (32) | "[renut] recording from marks ..., N whole checks, M chunks changed without a mark" | M = 0 over both whole sessions (5,120 whole checks in one) | 256 in N&B's toml |
| `ultimate_renut_replay_inherit_same` (true) | "[renut] replays with the inherited words as recorded ...: N of M" | never printed: the line counts replays with inherited words, and `banjo_renut_replay_inherits` is off (off-by-default.md section 1), so the key is never read | the default (inert) |

**The stereo check** (audio_channel_census, PC, the same route, 330 s; audio1 and audio2): the game fills FL, FR, C,
RL and RR in every frame (LFE never) with the default speaker configuration, and the same with
`audio_speaker_config = 1` (analog stereo: "speaker config 00000001" in the line). N&B mixes its five channels
whatever it is told, so the stereo saving needs a per-game patch (blur-nx's tools/stereo_mix.py): worth it only if the
console's CPU table puts the mixer near the top (a next step with a console profile).

## Ryujinx: the picture against renut-nx 1.0.2 (step 4; 2026-11-19)

Both runs with out/emu/emu_cmp.toml (1.0.2's shipped settings, logs on, dynamic resolution off, captures every 15 s, A
presses 135-225 s, the sticks from 330 s), 480 s each: **ref102** (1.0.2's NRO fdd75aa177e5 with its own installed
package and cache, data folder banjo-ult-ref) and **new1** (this branch's Switch build of 19:39: the cleared not-exact
forms, the copies in place, the shaders inside, the 53,113-variant cache; data folder banjo-ult). The emulator's save
starts a new game, so the route is the intro, the title, the menus, the loading screen, the opening story (the sepia
films, Spiral Mountain, Banjo's house) and gameplay on foot (new1 reached it at 434 s; it loads faster).

Matched scenes side by side (out/emu_runs/cmp_a.jpg, cmp_b.jpg; sheets ref102_sheet.jpg, new1_sheet.jpg): the same
picture in every one: the loading screen, the films' sepia grain and frame, 1.0.0's dialogue boxes and Andy Bold, the
fur, the lighting and depth of field, the HUD. The captures are not the same frames (the two builds run at different
speeds), so the only numerical check is the loading screen: its differences (out/emu_runs/cmp_loading_diff.png) are
the animated parts alone (the background's pan, the TV glass's bands, the cog); the frame, the text and the layout are
the same. No difference to take to RenderDoc.

new1's log: the DKSH cache (53,113 variants) used, 4 shader compiles in the whole run, no crash, guest memory 594 MB,
12,869 pipeline objects. One finding: the forms self-check reported near-exact #2's NaN conversions as differences
(and wrote banjo_selfcheck_failed.txt next to the NRO): fixed (2ed2e120; a handoff).

## The hot-function round and the final PC verify (steps 5 and 6; 2026-11-19 evening)

**Profile:** prof1, Ryujinx, 600 s, `switch_profile_stacks = true` (out/emu/emu_prof.toml; the emulator starts the
NRO without argv, so the profiler writes sdmc:/switch/rex_profile.log, not the NRO's logs/rex/): 54 windows, the
intro, the menus, the opening story and gameplay on foot. `tools/switch_hot_bulk.py ... --merge --min-samples 3`
added 40 functions (their lines say "Ryujinx samples") and, by the base's rule, took out the list's 59 copies under
40 instructions with fewer than 50 samples: 506 -> 489 listed, 487 made (two cannot be copies, as before).

**Final PC verify** (the shipped configuration: the cleared codegen, the LR elision off, sub_82C41318 and sub_8221BFA0
off; `banjo_hot_bulk_lines` on, a line per reached bulk copy, new): two routes.

| Run | Route | Compared | Bulk reached | Differences |
|---|---|---|---|---|
| verify_final_a | the save: Showdown Town in the vehicle, out and back (600 s) | 245.2 million calls (159.0 million not comparable, same stores) | 469 of 487 | 0 |
| verify_final_b | a new game (the save moved aside): the intro, the menus, the opening story, on foot (540 s) | 180.1 million (107.4 million not comparable; 4 not comparable with stores that were not the same before the stop) | 455 of 487 | 0 |

Both: the r1 mirror (714.5 / 569.7 million calls), the stack forwards, the generated-code self-checks and the
flush-mode flow: 0 differences. **All 40 new copies were reached** (on one route or both); 9 of them only in calls
that reach a function outside the copy (the guard compared their stores up to that call). Open: route B's 4 not-
comparable calls whose stores before the stop differed (no difference line; a thread race is the usual cause).

## The console regression (2026-11-20)

The user's console (handheld, stock clocks CPU 1020 / GPU 460.8 / RAM 1600, four-core forwarder) ran this branch's
NRO (fe622e97) slower than renut-nx 1.0.2. Runs, all on the console:

| Run | Build | Scene | fps | Render thread | GPU |
|---|---|---|---|---|---|
| the user's, logs on (2.5 min) | this branch | Showdown Town, about 5,000 draws a frame | 10-12 | 15-19 us a draw (core 1 saturated) | 14-16 us a draw |
| 1.0.2's of 30 Oct (memory) | 1.0.2 era | the same | 17.6-19.6 | 7.5-8.1 us a draw | about 8 us a draw |
| block 1 (8.5 min, 1 ms stack sampling) | this branch | the title's attract loop | 11-27 | 17-21 us a draw | 27-46 us a draw: GPU-bound |
| block 2 (1 ms stack sampling) | 1.0.2 (62ab5e4c) | the same loop | 30-40 (8 windows, then froze) | | |

**The GPU: the generic variants.** This tree's translator puts much more behind specialization constants: the generic
form of each shader (all constants at their defaults) is far bigger than 1.0.2's (tools/deko3d_glsl_compare.py, the
same uam on both: pixel shaders 2.52 M -> 6.67 M instructions over 3,834 shaders, +164 %, mean GPRs 24.5 -> 37.9;
vertex shaders +24 %). The console drew with the generic variants for good: 20,221 stages given the generic one at
the start, then only 148 read from the DKSH cache and 43 swapped in over 8.5 minutes, compiles about one every 6 s
(throttled). The cache holds the pipeline lists' variants (52,975 of the new list's 53,130), so the run-time keys are
not the ones tools/deko3d_variant_cache.py derives from the lists. 1.0.x had as many generic stages (27,713 in a
6-minute session), but its generic forms cost about what its specialised ones did.

**The CPU side:** the render thread's functions are the same mix in both builds (FastDrawLocked, memmove,
FillTextures, PushFloatConstants, Replay, ConvertVertices, PagesClean ...: tools/profile_threads.py); one new cost,
census::On() (1.7 %, now inline). Core 3 was saturated (0.7-1.7 % idle against 25-36 % in 1.0.x: the compiles), the
ring cleaner there fell behind and the render thread cleaned the upload ring itself (sasrt_ring_cleaner_take_over:
375 ranges, 1.0.x 0). This NRO had no PGO and no LTO (1.0.2's had both).

**The fixes in progress:** plume logs every variant it falls back for (banjo_deko3d_log_misses), a Ryujinx run
collects them (the renderer's keys are the same there), the DKSH cache gets them (--runtime-log); PGO and LTO for
every library but uam (the user's: uam stays -O2; the function registration file stays -O0: it runs once at boot and
optimising it costs 20 minutes a build).

**The console tools:** the virtual pad (sys-botbase) did not reach the game's title in handheld mode (only the Home
menu); the attract loop needs no input. 1 ms stack sampling froze 1.0.2 after two minutes (and sys-botbase with it:
the console needed the user); prefer no sampling or a longer interval for the next console runs.

### The fixes and their emulator checks (2026-11-20, 03:00-06:10)

**PGO and LTO.** The instrumented NRO (every library in PGO but uam), two Ryujinx training runs (the title loop; the
menus, the story and gameplay on foot with 1 s presses: the instrumented build at 8-11 fps misses 0.2 s ones), 663
.gcda files in pgo-inplace/, then the optimised NRO with LTO (`out/switch_pgo_use.bat`: 23 minutes; LTO's temporary
files on C:'s scratchpad, a path without spaces). NRO 84a50b1d (118 MB); Ryujinx: the same route and pictures, the
forms self-check 45,134 of 45,134.

**The DKSH cache.** v1 (the evening's) held 0.2 % of the variants the renderer asked for. `--round-guess` (the GPU
rounds' run-time words for list records made before them: the plain fetches' 950 / 958 / 966 + r and 926's bit 31;
the vertex shaders' byte-order words 937 / 939 and 932 = 7) and the run-time logs of four runs (`--runtime-log`;
banjo_deko3d_log_misses names every fallback) gave v3: 105,814 variants, 1.18 GB. The same route in Ryujinx with the
PGO + LTO NRO:

| Cache | Stages given the generic variant | Compiles (variants not in the cache) | Cache hits |
|---|---|---|---|
| v1 (the emulator's copy, with the training runs' compiles appended) | 25,651 | 54 | |
| v2 (the round guess, pixel shaders) | 17,160 | 163 (64 named: all vertex shaders with 937 / 939) | 8,952 |
| v3 (+ the vertex shaders' words) | 10,401 | 16 | 9,490 |

The generic count also counts the variants read from the cache in the background (6,763 in the v3 run: milliseconds
each once the uam thread is not compiling). On the console a compile took 3.9 s of core 3; the user's session made
about 70.

**Not measured yet: the console.** It froze with 1.0.2 at 02:03 (block 2, 1 ms sampling) and waits for the user.
tools/card_swap_when_free.py puts the card back to this build once N&B is closed (renames: the -ult files in place,
1.0.2's as -1.0.2) with renut.toml = the shipped settings, logs on, banjo_deko3d_log_misses on (for the next cache).

**The remaining 10 % (specialised pixel shaders against 1.0.2's).** The console's 13 pixel run-time variants, compiled
with their own constants and with one base feature's constant changed (scratchpad rt_variant975.py: the same uam):
13,223 instructions as run; 25,303 with 975 = 0 (the shader round off: it is what makes the variants small); 11,900
with 976 = 0 (vivapinata_shader_noearly: Renut's early alpha test left out; the same pixels); 990 and 914 change
nothing; 1.0.2's same shaders 11,938. So the early alpha test is the 10 %: about 110 instructions a shader, for the
quads it discards early. Per game by the base's rule (off-by-default.md section 4): the console decides, A/B 151 in
renut_ab.toml (the 2026-11-20 cache holds both halves, --noearly-ab); off half faster: vivapinata_shader_noearly =
true.

**The console, 2026-11-20 07:56 (block 3: the PGO + LTO NRO 84a50b1d, cache v3, no sampling, the title loop, 10
minutes).** Most windows 25-30 fps (1.0.2's block 2, sampled: 25-30), the render thread 12.6-13.7 us a draw early and
14-17 later (block 1, last night's build: 17-21), the GPU 18-31 us a draw (block 1: 27-46). Left: 86 compiles in 10
minutes (4.55 s each; 53 named misses with the 3D fetch words 934-936, which no guess makes), core 3 still 0.5-0.9 %
idle, and the slower later windows. The run's deko3d.log and renut log went into the next cache (--runtime-log). A fair
CPU comparison needs 1.0.2 without sampling on the same loop.

**The compile cap (the user's: the shader compiler at most 10 % of core 3).** Every compile, whoever waits for it, adds
a rest of 9 times its length (banjo_deko3d_background_compile_share 10, at most 60 s outstanding); a compile a draw
waits for, and a cache read, never wait. The first build (13c1cf67, not shipped) looked only at the queue's front: a
compile the prebuild waits for is queued ahead of the background ones too, so a draw's compile could sit behind one
for the rest. The second (b830247a) takes the first such job anywhere in the queue, and lifts the rest for the
prebuild's compiles while the render thread waits for a pipeline the prebuild builds (TakePrebuilt) and at shutdown
(plume::Deko3DHurryCompiles).

**The console, 2026-11-20 09:37 (block 4: 84a50b1d, cache v5, A/B "151,83,1" with 30 s halves, 12 minutes).** No
answer for 151: the title loop repeats every ~180 s (110-120 s of the 3D title scene at 1,300-2,100 draws a frame,
then 60-70 s of a light scene at ~150), the same as the A/B's cycle (3 targets x 60 s), so each target's halves kept
landing on the same scenes (151's "on" halves averaged 11 draws a frame, its "off" halves 482; the control's halves
differed by 52 % in GPU time). The one cycle where both of 151's halves fell in the light scene: 7.89 against 7.61 ms of
GPU, noise. The next 151 test: that target alone, 10 s halves, and only the pairs whose draws a frame match
(tools/ab_halves.py). The 3D scene's 10 s lines: render thread 16-28 ms a frame, GPU 29-42 ms, 26-30 fps.

Compiles: 74 in 12 minutes (4.46 s each: 330 s, half of core 3), 31 for the prebuild and 33 in the background, none a
draw waited for; background rests 52.9 s. All 74 were appended to the card's cache file (2,184,341,724 bytes now).
48 named misses against v5; tools/deko3d_miss_coverage.py --nearest puts them down to pipelines the shipped list never
had: the bool words 1243-1249 in combinations the list lacks (44), 906 (the inputs the vertex shader writes as zero:
another vertex shader with the same pixel shader, 28) and the vertex shaders' 955 (4). The next cache: the card's
grown pipeline_list.bin and this run's deko3d.log (--runtime-log).

The cap's second build (NRO 845d01fb, PGO + LTO) in Ryujinx, 2026-11-20 11:00-11:17 (the comparison route, 450 s):
self-check 45,134 of 45,134, the usual 152 NtQueryInformationFile errors, 27-30 fps late in the run. The first run
compiled nothing (the emulator's cache has the route); the second, with A/B 151 at 10 s halves, compiled 2 (one in
the background, 2.9 s; one the prebuild waited for, 1.0 s, after 25.8 s of rest), no draw waited. The hurry path
(the render thread waiting for a prebuild's compile) did not come up: the console decides.

Cache v6 (2026-11-20 10:43): v5 merged with the card's grown pipeline_list.bin (3.50 million pipelines, from 2.30)
and block 4's deko3d.log and renut log: 197,486 variants (1,917 compiled, 0 failed), 2,195,149,376 bytes,
out/deko3d_shaders/deko3d_shaders_v6.bin.

The 3D title scene's lines (800 draws a frame or more) of the console runs: 84a50b1d (blocks 3 and 4) 13.3-13.9 us of
render thread a draw and 23.4-24.8 us of GPU; 1.0.2 sampled at 1 ms (block 2) 11.8 and 22.3.

**Block 5 (renut-nx 1.0.2, logs on, no sampling, 2026-11-20 10:07): the GPU hung after 111 s**, in the 3D title scene
("[deko3d] a fence has not signalled after 2 s ... 16 s" from 10:09:24; only the audio threads logged after it). The
display froze, sys-botbase died on the next screenshot requests, and the user restarted the console at 11:57. Block 2
(1.0.2 with 1 ms sampling) froze the same way about 2 minutes in, so the sampling was not the cause. Both runs had
log_level "info", which 1.0.2 does not ship with (the user plays it with logs off without trouble): likely a
logs-only path of 1.0.2 (unproven). The ultimate build ran 12 minutes twice with logs on. No more 1.0.2 console runs
with logs on.

The 50 s of 1.0.2's 3D scene before the hang are the unsampled reference (out/card/c5_102): render thread 11.0-12.8
us a draw (median 11.2), GPU 20.2-26.3; the ultimate build 13.3-13.9 and 23.4-24.8. Per thread over the first 3D
stretch of each run (1.0.2 60-110 s, block 4 160-270 s): total 209 % and 212 %, the render thread 43.5 % and 49.9 %,
Main 44.7 % and 37.8 %, the game threads 79.6 % and 79.7 %, a core-2 helper (priority 0x3B) 0.8 % and 6.5 %. So
the total is level; the render thread is about 15 % heavier a draw, which a sampled run of the current build (5 ms,
out/card/renut_prof5.toml) is to break down.

tools/profile_per_draw.py (second version) lines the profile's windows up with the renut log's lines by the clock (the
first version counted lines, and its draws were the "specialised bools: N draws" count at each line's end). On the
sampled runs of the night: last night's build (no PGO) 16.0 us of render-thread user code a draw, 1.0.2 8.0 us, the
same scene (1,230 and 1,390 draws a frame).

**The console, 2026-11-20 12:14 (block 6: the cap's second build 845d01fb, the card's cache = v5 with block 4's 74
compiles appended, A/B 151 alone with 10 s halves, 12.4 minutes).** The cap holds: 49 compiles of 1.27 s on average
(62 s of compiling in 740 s, about 8 % of core 3), 540.9 s of rests, 15 in the background and 34 the prebuild waited
for, none a draw waited for; the uam thread 5.9 % of core 3 over the last 10 minutes (blocks 3 and 4: 26 % and 20 %).
No stall, no fence timeout. The attract loop showed other content this time (the vehicle demo, "RETURN TO GARAGE",
and quiet stretches of 15 draws a frame instead of 150): mostly a locked 30 fps, the render thread 6.4-6.8 us a draw
and the GPU 11-16, all threads 101 % of 400. Those figures are of a lighter scene and do not compare with blocks 3-5.
A/B 151 (tools/ab_halves.py --pairs, 8 heavy pairs): the off half (no early alpha test) GPU -1.4 %, render thread
-2.0 %: noise, so vivapinata_shader_noearly stays false.

**The console, 2026-11-20 12:30 (block 7: 845d01fb, stack sampling every 5 ms, 12 minutes).** No freeze at 5 ms. The
same content as block 6 (a locked 30 fps; heavy stretches of 1,150-2,100 draws a frame at 6.1-7.1 us of render thread
and 9-15 us of GPU a draw, quiet ones of 15 draws a frame). Since the console's restart the attract loop shows other
content than blocks 3-5 did (their quiet stretches drew 150 a frame and the heavy ones cost 29-42 ms of GPU against
15-23 now), so blocks 6-7 compare only with each other. tools/profile_per_draw.py over the run (890 draws a frame on
average, the quiet stretches included): 7.28 us of render-thread user code a draw: FastDrawLocked 1.46, Replay 0.78,
memmove 0.61, FillTextures 0.46, PushFloatConstants 0.43, ConvertVertices 0.35, WholeGeometryTry 0.34, PagesClean
0.30, DrawStateLocked 0.27, FastExportReads 0.17, ContentHash64 0.12 (last night's build without PGO: FastDrawLocked
2.45, FillTextures 0.94, PushFloatConstants 0.83, and PmullHash 0.44, ExpandElement 0.51, census::On 0.28 now gone).
The link registers of the render thread's samples (the callers of its leaf functions such as memmove): FastDrawLocked
25 %, texture content hashing (ContentHash, CheckedHash) 12 %, DrawStateLocked 10 %, Replay 8 %, texture level reads
(ReadLevelParallel) 8 %, ConvertVertices 5 %. The next render-thread items: the texture content checks and reads
(memmove, the hashes), then the vertex conversions 1.0.2 did not have.

**Correction (2026-11-20 14:15, from culdcept-nx's note): blocks 6 and 7 ran overclocked.** Their profiles' clock lines
read CPU 1963.5 MHz, GPU 998.4 MHz, memory 2733 MHz (rex_stderr: the starting point already memory 2733 and GPU 307.2;
the game's own request, configuration 0x92220008 = GPU 460 / EMC 1331 as in every block, came out as GPU 998.4). Blocks
3-5 ran at stock: CPU 1020, GPU 460.8, memory 1600. The console came back from the user's restart (11:57) with an
overclock profile active; N&B asks for none of it. So blocks 6-7's halved costs per draw and locked 30 fps are the
clocks, not the build; whether the attract content also differs (quiet stretches of 15 draws a frame against 150) is
open. What stands from blocks 6-7: the compile cap (a share of time: 8 % of core 3 compiling, the uam thread 5.9 %),
the in-run A/B 151 (both halves at the same clocks: noise) and the 5 ms sampled function breakdown (shares within the
run). Every comparison from here on checks the clock line first.

**Block 5's cause found (2026-11-20 14:55): renut-nx 1.0.2 lacks the base's port 64**, sasrt-nx's GPU hang fix: when
dynamic resolution steps with its settable codes 4 and 6, a texture over a depth resolve keeps its old-size image and
CopyDepthWords copies the new, larger size into it (a GPU write past the image; "a fence has not signalled", no deko3d
error). Both hung 1.0.2 runs stepped constantly at stock clocks (block 5: 720, 684, 648, 612, 576, 612, 648 between
10:08:23 and 10:09:14; the hang at 10:09:22), because the title scene's GPU time sits at the 34.5 ms threshold; at the
user's overclock the GPU stays far under it and the resolution never leaves 720, so 1.0.2 does not hang there. Not the
logs and not the shader files: 1.0.2 loaded its own cache (28,688 variants) and GLSL package (4,731 shaders), compiled
nothing, and its pipeline list is the release's plus 206 records of its own format (v5, 965 bytes). The ultimate build
has the fix. 1.0.2 comparison runs from here on: banjo_renut_dynamic_resolution = false (648 lines) in both builds.

## The render thread against renut-nx 1.0.2 in Showdown Town (2026-11-20 afternoon)

Same spot (the user's save, the spawn at the garage, input_script A presses, then still), 540 lines fixed, dynamic
resolution off (1.0.2's port-64 hang), stock clocks, unsampled; steady city lines:

| Build | fps | render thread a frame | a draw | draws a frame | GPU a frame |
|---|---|---|---|---|---|
| renut-nx 1.0.2 (62ab5e4c) | 26.5 | 36.6 ms | 7.55 us | 4,854 | 32.7 ms |
| ultimate d5cffbfb (this morning's PGO: title + intro) | 20.8 | 46.2 ms | 8.99 us | 5,207 | 39.0 ms |
| 72e4bafd (the fixes below, the old profile: the changed functions lost it) | 17.7 | 56.5 ms | 10.17 us | 5,549 | 35.5 ms |
| 5d1c60e9 (the fixes, PGO retrained in Showdown Town) | 23.1 | 42.4 ms | 8.39 us | 5,047 | 35.4 ms |

1 ms sampled runs of 1.0.2 and d5cffbfb in the city, named by source function with inlining (tools/profile_inline.py;
1.0.2's ELF is banjo-nx/app/out/switch-dist/renut, md5 of the release NRO 62ab5e4c2253; pkgstage/renut_release.elf is
1.0.1's): 7.29 against 9.22 us a draw. By area: the atomics, censuses, GPU round checks and spec words +0.42, draw
submit +0.38, plume's command list +0.26, mem* / NEON / byte swaps +0.30, the rest +0.6 spread thin; the copies'
proving is the same (0.62 / 0.63: its +0.13 under ProveCopyRange was attribution). The hot functions grew: 1.0.2's
FillTextures one copy of 16 KB, the ultimate's three of 18-20 KB (constprop clones), PushFloatConstants two, Replay two,
FastDrawLocked 22 -> 42 KB, from the same sources nearly.

The fixes (1f06bd5e, 17984e41): the censuses compiled out on the Switch (BANJO_SWITCH_DIAG_CENSUS OFF: their checks ran
with the keys off), sasrt::gpu_round::On() one byte per item (RefreshEffective at the latch and each A/B switch),
[[gnu::noclone]] on FillTextures, PushFloatConstants and Replay, the NaN flush key read once a push; N&B's defaults:
banjo_renut_const_nan_flush and banjo_renut_vertex_finite off (1.0.2 had neither), stream redeem and the Dishonored
readable check off (earlier). Built with the old profile they ran 13 % slower: every changed hot function lost its
profile (GCC drops it when the function's code changed). PGO retrained (the instrumented build, 16 minutes in Ryujinx
loading the user's save into Showdown Town, then input_script_sticks; out/emu/emu_train_city.toml; the old profile in
pgo-inplace-titleintro-2026-11-20): 23.1 fps. Left: +11 % a draw and +4 % draws a frame against 1.0.2.

## The render-thread batch (2026-11-20 evening, a390d9b2)

The audited table of 64 (44 batch items, 20 experiments; c15 city profile 8.88 us a draw sampled at 1 ms, 8.39
unsampled) went through against the code. What went in, each with its own A/B target where it can be switched:

| Item | What | A/B |
|---|---|---|
| 1 | spec-word memo (sasrt rt2 spec_memo) | 306 |
| 2 | -fno-threadsafe-statics on ultimate.cpp, banjo_renut_tuning.cpp, render_thread.cpp | build |
| 3 | g_ab_off_plain (a plain int for the draw path); Latch() tests g_latched.ready | - |
| 4 | the report's per-draw counters skipped with log_level "off" (release only) | - |
| 5 | export reads kept on the fast path (sasrt rt4 export_reads_fast) | 305 |
| 7 | held-export granule bitmap before GetStreamSource's map look | 307 |
| 8 | PagesClean NEON tail for 1-7 pages | 304 |
| 9 | whole-repeat answers for the kick's last four meshes | 309 |
| 11 | constant runs one or two groups apart pushed as one | 308 |
| 16 | PrefetchShaderBehind skipped for a stage whose word is the last op's | 301 |
| 18 | FillTextures: a call that does not ask "same as last" skips the per-slot compare and copy | 300 |
| 22 | the render thread's take-over of the cleaner's ranges off (594 of 1,320 a 10 s) | key |
| 29 | slot-last answers skip the epoch buckets while g_texture_bind_gen holds (TextureObject's rule) | 300 |
| 39 | depth bias left alone when its packets did not change | 302 |
| 54 | two more ring prefetch lines | 303 |
| 58 | experiment: banjo_renut_ring_spin_us (0 = off) | key |

Found already done in the code (the cost in the profile is the lines they touch, not repeated work): 6 (the export
checks run only while exports are pending), 12 (PushSharedConstants pushes only the lines written since the last push),
13 (each copy proven once per kick interval: ProveCopyRange's first compare), 17 (PrefetchResourceObjects skips the
objects the replay holds), 19-21, 27, 28 (plume: bindTextures skips an unchanged set and marks only rebound handles,
setPipeline returns for the bound pipeline and binds only changed stages and state groups, buffers rebound only when
dirty; noteDepthWrite is three loads). 15 and 16's rest are the shader record's first-touch miss: a memo would move it
to the draw's later reads of the same line. 38: command memory is 64 KB blocks already; reserve's cost is per command.
40: the skin table reports 0 without room. 42/43 not taken: one NRO with the A/B keys (the user's rule). 10 N/A.
The rest (14, 23-26, 30-37, 41, 44) are 0.01-0.03 us each on paper and touch the record/replay format; left for after
this measurement. memcpy (0.37 us, exp 45): its callers cannot be read from the stacks (a leaf without a frame record
is folded into its caller; scratchpad leaf_callers.py decodes the BL before each return address).

### The batch's second commit (6a8d3760): all of sasrt-nx's rounds 2-4, items 14 and 31

The user: "44 in the batch plus as many experiments as possible and all of sasrt's". sasrt-nx's 26 render-thread items
(its handoff docs/handoff/rt_rounds_1120) are a second table in sasrt_gpu_round.{h,cpp} with its keys, defaults and A/B
numbers (339 + i; 401/402/403 a round at once; our first table keeps the base's 90 + i). The eight items of a390d9b2
moved back under their keys (their temporary A/B 302-309 are gone; 300 and 301 remain mine). In with its code:
drop_throttle, color_alias_memo, readable_memo (the SDK's rex::memory::g_page_access_generation in xmemory.cpp, declared
in draw.cpp only; N&B's readable check is off, so it never runs here), bind_layout_narrow (no restore-copy term: N&B
has no EDRAM depth restore), texture_memo_two_way, surface_memo_256, state_id_crc, spec_key_skip, ring_lock_once,
inert_keeps_chain, and off as sasrt-nx ships them: pipeline_four_way, stream_memo_two_way; plus the menus' flash fix
(sasrt_resolve_alias_previous). Nothing in N&B's code for: slot_index and words_owner_table (sasrt-nx's copy-back and
restore lookups), place_wait_unlocked and place_spin (split-screen places), host_packets_split (split views),
repeat_recorded_index (no ReplayIndexBuffer here; WholeGeometryTry and WholeRepeatHit both read the live header);
round 1's later view_surface_memo and words_owner_memo likewise (no EDRAM views in GetHostSurfaceLocked).

Batch #31 was not a bind: "1,005,954 binds of a depth resolve's float target" is CountDepthFloatSampled, a call into
banjo_renut_tuning.cpp from every texture memo answer; it and CountDepthWords are inline and skipped with logs off.
#14: TextureFetch::FromGuest in two NEON loads and reversals. Also checked and already so in the code: 24 (ApplyState
applies the op's dirty chunks only), 25 (the fetch pool keeps each buffer's copy across draws and kicks), 26
(g_last_stream_sources), 35 (CachedIndices per buffer), 36 (FetchSpecWords is a few loads; the spec memo skips it),
41 (SetViewport writes only changes). Left: 23, 30, 32-34, 37, 44 (per command buffer or per frame, or plume's
recording format; under 0.02 us each on paper).

Experiments in this NRO (toml keys, off unless said): banjo_renut_ring_spin_us (#58); sasrt_rt4_pipeline_four_way,
sasrt_rt4_stream_memo_two_way; existing keys that are the table's experiments: banjo_renut_upload_cached (#56),
banjo_deko3d_kick_submit (#46), banjo_renut_core_layout (#60, hung before). The build-flag experiments (#47 cold split,
#52 -O2, #57 alignment, #62 one LTO partition, #63 partial training) each need their own NRO.

### The batch on the console (2026-11-21 01:05-01:40, NRO ef5fe0f4, PGO retrained in the city)

| Run | fps | render a frame | draws a frame | render a draw | GPU a frame |
|---|---|---|---|---|---|
| c16 batch ef5fe0f4 | 19.28 | 51.6 ms | 5,627 | 9.17 us | 35.5 ms |
| c17 5d1c60e9 (bracket, right after) | 24.26 | 41.1 ms | 4,842 | 8.48 us | 32.4 ms |
| c18 batch, A/B run (four-way on in the "on" halves) | 20.15 | 49.4 ms | 5,256 | 9.40 us | 34.2 ms |

The batch was ~10 % SLOWER a draw. Draws a frame differ run to run (4,842-5,627): the game autosaves on reaching
Showdown Town ("Saving content"), so every run moves the save on (day/night, NPCs); compare per draw, or in-run A/B.
In-run A/B (6 s halves, --pairs, render us a draw, on vs off): 300 -0.9 %, 301 -0.5 %, 401 (sasrt rt2) +0.7 %,
402 (rt3) -1.0 %, 403 (rt4) +1.1 %, 359 four-way +1.5 % (keep off): all within about 1 %, so not the regression.
CAUSE (log): item 22, sasrt_ring_cleaner_take_over false: the render thread cleaned 0 ranges itself (500 a 10 s
before) and WAITED instead (done_cv_) for the cleaner thread, which runs at the lowest priority on the system core.
The audit's "waits instead of re-cleaning" was wrong: the clean is cheap, the wait is not. Test: the same NRO with the
key true (out/card/renut_city_takeover.toml).

Follow-up the same night: c19 (the batch NRO with the take-over back on through the toml): the render thread cleaned
515 ranges itself again, but 9.10 us a draw (from 9.17): the take-over was a small part. Draws a frame rise as fps falls
in every build measured (1.0.2 26.5 fps 4,854; 5d1c60e9 23-24 fps 4,842-5,047; d5cffbfb 20.8 fps 5,207; the batch
19-20 fps 5,256-5,627; 72e4bafd 17.7 fps 5,549): replayed draws only (immediate, full-path, fur, clears the same), so the
game keeps something alive by frames; more draws a frame are a result of a slower frame, not its cause.
c20 (the batch, 1 ms stacks, against c15 = 5d1c60e9 sampled; tools/profile_inline.py --from 260): 9.17 against 8.89 us
a draw. Gains as meant: DrawStateLocked -0.11, FastExportReads -0.13, WholeGeometryTry -0.09, PagesClean -0.09 (its
NEON part shows as vld1_u8 +0.09), int atomic loads -0.07. New costs: SasrtSpecAnswersOf +0.12 (the spec memo hit 22 %;
SasrtSpecWordsOf 0.083 in both), byte atomic loads +0.10 (the second table's On() checks), memset +0.05 (a GpuStreams
zeroed every fast draw for export_reads_fast), FastDrawLocked +0.13, bindVertexInput +0.08 (not explained),
SetShared<u32> +0.06, Latch +0.04 (the counts checks, logs on). Also LTO kept one 14 KB BindStreams (5d1c60e9 had a
6 KB specialised copy for the fast path) since the fast path passed the export streams at its one call.
Round 2 (08b1a669): take-over on, spec memo + spec key skip + ring lock off by default for N&B, GpuStreams only when
used, BindStreams' constant call split off, On() answers plain bytes.

Round 2 on the console (03:19-03:36): 5da00249 20.07 fps, 9.67 us a draw; 5d1c60e9 right after (bracket 2) 24.06 fps,
8.54 us. Two batch builds with two separate city trainings both 9.1-9.7 against 8.48 / 8.54: the code, not PGO luck.
Stacks by callee per draw (c20 vs c15): FastDrawLocked's own frame 2.94 against 2.47 us, WholeGeometryTry under it
0.49 against 0.31, the general BindStreams 0.11 (shown as "State", its first inlined line). sasrt-nx's items sit in
FastDrawLocked (40.5 -> 43.9 KB, against the A57's 48 KB L1I): each check is cheap, the code is not.
DECISION (a8b56d4a, "the lean batch"): 5d1c60e9's sources for draw.cpp, render_thread.cpp, video.cpp,
command_buffers.cpp, surfaces, sasrt_gpu_round and xmemory.cpp, plus only: g_ab_off_plain, A/B 300 and 301,
export_reads_fast (A/B 305, with round 2's GpuStreams/BindStreams fixes), depth_bias_fields (A/B 302), the counts
switch, plain answer bytes, the ring-spin key. The rest of sasrt-nx's rounds 2-4 is out of N&B (no measured gain here).

### The draws-a-frame puzzle solved (2026-11-21 05:00-05:19)

Every batch build drew 13-16 % more a frame than 5d1c60e9 in the same spot (the camera's clip plane identical in all
runs). The lean NRO 3423e703 with only sasrt_resolve_alias_previous = false (sasrt-nx's menu-flash fix, textures.cpp):
24.06 fps, 4,825 draws a frame, 8.57 us a draw; with it on 21.90 / 5,523 / 8.24; 5d1c60e9 23.97 / 4,875 / 8.53.
The fix hands a read of a resolved page in another format the page's earlier destination; N&B evidently reads that
back and draws more (replayed +14 %, immediate ~2.7x). Now off for N&B (sasrt-nx agrees: per game; in SASRT's race it
changes nothing). Round 2's NRO with it off still drew 5,305 (immediate draws 2x): another of sasrt-nx's rounds 2-4
items moves N&B's draw stream too (likely the colour-alias memo, also a resolve read); the lean batch dropped them all.
So: per-draw comparisons between builds whose draw counts differ were confounded all night; the c18 in-run A/B
(same draws in both halves) stands. The lean batch equals 5d1c60e9 (24.06 / 23.97 fps). Against 1.0.2 (26.5 fps): the
GPU (32.4 vs 32.7 ms) and draws a frame (4,825 vs 4,854) now match; the render thread a draw (8.57 vs 7.55) is the gap.

### The lean batch against 1.0.2, by source function (c27 vs c11, both 1 ms sampled, --from 260)

8.19 against 7.29 us a draw. Lean ahead in FastDrawLocked (-0.18) and Replay + PrefetchShaderBehind (about even).
The rest is mostly what 1.0.2 does not have: the base's spec words (SasrtSpecWordsOf + SasrtPlainFetchMask +0.16:
shader specialisation, a GPU saving), plume's vertex input mask (bindVertexInput +0.06), the tracked texture
invalidation (markSampled +0.05), bindGraphicsBuffers +0.05, the GPU round's On() checks +0.04, memexport binds
+0.04; the report counters +0.12 with logs on (0 in the release: they follow log_level); memcpy +0.13 (copy volumes
a draw the same as 1.0.2's in the logs: callers not found). GPU a frame now equals 1.0.2's (32.4 vs 32.7 ms).
In-run A/B of the GPU round items that change no shader variant (c28; 6 s halves, render us a draw on vs off):
122 vertex input mask +0.6 % (fps -1.1 %), 132 +0.2 %, 133 -0.2 %, 134 -0.2 %: noise; defaults unchanged.
The spec-word items cannot be A/B'd cleanly (their off half needs other shader variants than the cache holds).

### N&B picks its draw level early in each run (2026-11-21 06:30) — the confound behind the night's numbers

lean2 (80b43288, sasrt_resolve_alias_previous off by default): 24.08 fps, 4,867 draws a frame, 8.50 us a draw (c29);
the same NRO with banjo_renut_draw_counts = false (c30): 21.46 fps, 5,627 draws a frame, 8.26 us a draw. Counters
cannot change what the game draws, so the draw level is not the code's doing. Across all 19 city runs the level is
set in the first 10 s line in the town and holds for the run (in-run A/B halves never differ): a low state ~4,800-4,900
(5d1c60e9 x3, lean alias-off, lean2, 1.0.2 c9) and a high state ~5,250-5,630 (every batch run, lean with the alias fix,
lean2 counts-off, and every 1 ms SAMPLED run: c11 1.0.2 5,266, c15 5d1c60e9 5,037, c27 lean 5,538). The loading-to-town
line's fps leans with it (>= 10.4 fps -> low in 6 of 7; <= 10.2 -> high in 8 of 9). The game's fence polls are ~0 a
10 s (not fence gating). The high state's extra draws are replayed +14 % and immediate ~2x. So: compare builds per draw
(render us a draw), or bracket A/B/A until both land in the same state; an fps difference between two runs is mostly
which state each landed in (~10 %). What the game varies (crowd, LOD, effects) and on what timing is still open.
Per draw, low state: lean2 8.50, 5d1c60e9 8.48-8.54, 1.0.2 7.55; high state: lean2 counts-off 8.26, lean (alias on) 8.24.
Correction to "the draws-a-frame puzzle solved" above: with sasrt_resolve_alias_previous on N&B landed in the high
state 5 of 5 times and off low 2 of 2, but the level is a per-run choice the counters' switch can flip too: the fix is
strongly associated with the high state, not proven to cause it directly. Off for N&B either way.

### SOLVED (06:45): the "draw level" is Showdown Town's day/night cycle

Screenshots of two runs of lean2 back to back: night (c31, 4,854 draws a frame, 24.22 fps) and DAY with "Saving
content" (c32, 5,107). The town's time of day runs on across runs through the autosave on arriving, so consecutive
runs alternate day and night; daytime draws 5,100-5,630 a frame (more replayed and immediate draws: likely the sun's
shadow passes), night ~4,850. Every "high" run was a daytime run: the alias fix, the counters switch and sampling are
cleared (the earlier sections on them are wrong; sasrt_resolve_alias_previous stays off for N&B only because N&B
does not need SASRT's menu fix and 1.0.2 did not have it). Regrouped, per draw:
night: 1.0.2 7.55; 5d1c60e9 8.48 / 8.54 / 8.53; lean 8.57, lean A/B 8.45, lean2 8.50 / 8.48 -> lean = 5d1c60e9.
day: b44 9.17 / 9.40 / 9.10, round 2 9.67 / 9.42 (alias off); lean 8.24, lean2 8.26 / 8.58 -> the full sasrt set
cost ~1 us a draw by day; the lean batch was the right call. No daytime run of 5d1c60e9 or 1.0.2 exists to compare.
Method from now: note day/night (a screenshot at ~300 s) and compare runs of the same time of day.
Daytime 5d1c60e9 (c33, screenshot: late-afternoon light): 22.87 fps, 5,242 draws a frame, 8.30 us a draw. Daytime
lean / lean2: 8.24, 8.26, 8.58 us; full batch b44: 9.17, 9.10. So the lean batch equals 5d1c60e9 by day and by night,
and sasrt-nx's full rounds 2-4 set cost ~10 % a draw in N&B. Against 1.0.2 (night 7.55) the lean build is +12.6 %.

## The render thread to ~33 ms (the user's 24 hours, from 2026-11-21 08:00)

Target: Showdown Town's render thread ~33 ms a frame (night ~4,850 draws: ~6.8 us a draw; lean2 is 8.50, 1.0.2 7.55).

What the c27 profile (lean, 1 ms sampled, by source line, scratchpad fn_lines.py "*") puts where, % of the render
thread: memcpy + memcmp 6.2 (memcpy's long loop, copies over 128 bytes, at ~0.9 GB/s; callers: deko3d's
dkCmdBufPushConstants and dkCmdBufBindTextures into command memory, the fetch copies, plume's handle copies; the
renderer's hot memcmp calls are the 80-byte spec-word compares, which GCC did not inline); the geometry proofs
(GeometryObjectsHit + ProveCopyRange) 5.0; Replay's prefetch helpers ~7-9 together (ApplyResources' copy prefetch
4.0, PrefetchResourceObjects 2.9, PrefetchShaderBehind 1.9, the next op's state lines 1.0); the spec words 2.35.
The 44 batch's hot code is 45 KB of 64-byte lines against the lean build's 43 KB (300-600 s): its loss was work, not
the instruction cache. Command buffers are recorded anew every frame (1.14M ops a 10 s, one an op a draw): there is no
recording to keep across frames.

Cycle 1 (cc0cc81a, 8d9cb5c5, 3d554cc0): the spec answers kept while their inputs hold (A/B 172; the line marks, then
the words they read, masked to the bits read); sasrt_gpu_vs_input_mask off for N&B; the renderer and plume with
-fomit-frame-pointer (BANJO_SWITCH_RENUT_OMIT_FRAME_POINTER, out/switch_pgo_use_fp.bat). Ryujinx verify run (11 min,
banjo_renut_fast_draw_verify): 0 kept answers differ; kept 40 % of the fast draws (a fifth of those by the word
check). The rest change shaders (the pipeline changes at ~60 % of the draws), which a last-draw keep cannot answer.

Console c35 (3d554cc0, day, 5,250 draws a frame, A/B 172/56/55): 8.33 us a draw against 5d1c60e9's 8.30 by day. A/B 172
(spec answers kept) -0.2 % a draw, 56 (next op's resource objects prefetched) -0.3 %, 55 within 0.3 %. The cycle's
items together (172, vs input mask off, no frame pointer) made no visible change. On the A57 a sample on a removed
line moves to the next touch of the same cache line: the render thread waits on memory, and a profile share is not
what the item saves. Cycle 2 (849e7a3f): cached command memory (banjo_deko3d_cached_commands, A/B 173: the command
buffers in CPU-cached memory, cleaned at end(); memcpy into the uncached blocks ran at ~0.9 GB/s), the spec-word
compares inline, the shader-behind prefetch (A/B 174) and ApplyResources' copy prefetch (A/B 175) as A/Bs.

Console c36 (849e7a3f, day, in-run A/B pairs: the render thread a draw with the item on against off, mean of the
pairs, halves above 20 us a draw left out): A/B 174 (PrefetchShaderBehind) +1.44 % (se 0.21, on slower in 19 of 21);
A/B 175 (ApplyResources' copy and card prefetch) +0.47 % (se 0.16, 16 of 22); A/B 173 (cached command memory) +0.21 %
(noise) but GPU -1.03 % a draw (19 of 21) and all threads' CPU -0.9 %. Both prefetches read a line to find the next
(the record's entry and declaration, the object's answers): the render thread waited on that miss as the draw would
have. Cycle 3 (6a90503d): both off by default, cached command memory on by default, A/Bs 176-178 for the prefetches
left. (scratchpad ab_pairs.py: per-pair means; tools/ab_halves.py's totals let one slow half weigh in.)

The runs of one build differ between sessions: c35 (08:42) 8.33 us a draw by day; c37 (13:30, the same NRO and
toml) 8.70 at night, and c36 (cycle 2, 13:05) 8.70 by day. Not the CPU clock (the user). Compare builds within one
session (brackets, the fixed save below) or by in-run A/Bs. From 2026-11-21
13:45 every city run starts from one fixed save (out/card/save_fixed_night_2026-11-21, the user's OK): the same
in-game time at every start (the town's day/night state decided the draw count before).

The fixed save does not fix the town's time of day: c38 (5d1c60e9) and c39 (cycle 2) started from the same restored
save and came up night and day. In ~30 runs the state never changed within a run (decided at load; scratchpad
daynight.py). The same build by day: c36 8.71, c39 8.38 us a draw (4 % apart). Builds are compared by in-run A/Bs.
Baseline c38 (5d1c60e9, night, 14:00): render thread 41.89 ms a frame, 8.60 us a draw, 4,871 draws, 23.8 fps.
c39's A/Bs (median of the pairs, render thread a draw with the item on): 174 +1.21 % (17 of 17 pairs higher), 175
+0.12 %, 173 (cached command memory) +0.90 % with GPU -0.9 %. With c36 (174 +1.57, 175 +0.58, 173 +0.11): 174 and 175
stay off; cached command memory off by default again (8c90ee87): the render thread pays its clean and sets the town's
frame rate. Cycle 3's console run: A/Bs 176-178 and 56 with 1 ms stack sampling (renut_city_c3ab_s1.toml).

Console c40 (cycle 3, 8c90ee87, night, fixed save, 1 ms stack sampling on): render thread 40.43 ms a frame, 8.36 us a
draw, 4,834 draws, 24.6 fps, against c38 (5d1c60e9, night, no sampling) 41.89 ms, 8.60 us, 23.8 fps: -3.5 % a frame
while carrying the sampling. A/Bs (median of 15 pairs, render thread a draw with the prefetch on): 176 -0.36 %, 177
-0.60 %, 178 -0.36 % (all three pay: their addresses come without a dependent load); 56 +0.36 % (10 of 15 higher; c35
gave -0.03 %). With frame pointers omitted the stack samples no longer name the renderer's callers (93 % "_EntryWrap");
the PC samples still give lines (fn_lines.py). Top lines: memcpy+memcmp 4.5 %, GeometryObjectsHit 3.4 %,
PrefetchResourceObjects 2.9 %, ApplyState 2.2 %, ApplyResources 1.6 %, PagesClean's card loads 1.5 % (no longer
prefetched with 175 off), HostShaderOf 1.4 %, TextureBindingEpoch 1.2 %, the spec words 1.6 %; deko3d's own calls:
PushConstants 2.0 %, the kick's ioctl 0.7 %, BindShaders 0.5 %.

Console c41 (cycle 4, 39354bba, day, sampled; renut_city_c4ab_s1.toml): 8.21 us a draw, 43.17 ms, 5,255 draws, 23.1 fps
(c39, cycle 2 by day unsampled: 8.38). A/Bs (median of 18-19 pairs, render thread a draw with the item on): 179, the kick
submits (banjo_deko3d_kick_submit): +1.74 % (18 of 18 higher), GPU +1.65 % a draw, fps -2.3 %: the render thread pays
about three more submits a frame (each an ioctl and a list's end) and the GPU more, smaller pieces; what they buy is
input latency (the GPU starts the frame before the present), so it is the user's trade, not changed. 181 (the next
draw's store cards prefetched from its record) +0.31 % (14 of 18 higher): even prefetches with no dependent load cost
when they are five lines a buffer; off. 56 (the next op's resource objects) +0.24 % (third run: -0.03, +0.36, +0.24):
off. The prefetches that pay are the few-line ones from the record or a known pointer (176-178, 55).

## The deko3d command stage on core 3 (2026-11-21 evening, the user's "build core 3"; cf8b37ac, 8d74f599)

banjo_deko3d_command_stage (off by default; A/B 182, its off half records directly): package 28b's two-thread recorder
(banjo_renut_record.cpp) on deko3d. The render thread records its command list into a stream (each plume call copied,
its arrays and push data with it); a thread on the system core (RexSwitchMoveCurrentThreadToSystemCore: core 3 in 4-core
mode, priority 0x2E, below the game's audio threads there) plays it into plume and libdeko3d. The list is drained before
it is ended (every end and submit already called record::Drain). Same commands, same order.

- The renderer's plume calls that take a deko3d list (16: constant pushes, uniform / storage / fetch / export binds,
  the shared constants' copy and its line marks, buffer waits, L2 flush, sample mask, next primitive, discard, counters,
  fragment textures) go through banjo_renut_record.h's inline wrappers (recorded on the proxy, plume otherwise).
- bindTextures reads the shared constants from the renderer's CPU copy (g_bound.pushed_shared), which the next draws
  change before the stage plays a draw. The stage keeps its own: the whole copy at each recorded SetSharedWords, then
  every recorded push to the shared buffer (uniform slot 2's address) written in at its offset (every write to the
  renderer's copy follows a push of the same bytes, PushSharedConstants). Verify key banjo_deko3d_command_stage_verify:
  a hash of the renderer's copy before every recorded draw, compared by the stage. Emulator run stage_verify2 (8 min,
  title and the city): 10.6 M draws checked, 0 differ (the first run's 6 were the verify's own error: it kept hashing the
  renderer's copy at the start of a list, before the list's first pushed draw, where plume reads none: 8d74f599).
- A descriptor set's image tables (imageSizes for txq, imageTextures for markSampled) are sized once when the set is
  made, and setTexture hands their update to plume's Deko3DSetImageTrackHook: on the render thread with a stage list
  open it goes into the stream after the draws recorded so far.
- plume objects a command can name (buffers, textures, views, framebuffers, descriptor sets, query pools, pipelines) call
  g_deko3dBeforeDestroyHook first: on the render thread with unplayed commands the stage plays them before the object
  goes (SMAA's and the frame capture's targets are destroyed at once, not retired with the frame).
- Deko3DBufferWaits on the proxy: the barriers recorded through it, counted across its lists (a later barrier is later
  on the one GPU queue whatever its list); the two sasrt tests that read it keep a wait at most once more.
- Not in 4-core mode (no system core) the stage stays off: on the game's cores it would take their time.
  banjo_deko3d_command_stage_any_core runs it unpinned for the emulator, where it shares the render thread's emulated
  core (the emulator's render thread looked 2x slower with it: not a console figure).
Emulator: no crash, the town drawn right (captures), ~8.5 commands a draw through the stream, every image-table update
on the render thread. Console: pending (renut_city_stage_s1.toml: A/B 182, 1 ms sampling).

Console c42 (stage build 4326cf95, kick submits off, day, 1 ms sampling, A/B 182 alone, 68 pairs): the stage ON
against OFF: render thread -5.02 % a draw (lower in 68 of 68 pairs), fps +2.8 %, GPU +0.65 % a draw (60/68 higher),
all-threads CPU +3.7 %. Halves: on 38.88 ms a frame, 7.35 us a draw, 24.9 fps; off 40.57 ms, 7.66 us, 24.5 fps
(5,290 draws). The stage thread on core 3 at 0x2E (core 3 39 % busy averaged over both halves: the game's XAudio
supervisor 20 %, audio worker 7 %, the stage ~20 % in its halves); it plays ~11 ms of commands a frame, about twice
what those calls cost on the render thread (core 3 has none of their lines); the render thread still waits ~0.85 ms a
frame at the list's drain. Next batch (the user, 19:30): the constant byte-swap and the fetch-data copies to the
stage, the stage ending and submitting the list itself, a look-ahead helper for the copies' card proofs and the texture
epochs, the memexport readback copies to a worker, and Replay as a front stage.

### Batch 2 (the user's, 2026-11-21 evening; 28424311, 59b688c4, f39da13a, f9110ce0)

Each item behind its key and in-run A/B:
- 183 banjo_deko3d_stage_submit: the stage ends the frame's list, waits for the ring cleaner's ticket, submits and
  presents (record::StageCall: a function with its arguments, played in stream order); the render thread hands the
  frame over and goes on (no drain at the frame's end). WaitSlotLocked waits for the slot's staged submit before its
  fence; the acquire (a queue operation) waits for the last staged present (a frame of recording earlier: no wait in
  practice), so the queue is one thread's at a time; RebuildSwapChain drains the stage.
- 184 banjo_deko3d_stage_swap: float constant runs go into the stream as the guest has them; the stage swaps them in
  place (NaN flush rule kept).
- 173 cached command memory, now with its clean on the stage (the list's end is the stage's).
- 185 banjo_renut_warm_helper: a thread on the system core (priority 0x2F) reads 24 ops ahead of the replay and touches
  what their draws read (state lines, records, texture and geometry objects with their copies and cards, shader records
  with their entries and declarations): the prefetch helpers that cost the render thread (174/175/56/181) run their
  dependent loads there; the lines land in the shared L2. Replay publishes its buffer under a sequence lock and its op
  index at each op. Reads and prefetches only; geometry objects checked against their pool.
- 186 banjo_deko3d_command_stage_poll_us (200): the idle stage looks for work every 200 us; the render thread never
  wakes it (about 18 wake system calls a frame before).
- 187 banjo_deko3d_command_stage_peek (8): the stage prefetches the pipeline, framebuffer and buffer objects of the next
  commands (its setPipeline was 14.5 % of its thread in c42).
- The recorder copies push payloads inline.
Not built, measured as nothing to move: the fetch-data copies (already batched at the kick, 0.02 %), the memexport
readback completion (0.03 %). Not built: Replay as a front stage (the draw reads the state the replay writes: ~2-4 %
for a day's refactor); the look-ahead card proofs (a read-only helper keeps most of the cost).
Emulator: items 3 and 5 verified (batchA_verify, 10 min: 16.2 M draws' shared copies checked, 0 differ, frames
presented by the stage, the town drawn right).
Emulator, the whole batch (batchB_verify, 9 min, verify on): 0 differ, no crash, the town drawn right, the stage polled
(0 wakes), the warm helper ran. It showed a cost item 5 brought in: with no drain at the frame's end, the destructors
of the objects retired with a frame slot (DrainSlot, after the slot's fence) found the stage still playing the next
frame and drained it, 20-300 times in 10 s. 69a5a2f5 (banjo_deko3d_stage_retired_free, on): DrainSlot's destructors
go without a drain, since WaitSlotLocked has waited for that slot's staged submit (every command naming them is played) and
no later command names a retired object. Report line: "drains before an object went N (M retired objects went
without one)".

### Batch 2 on the console (c43, 2026-11-21 21:29-21:47, NRO 69a5a2f5, PGO retrained in the city with it)

Showdown Town by day (~5,570 draws a frame), fixed save: no crash, no stall, 0 differ; the stage and the warm helper
on core 3. Render thread 38.41 ms a frame, 6.89 us a draw over every half (c42's stage halves: 7.35 us); 25.16 fps;
GPU 34.74 ms. In-run medians, render thread a draw with the item on:

| A/B | item | render a draw | render ms | fps | pairs on higher |
|---|---|---|---|---|---|
| 186 | the stage polls, never woken | -5.29 % | -5.87 % | +3.26 % | 0/9 |
| 173 | cached command memory (with the stage) | -1.22 % | -1.06 % | +1.01 % | 0/10 (GPU -1.4 %, CPU -1.8 %) |
| 185 | warm helper | -0.87 % | -1.36 % | +1.22 % | 1/9 (CPU all -0.2 %) |
| 184 | constants swapped on the stage | -0.73 % | -0.53 % | -0.55 % | 1/9 |
| 183 | the stage submits and presents | 0.00 % | +0.57 % | +2.14 % | 4/9 (its gain is the drain wait, outside "busy") |
| 187 | the stage's peek prefetch | -0.44 % | -0.50 % | -0.20 % | 4/9 (on core 3: setPipeline 14.5 % -> 6.1 %) |

All on: ~6.83 us a draw. 186 is the lesson: about 18 wake system calls a frame cost the render thread 5 %.
Core 3: the stage ~47 % busy, the helper about a third of that. The stage's profile: one PC, the first load of a
command header written on the render thread's core, is 17.8 % of the stage (the audit's stream prefetch, A/B 188).

The stage's ARM audit (4c71dec9, 9d24fb6b): stream prefetch 512 bytes ahead (A/B 188); the NVK pipeline-bind hint
left out on deko3d (0.45 % of the stage); the warm helper's counters off the cache line of the op index the render
thread stores at every op. The recorder's methods: their hot path is 31 instructions in two lines, the slow path
after it (no change). libdeko3d (prebuilt) copies push constants with memcpy (7.6 % of the stage): left.
45da8500: the stage on by default on the Switch (4-core mode only); cached command memory follows the stage.

### Batch 3 on the console (c44, 2026-11-22 00:33-00:58, NRO 4bc78bdf, PGO retrained with batch 3)

Showdown Town by day, fixed save, the GPU profile per shader on (its timestamps cost GPU time: totals not comparable);
no crash, 22 captures with the picture right. In-run (tools/ab_pairs.py means; medians agree):

| A/B | item | render a draw | GPU | all CPU | fps | default |
|---|---|---|---|---|---|---|
| 182 | the command stage with batches 2-3, against none | **-14.9 %** (6.89 vs 8.11 us) | -1.0 % | -1.1 % | +6.4 % | on |
| 135 | sasrt_gpu_handover_with_smaa | 0 | **-1.1 %** | -0.5 % | +0.3 % | on (Switch) |
| 189 | stage + helper float on cores 1-3 | -1.5 % | 0 | **+2.5 %** | -3.3 % | off |
| 188 | the stage's stream prefetch | +0.3 % | +0.4 % | -0.3 % | -0.7 % | off |
| 102 | tiled cache | 0 | **+12.5 %** | 0 | | off |
| 104 / 132 / 134 | lazy depth / tracked drops / tracked waits | 0 | 0 | 0 | | off |
| 156 | skip unread depth resolves (not exact) | 0 | -0.3 % | -1 % | +1.6-2.2 % | off |

fad3603b sets those defaults. GPU by pass (36 ms with the profiler): shadow maps ~6 ms (768x768 ~1,780 draws 4.0 ms,
1024x1024 ~910 draws 2.1 ms), resolves 2.0 + depth 1.5 ms, three full-screen blended passes 1.5-2.2 ms. The base's
handoff: out/ultimate_handoff_2026-11-22.{md,patch} (6e974759..fad3603b).

### The final build, clean (2026-11-22 07:48-08:16, NRO a8e1aa3a, no A/B, no profiler, the user's settings)

Showdown Town, fixed save, three launches (the time of day is the game's pick): warm sky (37 windows) 24.73 fps, render
thread 38.26 ms, GPU 34.42 ms, 5,573 draws a frame, 6.86 us a draw; blue midday sky (36 windows) 25.24 fps, render
36.22 ms, GPU 33.10 ms, 5,114 draws, 7.08 us a draw; the third warm again. The ~4,850-draw state of the earlier
"night" runs did not come up. The start of the 24 hours (5d1c60e9): 23.09 fps, render 42.36 ms, GPU 35.42 ms.
A fourth launch (08:35, a night attempt after Blur's slot): a dim blue-grey sky, 26.16 fps, render 36.67 ms, GPU
33.47 ms, 5,313 draws a frame (6.90 us a draw); still not the ~4,850-draw state. Across the clean runs: 24.7-26.2 fps,
6.86-7.08 us a draw, the render thread the limit in each. The whole run read every stage from the DKSH cache: "uam 0
compiles, blocking 0, DKSH cache 13091 hits, 0 added" (RR6's blocking generic compiles behind the core-3 stage do not
happen here).

### The slow start: the DKSH cache's walk (2026-11-22, the user: "we have a slow boot")

Not pacman-nx's CLI11 registration (its fix, 72c28ced, is in: sdk/src/core/cvar.cpp registers only the cvars the
command line names). Every console start since at least c40 spent 101-105 s between the log's fourth line and
"[deko3d] DKSH cache /switch/renut-nx/deko3d_shaders.bin: 195826 variants": LoadCache sought to each record and read
its 16-byte head, and a seek drops stdio's buffer, so 195,826 small reads at new places on the SD card. The card's
cache has every variant of every build since 2026-10 (a Showdown Town session hit 13,091 of them). The base and the
other ports (sasrt-nx, blur-nx, culdcept-nx) have the same walk.

Fix (banjo_deko3d_cache_index, on): deko3d_shaders.bin.idx beside the cache lists the records (key, offset, size: 16
bytes each, ~3 MB), read in one go; it keeps the cache's length and the first and last records' heads, checked against
the cache before it is used; records added after it are walked from its end, and the index is written again
(NAME.idx.new, then renamed) whenever the walk found records. The walk itself reads 4 MB pieces (a cache without an
index: one sequential read of the file instead of a seek a record). The start's log line gives the time and where the
records came from. Not yet measured on the console.

