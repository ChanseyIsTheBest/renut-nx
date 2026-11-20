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
