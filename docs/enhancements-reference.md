# Enhancements reference

> **Title update 3 (2026-10-29).** banjo-nx is now built from the updated game (and installs the DLC):
> `docs/title-update-3.md`. Every address-keyed item on this page (natives, localized and bulk copies, hooks, the
> analysis lists) was moved to the update's code or derived again on it; addresses quoted below that predate it are the
> base game's. `banjo_pool_lock` is off by default there (the update has its own pool lock).

Performance changes that keep the picture and the game's behaviour the same. Each has its own key and belongs to a group
whose master key must also be on:

| Master key | Shipped | Covers |
|---|---|---|
| `banjo_cpu_enhancements` | `true` | host CPU work: the game code's native functions, the render thread, the audio, the runtime |
| `banjo_noncpu_enhancements` | `true` | GPU and storage: shader variants, resolves and copies, the renderer's GPU items |

A key can be set in `switch/renut-nx/renut.toml` whether or not the shipped file lists it (only 24 are listed; the rest
have their Switch default in the code). The framework is `app/src/cpu/banjo_enhancements.{h,cpp}`. This page lists the keys
that act on reNut's renderer on deko3d and on the CPU: what each changes, why the result is the same, how it checks itself,
and what was measured.

The earlier PM4 renderer's keys are not listed: its code is still in the NRO, idle (`docs/deko3d-renderer.md`, section
23), so they do nothing on the Switch. Neither are keys that only act on plume's Vulkan back end, other than as noted.

The history behind the keys is in `docs/enhancements.md`. The changes made to the generated game code at build time have
no run-time key and are on their own page, `docs/codegen-enhancements.md`.

## Measured SLOWER ON THE CONSOLE (kept off)

These are exact, but each made the frame slower on the console, so its key is off by default. Each keeps its A/B target
(set the key on to measure it again). In the code each is marked `SLOWER ON THE CONSOLE` at its key and in its A/B name.
Figures are on minus off in the city, render thread a frame unless said.

| Key | A/B | Measured | Entry |
|---|---|---|---|
| `banjo_renut_replay_lookahead` | 29 | +2.15 ms (2026-10-27); confirmed: +2.79 ms (+3.1 / +2.2 / +3.1, night_gpu5 check) | "Render thread night and the shadow sort" |
| `banjo_renut_shadow_sort` | 40 | +1.13 ms, GPU +0.65 ms (2026-10-27) | same section |
| `banjo_renut_texture_slot_skip` | 53 | +0.64 ms (night_final); +0.69 ms and -0.53 fps (night_gpu5 check) | "Total CPU" |
| `banjo_renut_inline_line_compare` | - | PushSharedConstants 2.7 -> 4.9 % of the render thread (36q); no effect since 2026-10-23 | - |

Mixed between night_gpu4's two runs, to be measured again (not marked): 59 `banjo_renut_point_fast_state`, 46
`banjo_renut_replay_stream`, 27 setters' tags apart.

How sure these are (night_gpu5, 2026-10-28): A/B target 1 (`banjo_renut_precise_barriers`, Vulkan only, so nothing on
the Switch) measured +1.8 and +1.3 ms of render thread in two 15 s pairs: that is the noise of one short pair. The
check run (four-core, 30 s halves, three or four rounds each, target 1 as the control at +0.8 / +0.1 / -0.1 ms) gave:

| A/B | Render thread (on minus off) | Verdict |
|---|---|---|
| 29 replay look-ahead | +2.79 ms (+3.1, +2.2, +3.1) | slower: stays off and marked |
| 53 texture-slot skip | +0.69 ms (+1.4, +0.7, 0.0), -0.53 fps | slightly slower: stays off and marked |
| 34 resident literals | -0.34 ms (-1.0, -0.2, +0.1) | NOT slower: mark removed, on again (`banjo_renut_literals_resident`) |
| 57 record prefetch | -0.21 ms (-0.6, 0.0, 0.0) | no measurable effect: mark removed, stays off |
| 65 host packets | +0.45 ms (+0.7, 0.0, +0.4, +0.7) | neutral (the 3-core run before had -0.54 in each round): OFF by default |

Method for other ports: put a target that does nothing on your platform in every A/B list as the control, and use 30 s
halves or more. 15 s pairs mark items slower (or faster) by chance.

## How a key takes effect

- An enhancement is active when its own key **and** its group's master key are on (`EnhancementActive`).
- Almost every key is read once at start (`kInitOnly`). Change the toml and restart.
- With `banjo_cpu_enhancements = false` the code also resets the two SDK keys (`kernel_critsec_pcr_owner`,
  `thread_signal_skip_idle`), whose code reads the key directly.
- `log_file_writer` is read when logging starts, before the master switches: the off configs set it to `false`
  themselves.
- 2026-11-02 (renut-nx 1.0.1): the 24th key, `language` (docs/title-update-3.md, "The language key").
- 2026-11-01 (the user's lean `renut.toml`): the package's `renut.toml` lists only the 23 keys the user wants to
  change, by category: MSAA level and SMAA, resolution, fur detail, 1.0.0 look, frame cap, four-core mode, controls.
  Every other key's Switch default is the value the long file had (`BANJO_SWITCH_DEFAULT(on_switch, elsewhere)` in
  `sdk/include/rex/banjo_switch_default.h`; 51 keys, among them `target_refreshRate` 0, `banjo_renderer` "renut",
  the diagnostics, and the old renderer's and the SDK emulated GPU's keys, which nothing reads with Renut's renderer
  but which keep the long file's values so the `[enhancements]` line stays the same). The PC keeps its old defaults.
  A key from an older toml still works when a toml sets it.
  - `disable_msaa` became `msaa_level`: 0 = off (the Switch default), 2 = the game's 2x (the PC default). Higher
    values count as 2, because the game's tiling is laid out for 2x. `disable_msaa = true` still turns it off.
  - `tools/toml_lean_check.py <long toml> <lean toml>` (run from banjo-nx) compares every key of the long file with
    the value it gets now. On 2026-11-01 it listed only `disable_msaa` (now `msaa_level` 0, the same result).

## What the log shows

- `[enhancements] optimised codegen (...); cpu master on, non-cpu master on | N of M active, K switched on because
  the toml does not set them | <key> ON, <key> off, <key> off (master), ...`: the first line. `off (master)` means the
  key is on but its group is off. It also names the prewarm thread count and whether the two profilers are on.
- `[enhancements] instruction forms self-check: N of N results as expected`: every helper that has a form for one
  processor only, run against a plain C++ reference at every start (248 results on the console).
- The renderer's report every 10 s: `[renut]` lines (work per frame and one count per enhancement that counts its uses)
  and, with `renut_gpu_profile`, the `GPU profile` lines. These need logging on (`log_level = "info"`): the release logs
  only crashes.
- `[hot] <function> ON: about N calls so far, N compared with the original, N not comparable, N differences`: the
  native copies of game functions.
- `[verify] checks done: ... | differences: D`: D must be 0.
- `DIFFERENCE` or `MISMATCH`: a self-check found a result that differs from the original path. The enhancement
  switches itself off for the rest of the session, and the line counts into the verify line's D. None appeared in
  the console runs of 2026-10-13 and 2026-10-14.
- In an A/B run: the `[renut A/B]` line every 10 s (both halves of the render thread and the GPU).

## Premade configs

`python tools/make_enhancement_configs.py dist/switch/banjo-nx/renut.toml dist/switch/configs` deletes the old ones and
writes these from the shipped `renut.toml`. Each differs from it only in the keys its first line lists, so two logs
compare directly. Copy one over `switch/renut-nx/renut.toml`.

| File | Change from `renut.toml` |
|---|---|
| `banjo_cpu_on.toml` | none (as shipped, profilers on) |
| `banjo_cpu_off.toml` | the CPU group off (`log_file_writer` too) |
| `banjo_noncpu_off.toml` | the non-CPU group off |
| `banjo_all_off.toml` | both groups off: the original paths everywhere |
| `banjo_cpu_verify.toml` | `banjo_cpu_verify = true`: every self-check on every use; profilers off; slow |
| `banjo_profile.toml` | also the app's own sampler (`banjo_cpu_profiler`, `logs/rex/banjo_profiler.log`) |
| `banjo_renut_tuning_off.toml` | Renut's renderer without banjo-nx's tuning of it |
| `banjo_renut_upload_uncached.toml` | Renut's renderer with uncached upload memory (`banjo_renut_upload_cached = false`) |
| `banjo_renut_xxh3.toml` | Renut's renderer hashing with XXH3 (`banjo_renut_pmull_hash = false`) |
| `banjo_renut_unpinned.toml` | Renut's renderer with its threads not pinned (`banjo_ring_core` and `banjo_background_compile_core` -1) |
| `banjo_renut_post_high.toml` | Renut's renderer with its own default 16-bit post-processing buffers (the others use the console's 8-bit) |
| `banjo_renut_dedupe_ab.toml` | Renut's renderer, in-run A/B target 3 (`banjo_renut_shared_dedupe`) |
| `banjo_renut_push_ab.toml` | Renut's renderer, in-run A/B target 4 (`banjo_renut_push_point_only`) |
| `banjo_renut_pool_ab.toml` | Renut's renderer, in-run A/B target 5 (`banjo_renut_capture_pool`) |
| `banjo_renut_pin_ab.toml` | Renut's renderer, in-run A/B of the render thread's pin (`banjo_renut_ab_target = 2`, the Switch) |
| `banjo_renut_cpu_off.toml` | Renut's renderer with the CPU master switch off |
| `banjo_renut_exact_ab.toml` | package 27: Renut's renderer, in-run A/B of `banjo_renut_exact_streams` (`banjo_renut_ab_target = 6`: what the exact rule costs) |
| `banjo_renut_exact_off.toml` | package 27: Renut's renderer with Renut's own stream check only (the `[renut]` line counts the copies it finds late) |
| `banjo_renut_memory_policy_off.toml` | package 27: Renut's renderer with Renut's PC-sized memory limits |
| `banjo_renut_eviction_test.toml` | package 27, a test: texture eviction forced every 32 frames and released slots poisoned magenta (any magenta on screen is a bug) |
| `banjo_renut_boolspec_ab.toml` | package 27: Renut's renderer, in-run A/B target 7 (`banjo_gpu_bool_spec`) |
| `banjo_renut_blendskip_ab.toml` | package 27: Renut's renderer, in-run A/B target 8 (`banjo_gpu_blend_skip`) |
| `banjo_renut_tessgrid_ab.toml` | package 27: Renut's renderer, in-run A/B target 9 (`banjo_gpu_tess_grid`) |
| `banjo_renut_depthonly_ab.toml` | package 28: Renut's renderer, in-run A/B target 11 (`banjo_gpu_depth_only`) |
| `banjo_renut_trim_ab.toml` | package 28: Renut's renderer, in-run A/B target 12 (`banjo_gpu_output_trim`; its off half compiles the untrimmed pipelines on first use: stalls in the first minutes) |
| `banjo_renut_words_ab.toml` | package 28: Renut's renderer, in-run A/B target 13 (`banjo_gpu_depth_words_view`) |
| `banjo_renut_trim_poison_test.toml` | package 28, a test: the channels `banjo_gpu_output_trim` leaves out are 60000 instead of 0; the picture must be the same as with `renut.toml` |
| `banjo_pacing_game.toml` | the game's own frame pacing instead of `renut.toml`'s `target_refreshRate = 0` + `frame_cap = "30"` (package 26) |
| `banjo_hot_functions_off.toml` | the 24 `banjo_cpu_native_*` keys off |
| `banjo_hand_natives_off.toml` | the four `banjo_hand_*` keys off |
| `banjo_cpu_rounds_off.toml` | `banjo_cpu_guest_cas_no_barrier`, `kernel_critsec_pcr_owner` and `thread_signal_skip_idle` off |
| `banjo_logwriter_off.toml` | `log_file_writer = false` |

These configs are for test runs with logging on; the shipped `renut.toml` is not one of them.

## How to measure

Compare the same stretch of the game with one thing changed. Better, use an in-run A/B: the key `banjo_renut_ab_target`
names turns off and on every `banjo_renut_ab_s` seconds, and the `[renut A/B]` line gives both halves from one run, under
the same load and scenes. The PC is a noisy proxy (other projects share it, and its CPU and GPU are nothing like the
console's); every enhancement is judged by what it does on the console's A57 at 1020 MHz. On the PC the picture check is
a recording of an in-run A/B: `tools/ab_flicker.py --log <run log>` finds the flips and the frame differences around them,
which must look like those anywhere else.

The first targets: 1 `banjo_renut_precise_barriers` (Vulkan only, so on the Switch it is the control: a target that does
nothing), 2 the render thread's pin to `banjo_ring_core`, 3 `banjo_renut_shared_dedupe`, 4 `banjo_renut_push_point_only`,
5 `banjo_renut_capture_pool`, 6 `banjo_renut_exact_streams`, 7 `banjo_gpu_bool_spec`, 8 `banjo_gpu_blend_skip`, 9
`banjo_gpu_tess_grid`, 11 `banjo_gpu_depth_only`, 12 `banjo_gpu_output_trim`, 13 `banjo_gpu_depth_words_view`, 69
`banjo_gpu_fetch_spec` (tessellated draws' fetch types and patch kind as vertex shader constants; `docs/shader.md`, the
water's vertex fetch). The others are named in their entries below and in the key's description in
`app/src/renut_engine/gpu/banjo_renut_tuning.cpp`.

## Every key

"Shipped" is the value in the shipped `renut.toml`, or the code's default where the toml does not set the key.
"Kind": **speed** (same output, less work), **fix** (makes the output match the console), **trial** (off until a
console run decides), **not exact** (off, and stays off), **measurement off** (the gain is turning a measurement off).

| Key | Group | Shipped | Kind | Section |
|---|---|---|---|---|
| `banjo_native_reverb` | CPU | `true` | speed | Game code as native code |
| `banjo_hand_memcpy` | CPU | `true` | speed | Game code as native code |
| `banjo_hand_memset` | CPU | `true` | speed | Game code as native code |
| `banjo_hand_constants` | CPU | `true` | speed | Game code as native code |
| `banjo_hand_state_flush` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_823A9E80` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_823A9FF0` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_82B0DCB8` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_823A74E8` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_8222AB88` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_8230AE18` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_823AB4A8` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_823B5FD0` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_823A9A58` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_8225B750` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_823AC218` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_8225A680` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_823B5EA0` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_xaudio_src_m03` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_82B0D0C0` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_829A5DC0` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_82252C10` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_823A9D70` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_sub_8225B210` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_memcpy` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_memset` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_constants_vs` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_constants_ps` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_flush_constants` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_native_flush_registers` | CPU | `true` | speed | Game code as native code |
| `banjo_cpu_guest_cas_no_barrier` | CPU | `true` | speed | Runtime |
| `kernel_critsec_pcr_owner` | CPU | `true` | speed | Runtime |
| `thread_signal_skip_idle` | CPU | `true` | speed | Runtime |
| `log_file_writer` | CPU | `true` | speed | Runtime |
| `banjo_gpu_boost` | non-CPU | `true` | speed | Clocks |
| `banjo_gpu_blend_skip` | non-CPU | `true` | speed | Shaders |
| `banjo_renut_upload_cached` | CPU | `true` (Switch) | speed | Renut's renderer |
| `banjo_renut_pmull_hash` | CPU | `true` (ARM) | speed | Renut's renderer |
| `banjo_renut_push_point_only` | CPU | `true` | speed | Renut's renderer |
| `banjo_renut_capture_pool` | CPU | `true` | speed | Renut's renderer |
| `banjo_renut_neon_swap` | CPU | `true` (ARM) | speed | Renut's renderer |
| `banjo_renut_shared_dedupe` | CPU | `true` | speed; **beneficial** (console A/B target 3, 2026-10-30: render thread -0.30 ms in all 4 pairs, +0.12 fps) | Renut's renderer |
| `banjo_renut_queue_notify_idle` | CPU | `true` | speed | Renut's renderer |
| `banjo_gpu_depth_only` | non-CPU | `true` | speed | Renut's renderer |
| `banjo_gpu_output_trim` | non-CPU | `true` | speed | Renut's renderer |
| `banjo_gpu_depth_words_view` | non-CPU | `true` | speed | Renut's renderer |
| `banjo_gpu_tess_grid` | non-CPU | `true` | speed | Shaders |
| `banjo_gpu_fetch_spec` | non-CPU | `true` | speed (2026-10-31: the water's vertex shader 2,494 -> 411 instructions; A/B 69) | Renut's renderer, deko3d |
| `banjo_gpu_bool_spec` | non-CPU | `true` | speed | Shaders |

## CPU group

### Game code as native code

#### `banjo_native_reverb`

XAudio's reverb (`princeton_digital::stereo_room_t<float>::process`, `0x826683D0`) as native code instead of the
recompiled guest code.

- **Why exact:** bit-identical, including the host's flush mode on return (the generated reverb's, with
  `fp_moves_modeless`).
- **Checked:** `banjo_native_reverb_verify = true` (set by `banjo_cpu_verify`) runs both on every call and compares:
  63,000 calls bit-exact in a verify session.
- **Measured:** the recompiled reverb cost about a quarter of a console core.

#### The localized copies: `banjo_cpu_native_*` (24 keys)

For each function of `app/src/cpu/hot/localized.txt`, `tools/localize_native.py` writes a copy of the recompiled
function's own statements (`app/generated/hot`) with three changes: the volatile guest registers it keeps in the
context are C++ locals; the functions it calls come along as inlined copies when their whole call tree can be
repeated by the guard; a call that stays a call stores and loads only the registers the callee can reach and that can
have changed. `app/src/cpu/banjo_hot_guest.cpp` puts a hook with its own key in front of each. From sa2-nx's
mechanism; Banjo's guard is new (below). Ranking and the 77 candidates: `docs/enhancements.md`, "Hot guest
functions as localized copies".

- **Why exact:** the same statements; only where the guest registers live changes.
- **Checked:** the guard runs the copy and the recompiled original on the same state: both recorded first (every
  guest store logged), memory rolled back, then both for real over a snapshot of exactly the bytes either wrote; every
  register of the context, the written bytes and the flush mode are compared. A difference must repeat three times
  (audio buffers change under a call). The first 64 calls of each thread, then 1 in 4096
  (`banjo_hot_guard_calls`, `banjo_hot_guard_period`), every call with `banjo_cpu_verify`. Verify run (1,560 s):
  13,587,542 calls compared, 0 differences. The hook costs 14 A57 instructions.
- **Measured:** an estimate of A57 instructions per call (`tools/hot_arm_compare.py`: real block counts from a PC run,
  the Switch compiler's code), not a console measurement. Together the 18 copies are worth about 1 % of the game
  threads' guest time, and the game threads are not the limit: no frame-rate change is expected.
- **Off:** `banjo_hot_functions_off.toml`.

| Key | Function | What it is | A57 instructions per call: original -> copy + hook |
|---|---|---|---|
| `banjo_cpu_native_sub_823A9E80` | `sub_823A9E80` | draw threads: binds a draw's textures | 575 -> 539 + 14 (-3.8 %) |
| `banjo_cpu_native_sub_823A9FF0` | `sub_823A9FF0` | draw threads: uploads a draw's shader constants | 338 -> 308 + 14 (-4.7 %) |
| `banjo_cpu_native_sub_82B0DCB8` | `sub_82B0DCB8` | audio thread: XAudio sample loop | 21591 -> 20548 + 14 (-4.8 %) |
| `banjo_cpu_native_sub_823A74E8` | `sub_823A74E8` | draw threads: one draw's state set-up | 1075 -> 780 + 14 (-26.2 %) |
| `banjo_cpu_native_sub_8222AB88` | `sub_8222AB88` | draw threads: Direct3D state setter | 280 -> 238 + 14 (-9.9 %) |
| `banjo_cpu_native_sub_8230AE18` | `sub_8230AE18` | main thread: float and vector routine | 702 -> 643 + 14 (-6.4 %) |
| `banjo_cpu_native_sub_823AB4A8` | `sub_823AB4A8` | draw threads: list walk | 7047 -> 6190 + 14 (-12.0 %) |
| `banjo_cpu_native_sub_823B5FD0` | `sub_823B5FD0` | draw threads: float and vector routine | 321 -> 289 + 14 (-5.6 %) |
| `banjo_cpu_native_sub_823A9A58` | `sub_823A9A58` | draw threads: loops over a draw's bindings | 1502 -> 1260 + 14 (-15.2 %) |
| `banjo_cpu_native_sub_8225B750` | `sub_8225B750` | main thread (menus): list walk | 208 -> 176 + 14 (-8.7 %) |
| `banjo_cpu_native_sub_823AC218` | `sub_823AC218` | draw threads: float and vector routine | 1989 -> 1862 + 14 (-5.7 %) |
| `banjo_cpu_native_sub_8225A680` | `sub_8225A680` | main thread (menus): small routine | 300 -> 263 + 14 (-7.9 %) |
| `banjo_cpu_native_sub_823B5EA0` | `sub_823B5EA0` | draw threads: float routine | 880 -> 794 + 14 (-8.2 %) |
| `banjo_cpu_native_xaudio_src_m03` | XAudio SRC (mono) | audio thread: sample-rate converter loop | 3340 -> 3044 + 14 (-8.5 %) |
| `banjo_cpu_native_sub_82B0D0C0` | `sub_82B0D0C0` | audio thread: XAudio limiter, log10 / log / pow of every sample in its closure (2026-10-16; 7.3 % of the game threads' samples in package 20e) | 1581 -> 1250 + 14 (-20.1 %, static: every block once; 16 calls -> 0, host stack 114 -> 36) |
| `banjo_cpu_native_sub_829A5DC0` | `sub_829A5DC0` | main thread: routine with nine callees | 8998 -> 7117 + 14 (-20.7 %) |
| `banjo_cpu_native_sub_82252C10` | `sub_82252C10` | main thread: float routine | 3325 -> 2684 + 14 (-18.8 %) |
| `banjo_cpu_native_sub_823A9D70` | `sub_823A9D70` | draw threads: Direct3D state setter | 198 -> 172 + 14 (-6.0 %) |
| `banjo_cpu_native_sub_8225B210` | `sub_8225B210` | main thread (menus): small routine | 300 -> 263 + 14 (-7.8 %) |
| `banjo_cpu_native_memcpy` | `sub_82BB4510` | every thread: the C runtime's memcpy, hand-written native in front | 239 -> about 117 over the size mix |
| `banjo_cpu_native_memset` | `sub_82BB4A40` | every thread: the C runtime's memset, hand-written native in front | 669 -> about 100 over the size mix |
| `banjo_cpu_native_constants_vs` | `sub_8222EFA8` | draw threads: vertex shader float-constant writer, hand-written native in front | 128 -> about 79 |
| `banjo_cpu_native_constants_ps` | `sub_8222F0D8` | draw threads: pixel shader float-constant writer, hand-written native in front | 166 -> about 81 |
| `banjo_cpu_native_flush_constants` | `sub_8222D0E8` | draw threads: state flush, dirty constant blocks into the command buffer, hand-written native in front | 228 -> 151 for one run of one block |
| `banjo_cpu_native_flush_registers` | `sub_8222CB90` | draw threads: state flush, dirty register words into the command buffer, hand-written native in front | 152 -> 114 for one run of one word |

#### `banjo_hand_memcpy`, `banjo_hand_memset`, `banjo_hand_constants`, `banjo_hand_state_flush`

Hand-written natives (`app/src/cpu/hot/banjo_hand_natives.h`, list `app/src/cpu/hot/hand.txt`). They replace a
function's statements where the localized copies keep them. `tools/localize_native.py` puts each in front of the
generated copy of its function wherever that copy is used: as the function's own hook (the last six rows above) and
inlined in every other hook's copy that contains the function (memcpy in 2, memset in 1, the constant writers in 3).
A native returns false to decline a call it does not cover, before it writes anything, and the generated copy runs.
Each key switches its native off everywhere at once.

- `banjo_hand_memcpy`: overlapping 16 / 8 / 4 / 2-byte moves up to 128 bytes (83 % of the calls), the host's memcpy
  above. Declines a forward copy that would smear, ranges that wrap, a destination over its own save slot. From
  sa2-nx.
- `banjo_hand_memset`: overlapping stores up to 64 bytes, the host's memset above. From sa2-nx.
- `banjo_hand_constants`: Direct3D's two shader float-constant writers as a plain copy of the vectors and the dirty
  mask update, instead of two emulated unaligned vector loads, a table lookup and a byte reversal per vector. From
  sa2-nx.
- `banjo_hand_state_flush`: the two writers of Direct3D's state flush (dirty register words and dirty constant blocks
  into PM4 packets) copy each run in guest byte order (the originals swapped every word twice). Declines a run that
  might not fit the buffer. New in Banjo.
- **Why exact:** the guard compares each native with the original's statements like any copy. The natives write no
  scratch register the originals leave values in (memcpy r0, r4-r12; the constant writers r4-r6, r8-r11, v0 and
  v10-v13): sa2-nx's `tools/call_site_liveness.py` over the 3,029 direct call sites found no caller that
  reads one after the call, so the guard leaves those out of its register compare. The natives clobber nothing (the
  game was linked with whole-program register allocation: callers keep f0, f11-f13 and r7 alive across the constant
  writers).
- **Checked (PC verify, 340 s):** 4,696,921 memcpy, 1,091,193 memset, 2,154,118 constant-writer and 3,077,315
  state-flush calls compared, 0 differences.
- **Measured:** A57 instructions along the usual path, from the Switch build's disassembly (the table above): about 7
  billion instructions less over a 340 s PC route, roughly 2 to 3 % of the game threads' time.
- **Off:** `banjo_hand_natives_off.toml`.

### Runtime

#### `banjo_cpu_guest_cas_no_barrier`

The game's compare-and-swap (`tools/guest_cas.py`, 559 sites) and the SDK's lock words (`rex/thread/atomic.h`) without
the legacy builtin's trailing `dmb ish` on the A57. `false` puts the barrier back at run time. From sa2-nx's CPU
rounds.

- **Why exact:** PowerPC's `lwarx` / `stwcx.` order nothing by themselves; the game's own barriers are translated
  separately and stay.

#### `kernel_critsec_pcr_owner` (SDK key)

`RtlEnterCriticalSection` / `RtlTryEnterCriticalSection` take the owner thread from the processor control region the
guest context points at (r13) instead of looking the host thread up (24,000 enters a second on the PC).

- **Checked:** the PC build compares both on every call: 18 million in a verify session, 0 wrong.

#### `thread_signal_skip_idle` (SDK key, Switch)

Setting an event or releasing a semaphore that no thread is blocked on does not call the condition variable's notify
(on Horizon always a system call, even with nobody waiting). From sasrt-nx through sa2-nx.

- **Why exact:** the waiters of each condition are counted under the same mutex as the state change
  (`sdk/tests/signal_skip_idle_test.cpp`).

#### `log_file_writer` (SDK key)

The file log is written by a thread of its own (`sdk/src/core/logging.cpp`, `WriterFileSink`, priority 0x3B on the
Switch): the logging thread only formats the line into memory; the writer writes the batch with one `fwrite` once a
second, sooner at 64 KB or on a warning. From futari-nx, whose console run had every frame over 100 ms while its renderer's
thread wrote its 10 s report into the SD card.

- **Why exact:** the same lines, written up to a second later. Past `log_file_writer_buffer_kb` (4 MB) lines are
  dropped and counted, never waited for; a crash or an exit still writes everything.
- **Off:** `banjo_logwriter_off.toml`.

## Non-CPU group

### Clocks

#### `banjo_gpu_boost`

In handheld mode asks for the official 0x92220008 profile (GPU 460.8 MHz, memory 1331.2 MHz, the one
UnleashedRecomp-NX, MarathonRecomp-NX and masseffect-nx use) through the SDK's `switch_apm.cpp`, which checks the real
clocks for 1.5 s and reverts if the memory clock moved. Docked, nothing is requested. From sa2-nx.

- **Why exact:** a clock, not a setting.
- **Risk:** more power draw in handheld mode.

### Shaders

These keys choose variants of a shader. On deko3d a variant's constants are `#define`s in its GLSL, so each has its own
text and its own DKSH (`docs/deko3d-renderer.md`, section 5).

#### `banjo_gpu_blend_skip` (off on this renderer)

Pixels the pipeline's blend would not change are discarded at the shader's exits (specialization constant 900; one UNORM
target, no depth or stencil write, a blend that keeps the destination for a zero source; a NaN output never). In-run A/B
target 8. **Off:** it takes `banjo_renut_blend_skip` as well, which is off because the depth of field and the river water
come out wrong with it (`docs/deko3d-renderer.md`, section 11).

#### `banjo_gpu_tess_grid`

Nuts & Bolts' water is tessellated triangle patches, and the renderer has no tessellation stages. Tessellated draws are
indexed by grid vertex (`renutVertexRegisters` kinds 3 and 4): the vertex shader runs once per grid vertex instead of once
per triangle corner. PC route: 3.0 million grid vertices for 15.2 million corners every 10 s.

- **Why exact:** the host's index list (one per kind and level, made once) names the same triangles in the same order with
  the same corners, and each grid vertex gets the same weights.
- **A/B:** target 9.

#### `banjo_gpu_bool_spec`

The material shaders test bool constants at run time, and every path is in the binary behind a uniform branch, with the
registers of the largest. Each bool a shader tests (`BOOL_BIT(N)`, `banjo_bool_spec.h`) is specialization constant 1000 + N,
default 2 = read at run time, so the generic pipeline is the old shader. The renderer's pipeline key holds the bool words.
34,660 constants in 3,627 shaders; on the PC route about 110,000 of 226,000 draws in a 10 s window draw specialised.

- **Why exact:** the specialised value is the one the shader reads at run time for that draw.
- **Compiles:** a new combination draws with the generic pipeline (the same pixels) while a prebuild worker makes it.
- **A/B:** target 7. Off at build time: `BANJO_RENUT_BOOL_SPEC=0`.

### Package 27 (2026-10-17): keys that act on Renut's renderer

`banjo_gpu_bool_spec` (bool constants as specialized variants; specialised pipelines built by the prebuild workers, the
generic one meanwhile), `banjo_gpu_blend_skip` (off through `banjo_renut_blend_skip`: `docs/deko3d-renderer.md`, section 11)
and `banjo_gpu_tess_grid` (tessellated draws by grid vertex) have their entries above. What each does here and its counts:
`docs/renut-renderer-port.md`, section 7.

Renut's translator also has literal constants from the constant bank (the renderer writes each shader's literal values into
its constant registers, so no per-draw check), the bone rows in one load, bone fetches without their branch, the moves as
their operand and early alpha (`tools/renut_early_alpha.py`). They are build-time switches of the shader cache, listed in
`docs/shader.md`.

Not enhancements (no master switch; "Other keys" below): `banjo_renut_exact_streams`, `banjo_renut_memory_policy`,
`banjo_renut_keep_gpu_written`, `banjo_renut_diag_poison_slots`.

#### `banjo_gpu_depth_words_view` (package 28; non-CPU group; in-run A/B target 13)

A texture over a depth resolve's memory (an 8_8_8_8 fetch of the guest words; Nuts & Bolts reads nearly every depth
resolve so) samples a view of the resolve's own words texture instead of a copy of it, which Renut made at the first
bind after each resolve (a 4-byte read and write a texel of the whole texture). `WordsViewSlot` in
`app/src/renut_engine/gpu/render/textures.cpp`; only a texture of the words' format and size with one level, else the
copy.

- **Why exact:** the same bytes. Every draw binds its textures again, so the copy was made before the first draw after
  each resolve, and the view shows the same texture's bytes to every draw after the resolve.
- **Measured (PC route):** about 600 binds per 10 s through the view, 0 copies; in-run A/B within noise on the PC
  (median 0.09 ms a frame over 22 windows); the console's memory is where the copy costs.

#### `banjo_gpu_output_trim` (package 28; non-CPU group; in-run A/B target 12)

UnleashedRecomp-NX / MarathonRecomp-NX's trimmed pixel outputs: the colour channels a pipeline never writes (the write
mask, the target's format, an output without a target) are 0 from the pixel shader's exits on, after the alpha test
and the blend skip, through specialization constant 901 (`OutputTrimMask` in `render/pipeline.cpp`, `banjoTrim` in
`shader_common.h`). The compiler drops what computed only them.

- **Why exact:** the GPU writes only the channels the write mask and the format have; blending reads a channel's own
  source and the source alpha, so the alpha stays wherever a written channel blends with it, and with alpha to
  coverage. The test: `banjo_renut_diag_trim_poison` (`configs/banjo_renut_trim_poison_test.toml`) writes 60000
  instead of 0; the picture must not change.
- No new pipelines: the key holds the colour mask, blend and formats already.
- **Measured (PC route):** 26,000-30,000 draws per 10 s write part of a target's channels; A/B no change on the PC
  (its driver sees the write mask when it compiles). The poison test showed no change.

#### `banjo_gpu_depth_only` (package 28; non-CPU group; in-run A/B target 11)

A draw that writes no colour, with a depth target, no alpha test, no alpha to coverage, and a pixel shader that
neither kills, writes depth nor exports (the translator's `shaderFlags`, self-checked against each shader's text) is
drawn without its pixel shader: its constants and textures are not set up, and its pipeline has no fragment stage.

- **Why exact:** depth and stencil come from the rasterizer either way (no `SV_Depth`), and nothing else of the shader
  is visible.
- **Measured (PC route):** 0 draws: every depth-only draw without an alpha test (about 42,000 per 10 s) uses one of 27
  shadow-caster shaders whose kill is the game's alpha cut-out. Kept on for draws elsewhere that qualify.

### Render thread night and the shadow sort (2026-10-27)

Measured on the console in `docs/record-time-draw-path.md` ("Console run of the shadowsort package"). All are exact;
the ones the A/B list switches have their target in brackets.

- `banjo_renut_touch_compare` [26], `banjo_renut_tags_apart` [27] (Banjo/Enhancements, on): the native D3D setters mark
  only values that change, and their tags stay out of the recording devices (fewer chunks compared a recorded op).
- `banjo_deko3d_texture_skip` [39] (on): plume's bindTextures skipped for a draw whose pipeline, descriptor epoch,
  image set and shared-constant lines are those of its last whole run; the deko3d line counts "bound textures N read,
  M skipped".
- `banjo_renut_shadow_sort` [40] (**SLOWER ON THE CONSOLE, off** since the 2026-10-27 console run: render thread +1.13 ms, GPU +0.65 ms): runs of
  depth-only draws replayed sorted by shader contents, render state and texture slots; `banjo_renut_shadow_sort_verify`
  (Banjo/Debug) compares each sorted draw's whole device with its recorded op's. Set it on for A/B 40.
- `banjo_renut_replay_lookahead` [29] (**SLOWER ON THE CONSOLE, off** since the 2026-10-27 console run: +2.15 ms): the replay prefetching the next
  op's shader lookups, guest headers and texture memo entries. Set it on for A/B 29.
- Without a key of their own (A/B only): 28 render-state numbers by content, 30 base-vertex draws, 31 no texture or pixel
  constant work for draws that sample nothing, 32 texture memo refresh, 35 texture epochs by page
  and address, 36 kept fetch data, 37 kept stream sources, 38 index copies bound by pool chunk, 41 the fast path's shader
  checks by cache entry, not by shader object.
- `banjo_renut_literals_resident` [34] (on; neutral on the console; a key of its own since h18): a shader whose literal
  registers all still hold its values (their owner) takes them as held, not pushed again. The 2026-10-27 run had
  -0.43 ms. night_gpu4's two 15 s samples said +2.4 / +1.7 ms, so h18 turned it off. night_gpu5's check (30 s halves,
  a control target) said -0.34 ms (-1.0, -0.2, +0.1): the "slower" was noise, and it is on again.

### Four-core mode and mesh objects (2026-10-27 afternoon)

- `banjo_window_blocks` and `banjo_code_blocks` (Banjo/Switch, `true`), `banjo_tlb_test_s` (Banjo/Diagnostics, `0`): game memory
  and the NRO's code on 2 MB blocks, and a test of what the TLB costs. See deko3d-renderer.md, section 43.
- `banjo_system_core` (Banjo/Threads, `1`) and `banjo_system_core_compiles` (`true`): NetherSX2_nx's 4-core mode (MIT,
  NaGaa95). With all four cores granted (a forwarder whose NPDM allows CPU 0-3) the audio output thread, the Audio
  Worker, the XMA Decoder, XAudio's mixer threads and the shader compiles run on core 3 alone; every other thread stays on
  0-2. No effect with the usual three cores. See `docs/four-core-mode.md`. Same output: thread placement only.
- `banjo_renut_mesh_objects` [42] (Banjo/Enhancements, on): a whole-buffer draw's geometry (its streams' and index
  buffer's whole copies) kept as an object under what it is resolved from, found by one probe; any copy dropped or cache
  entry erased retires every object (`g_mesh_gen`), and the copies are proven once per kick interval as before. The
  10 s line "mesh objects" counts the answers. Exact: the same copies the old path finds for the same inputs, proven by
  the same rule; `banjo_renut_whole_buffers_verify` checks the draws either way. See `docs/record-time-draw-path.md`.
- `banjo_renut_record_extents` [43] (Banjo/Enhancements, on), `banjo_renut_record_extents_verify` (Banjo/Debug): the
  recording threads work out the guest extent of each texture slot whose fetch words a recorded op changed; a replayed
  draw's memexport check tests those against the 1 KB export bitmap instead of a table of answers by fetch words (which
  the pending exports' changes, several a frame, emptied). Exact (emulator verify: about 3 million checks, 0 differ).
  See `docs/record-time-draw-path.md` ("Record-time translation, the export checks").
- `banjo_renut_shadow_cull_indices` (Banjo/Enhancements, **0 = off**; NOT pixel-identical, the user's call): shadow
  casters of at most this many indices are not drawn into the shadow maps (depth-only draws into a square depth target:
  N&B's 1024 and 768 maps). `renut_shadowcull.toml` sets 300 (about 14 % of the shadow draws on the emulator's title
  screen; 3000 would be about 65 %). The 10 s line "shadow casters skipped" counts them.
  2026-11-01, the user's console run (Nutty Acres): some vehicles lost their shadows (N&B vehicles are built of small
  parts, each a caster under 300 indices), and the level was no faster: its shadow maps cost 0.94 ms a frame (202
  depth draws into the 768 map) next to the sea's 18.4 ms. The two shadowcull tomls are no longer in the packages.
  `banjo_renut_diag_shadow_census` (Banjo/Debug): the depth-only draws by depth target and index count.
- `banjo_xma_frame_cache` (Banjo/Audio, on), `banjo_xma_frame_cache_verify` (Banjo/Debug), `banjo_xma_frame_cache_mb`
  (24): a decoded XMA frame kept under the chain of frames its codec decoded since it was opened (the format at the root,
  each packet's exact bytes); a sound played again from its start takes the samples of the first time instead of
  FFmpeg's decode. The codec catches up on the frames it skipped (decoded, output discarded) before the first frame the
  cache cannot answer, at most 256 in a row (short repeated sounds). Exact: FFmpeg's xmaframes decoder (WMA Pro) depends
  only on the frames since it was opened (overlap-add tail, previous block length, reused scale factors); the verify key
  decodes every frame and compares. `sdk/src/audio/xma_context.cpp`, XmaContext::DecodeFrame; the XMA log line every
  10 s counts the answers.

### Phase A, the replay stream, fetch cache and geometry objects (2026-10-27 evening)

- `banjo_renut_resource_objects` [45] (Banjo/Enhancements, on), `banjo_renut_resource_objects_verify` (Banjo/Debug):
  Phase A. The recording threads resolve each changed texture slot to a texture object and take each draw's index and
  vertex buffer header words; the render thread uses the objects' answers (no guest header read). Exact (emulator verify
  19.7 million answers, 0 differ, including headers the game rewrote after recording). Console A/B 45: -2.26 ms.
- `banjo_renut_replay_stream` [46] (on): ops 64-byte aligned, a draw's op two lines (`OpCall`: the call without
  pointers, the UP data by offset, the shaders' words); Replay prefetches in three stages (ops three ahead, the shader
  memo's sets two ahead, the next op's arena state, records and shader records). Exact (prefetches only).
  `banjo_renut_replay_stream_shaders` [49] (**off** since 2026-10-28): its shader stages alone (off: the memo sets one op
  ahead). The c4 console A/B measured it +0.7 ms a frame (4 of 5 periods worse), so it is off by default. The emulator's
  four-core test mode also froze with it on (see `docs/record-time-draw-path.md`).
- `banjo_renut_fetch_cache` [47] (on), `banjo_renut_fetch_cache_verify` (Banjo/Debug): vertex fetch data (shaders'
  fetches by address) kept in the stream cache's pool once unwritten for three kicks, proven once per kick (store cards
  or exact hash) as the whole-buffer copies are; a draw whose fetch buffers are all kept in one pool chunk binds it.
  The 10 s line "fetch cache". Exact.
- `banjo_renut_geometry_objects` [48] (on): each draw's stream sources and index buffer resolved to objects by the
  recording threads (one per buffer); the render thread keeps each object's whole copy and a draw takes them with a
  pointer and a few compares a buffer. Replaces `banjo_renut_mesh_objects` while on (A/B 48's off half is the mesh
  objects). The 10 s line "geometry objects". Exact (same copies, same proofs; `banjo_renut_whole_buffers_verify`).
- Not keyed (exact, small): the constants push reads the shader's used registers and literal flag from its record's draw
  summary; the deko3d pipeline object's draw-time fields first, prefetched once the pipeline is known.

### Total CPU (2026-10-28)

The c4 console run showed the three game cores (0-2) about 98 % busy: the frame is the total CPU on them divided by
three, not the render thread alone (A/B 48 cut the render thread 4.6 ms and the frame about 2 ms). Items are now judged by
the CPU of all threads a frame, which each `[renut A/B]` half reports ("CPU ... ms a frame (all threads)", Switch only).

- `banjo_renut_record_lookups` [50] (on): the recording threads' object lookups (texture, stream and index buffer objects,
  46 % of NewOp in the c4 run, about 6 ms a frame). The tables hold each object's key words beside its number, so a
  lookup reads one line, not two (always, also in A/B 50's off half). With the key on: an op's lookups are made in three
  steps whose misses overlap (the guest headers' lines, then the table lines, then the compares), and each recording
  device remembers the object it found last for each texture slot, stream and the index buffer. The 10 s "recording
  from marks" line counts memo and table lookups. The same objects (exact).
- Not keyed (exact): the fast path's pipeline cache entry has the pipeline and the words its check compares on its first
  line and the key's draw-time fields (point sprite, colour count and mask, depth control, stencil mask) on its second
  (`KeyHot`), so a pipeline change reads the two lines the entry's prefetch took, not four.
- ARM audit of the render and recording code (2026-10-28; the user's request; not keyed, all exact). The c4 profile's
  hot instructions were nearly all cache misses, so most items move data or fetch it earlier:
  - Replay's stream takes the resources a stage further: a draw's resource record two ops ahead, its objects' lines one
    op ahead, and at the op's own `ApplyResources` the lines of the copies their answers name (`ProveCopyRange`'s first
    load of the index copy was 1.45 % of the render thread on its own).
  - The shader record's two declaration pointers moved to its first two lines (from its fourth: 1 % of the render
    thread); the declaration record's `layout_hash`, stream order and count to its first line and the layout's
    `used_mask` before its 512 bytes of streams (they were on its ninth and tenth lines).
  - The recording's new lookups: fixed-size key compares as 64-bit XOR/OR (`SameWords`, no `memcmp`), the 16 stream
    source words tested at once with NEON (most draws bind one or two), and the lookup counts kept per recording device
    (a shared counter took an exclusive-access loop and moved its line between the two recording threads at every op).
  - The whole index copy's first index by shifts (the division by the index size was a `udiv` at every such draw).
  - plume's `setPipeline` asks for the lines of every state group that differs before the binds read them (vertex
    input counts, blend words: a miss per bind before).
- Cache-miss audit 2 (2026-10-28, the user's request; exact): the render thread's and recording threads' hottest loads in
  the c4 profile, each traced to the data it missed:
  - `banjo_renut_first_op_groups` [51] (on): a recorded buffer's first op keeps the float constant groups set so far,
    not all 512 registers. That was 8 KB per buffer, copied on the recording thread and again into the replay's device
    image. A replayed draw reads a group from the image only when its op's set mask holds it, and every such group was
    copied at or before its op.
  - The store cards and page serials a copy's proof reads are fetched with the copy, an op ahead (`PagesClean`'s loads
    were about 2 % of the render thread); the fetch cache's entries and their cards while a draw's fetch copies are
    gathered.
  - The memexport granule bitmap sits behind a 64-bit summary (a bit per 8 MB): most ranges are answered without the
    1 KB bitmap (its loads were about 0.8 %).
  - At a shader change: the device image's bool and alpha-test lines, and the bound declaration's first lines, fetched
    while the shaders and the declaration are found. The targets' write counters are fetched once the targets are known.
  - The constant push's model (`ConstantUpload`) has its small fields ahead of its 4 KB copy; the shader record's
    "fetch declaration built" flag sits beside its pointer.
  - The recording threads' extents: each slot's last words and extent are checked before the 12 KB memo (a chunk change
    marks two or three slots whose words did not change).
  - The texture export answer's compare without a `memcmp` call.
- Per-draw night (2026-10-28, not keyed, exact):
  - d5: the d4 crash fixed (an arena's doubling with the heap full): arenas grow by a quarter past 8 MB, a reused
    buffer more than twice its last recording's size gives its memory back, spares capped at 32 MB, a retry after
    freeing the spares; the two new prefetch helpers trimmed (objects already held, index objects two lines, cards only
    for a copy not proven this interval).
  - e1: the replay fetches a draw's shader records from its recorded shader words an op ahead (records sit at their
    guest address), then their cache entries and bound declarations at the op; the stream cache's 60-frame sweep clears
    the memos and geometry answers only when it erased entries.
  - e2: the device declaration's record fetched an op ahead from a recorded word; the shared constants' line masks
    without a loop and their accessor inline (the fast path's marks fold to constants); the per-shader GPU profile
    times one frame in 8 (was 4).
  - e5: a shader or declaration record destroyed (N&B makes and drops shader objects with its meshes every frame)
    invalidates the fast path's state only when it holds that record (`InvalidateFastDrawForRecordLocked`; it cleared
    it at every destruction, so the next draw took the full path), and the stream-source memos that name records.
  - e6: the recording threads' resource lookups in two calls around the op's state copy (`PlanResources`: headers,
    keys, memos, table-line prefetches before `CopyState` / `RecordExtents`; `FinishResources`: probes and the record
    after), so the table misses overlap the copy; the marked chunks' shadow lines prefetched together before the
    chunk compare (its shadow loads were 1.6 % of the recording threads in d4).
  - e7 [54]: `DropReplacedRanges` (whole copies' replaced range copies) walks both geometry caches at most every 16
    frames (`kWholeReplacePeriod`), not in every frame with a whole copy 4 frames old (0.8 ms a frame on the d4 render
    thread). Memory only: the ranges it drops stay up to 16 frames longer. A/B 54 runs it every frame.
  - e8: a constant group's two copies at a push or upload (the held model's raw bytes, the swapped lanes) from one
    NEON load of its 64 bytes (`CopyAndSwapGroup`; the loops compiled to sixteen scalar load / REV / store triples and
    a separate copy). A pipeline bind rebinds only the shader stages that changed (plume `setPipeline`; deko3d binds
    each stage on its own, and a stage in the mask without a shader is disabled), the command list's first bind still
    every stage with the whole graphics mask: fewer command words for the GPU's front end and the render thread when a
    draw keeps its vertex shader (or, in the shadow passes, has no pixel shader).
  - e9: a stage's texture handles bound from the first to the last that changed (`dkCmdBufBindTextures`' first id),
    not all of the stage's handles, when its binding count is the one bound: the driver constant buffer ends with the
    same words (each bind is a selector, a no-op and a header, then the handles).
  - e10 [55-57]: (57's stage OFF since the night_final console A/B, +0.99 ms then; night_gpu5's check: -0.21 ms, no measurable effect; key
    `banjo_renut_replay_record_prefetch`, false) in-run A/B targets that each drop one of the replay stream's
    look-ahead prefetch stages (55 the next
    op's state lines, 56 its resource objects, 57 its shader and declaration records), for the console to say which
    pay on the A57 (the d4 profile's hottest Replay line was the state prefetch). Prefetches only.
  - f1: the 16-bit integer vertex formats (16_16, 16_16_16_16, normalized or not, signed or not) expanded with NEON,
    four components at a time (`Decode16x4`: the same conversions and IEEE divisions as `DecodeComponent`, the mode
    chosen once a loop), checked once against `DecodeComponent` for every 16-bit value in every mode (`Neon16Exact`;
    a difference keeps the scalar path). Two- and three-word elements swapped with a 64-bit NEON reverse.
  - f2: the texture size entries a bind changes pushed as runs of consecutive entries (one push of 16 bytes an entry
    before, 7 words of overhead each).
  - f3 (`banjo_renut_point_fast_state` [59], on): a point-sprite draw (its constants in ring uploads) leaves the fast
    path a state to start from; the draw after it was sent the full way ("no state": 7,900 per 10 s in the d4 city,
    8,700 on the title screen). The fast draw already puts back what the ring draw changes (the pushed buffers'
    bindings and shared words, its constant models compared whole, its texture slots and fetch data refilled).
  - f4: the fast path's pipeline cache 4,096 entries (1,024 kept evicting a city frame's ~3,200 pipelines: 7.5 % of
    the fast draws made their key again and looked it up in the main cache).
  - f5 [60]: entries erased from the geometry caches void every geometry and mesh object's answer (`ClearWholeLast`)
    only when a whole-buffer entry went (`CachedStream::whole`; whole index entries by their key): answers, mesh
    objects and the last-copy pointers hold whole entries alone, a range entry is found by its key through the memos
    (still cleared). In the d4 city every drop of replaced range copies voided every answer: 30-40 % of the draws while
    driving missed them (192,000 per 10 s against 410,000 answered).
  - f8 [61]: a draw whose recording changed the predication word stays on the fast path, the predication taken as one
    of the viewport's inputs (the viewport is all that reads it). 4,092 draws per 10 s of the d4 city went the full way.
  - f18 [63]: the fast path's pipeline cache keeps an answer across prebuild epochs unless it was provisional (a
    specialised key's generic pipeline while its own is built). Every entry was voided whenever any prebuild job
    finished: 20-30 % of the title screen's fast draws (scene changes queue jobs all the time) and 7.5 % of the d4
    city's made their key and looked it up again. A built pipeline never leaves the main cache, and a failed
    specialised key keeps its generic one: those answers hold.
  - f15 [62]: a queued draw taken as a whole snapshot gets a chain number with every chunk and field marked changed (as
    a replay's first draw), not none: the fast path takes it after the draw before it, working everything out again
    from the snapshot (no replay constants: every constant group compared). "Chain break": 4,100-4,700 per 10 s on
    the title screen (run f2), 3,300 in the d4 city.
- The heap (the user's request, 2026-10-28; the d4 city: 2.7-2.9 GB in use of 3.08 GB, 260,000 free pieces):
  - f6: the stream cache pool's chunks 32 MB (64): a chunk goes back only when every copy in it has gone (112 MB of
    copies held 192 MB of chunks), and a smaller block finds room in a fragmented heap. Not smaller: a draw's fetch
    buffers are kept together only in one chunk.
  - f7 (`banjo_renut_prebuild_release_s`, 180): when the heap is short (its room, what sbrk has not handed out, below
    256 MB; the d4 city ran at about 160 MB), pipelines the prebuild made from the list that no draw has taken for
    that long go back to it (12,953 of 16,358 in the d4 city); their jobs stay, and a draw that asks later has it built
    again (a specialised key queued for the workers again, the generic pipeline drawing meanwhile). The memory line
    counts them ("given back unused"). Given back regardless of the room (the first form, emulator run f2), 6,588 went
    after three minutes and the attract loop's next visit to a scene built 408 again at once (a hitch); the fast-draw
    verify then saw generic and specialised variants swap within a draw (a race of the prebuild epoch: the same pixels).
  - f9: the upload rings' chunks 16 MB (32): each ring's first chunk stays all session, and a city frame writes a few
    MB with the constants pushed; a bigger frame adds a chunk, which the ring's trim gives back.
  - f11: texture objects 32,768 (65,536; the d4 run made 1,648) and geometry objects 32,768 (16,384; the d4 city made
    15,456 in twelve minutes, and a buffer past the last has no object: the slower lookups). Static tables come out of
    the heap a loader gives the NRO; net about 4 MB less. Objects are still never given back: a limit for very long
    sessions.
  - f13: the mesh objects' 2 MB table made at its first use (the geometry objects, on by default, never use it).
  - f16: the stream and fetch caches' idle pass drops copies no draw read for 150 frames also when the heap is short
    (room below 256 MB, the rule of f7), not only when copies were refused at the cache's own cap; a dropped copy is
    made again when needed.
  - f17: the recorded command buffers' spares (up to 32 MB) go back to the heap while it is short (checked every 5 s
    from the frame's hook); before, only once an arena's growth had failed.
  - f14: the pipeline list's 16,358 jobs share their vertex layouts (one decoded block a distinct layout, by its
    encoded fields): each job held its own (a 540-byte layout, its elements' vector and a deque of their names: about
    1.5 KB and three or more allocations).
  - g1 (`banjo_renut_heap_walk_every`, 10; Nuts&Bolts/Debug): the memory report's `mallinfo()` walked every free piece
    of the heap (newlib's bins: 150,000-260,000 pieces over 3 GB, a cache and TLB miss each) on the render thread with
    State().mutex and the malloc lock held. The d4 log puts 25 ms (150,000 pieces) to 46 ms (260,000) between the
    lines before and after the walk, every 30 s (`renut_memory_report`'s default): a render-thread stall, and every
    thread that allocated meanwhile waited too. Now every 10th report (5 minutes), its time in the line; the reports
    between print what sbrk has handed out and what is left (O(1)). Diagnostics only. Emulator: 9-14 ms between the
    report's two lines at every report before (run f2), under a millisecond between walks after (final run).
- More render thread (2026-10-28, after the third profile):
  - g5: the pipeline list's file is written by a thread of its own. The render thread wrote and flushed each new
    pipeline's record (a call into the file system service a pipeline, hundreds in a row when a new area appends its
    pipelines) and synced the card (an fsync: milliseconds) at most once a second while records came. Now it appends
    the encoded record to a buffer under a small lock; the writer (a background thread's core and priority, started
    with the first record) wakes every second and writes, flushes and syncs what came. Same records, same order, on
    the card within about a second as before.
  - g3 (`banjo_renut_shared_host_shaders`, on): the live shader objects of one shader share one host shader. N&B makes
    a shader object per mesh, many alive at once with the same shader, and each made its own at its first draw on the
    render thread (deko3d: the GLSL unpacked with zstd, copied into the shader and parsed), kept its own copy of the
    GLSL text, and for each variant its pipelines used built the variant's text again (a copy of the whole GLSL with
    the defines) and hashed it (FNV-1a, a byte at a time) to find the compiled variant in the device's table (the
    host's own memo of variants is per host). The d4 city profile: zstd 0.35 ms, createShader 0.20 ms and memchr (the
    parse's `find`) 0.40 ms a frame on the render thread. `banjo_renut_shader_reuse` gave a destroyed object's host to
    the next object of its shader only. A host shader is made from the cache entry alone (the reuse's own rule), so a
    shared one is what making it again gives. The record keeps a raw pointer (its lines unchanged); a registry owns
    the hosts with a count of their records, and the last record's release hands the host to the reuse as before. The
    log counts them ("host shaders: N made, M shared ...", at 256, 512, ...). Off: one host an object (the registry
    keyed by the object).
  - g4: `PagesClean` (the store cards' proof of a cached copy) takes eight pages at a time too, after the sixteen-page
    blocks: a range's last 8-15 pages and ranges of 8-15 pages (most mesh buffers) went a page at a time. The same
    test and exclusions as the sixteen-page block.
- h1 (the console's out-of-memory crash, night_final package, 2026-10-28 05:43): in a new area the GPU's textures
  reached 1.2 GB, the heap's room fell to 18-34 MB with 300 MB free in 174,000 pieces, and the process ended with no
  crash log: deko3d takes a memory block's storage from the heap itself and aborts the process when it cannot. The
  image heap (plume) took texture memory in 64 MB chunks and never gave a chunk back, so 1,600 evicted textures freed
  space inside the chunks only. Now: plume tries the block's allocation first (memalign of the same size and
  alignment, freed at once; deko3d then allocates and zeroes its own) and, when the heap has none, gives back the
  image heap's empty chunks and the spare command buffers (a callback) and tries again; still none: a log line
  ("memory block: ... NONE MADE") and no block (every caller has a failure path: a buffer or texture without memory)
  instead of the abort. (The first form gave deko3d storage of its own: the emulator hung at start, since deko3d
  zeroes only the blocks it allocates and the zero vertex buffer and command memory rely on it.) The image heap gives back a chunk once it is empty while the heap's
  room is below 256 MB, and a new 64 MB chunk that does not fit is asked again at the image's own size.
- h9 (the night_gpu2 console freeze, 2026-10-28 09:49): h1 turned the out-of-memory abort into a 95 s freeze at 0.1
  fps. The heap had taken 3,076 of its 3,085 MB, with 577 MB free in 170,000 pieces and none of 32 MB; on docking the
  stream cache's pool (127 of 128 MB) asked for a new 32 MB chunk for every new stream: 3,812 failures. Each one took
  22 ms: the log line's `mallinfo` walked every free piece under malloc's lock (the audio threads waited in `free`),
  then an SD card write. Now: the pool makes no new chunk for 600 frames after one fails (an 8 MB chunk is tried
  first; a chunk released ends the wait; every caller already takes the uncached path); in `makeBlock` a request at
  least as big as one that failed in the last 250 ms fails at once, and only the first 8 failures take the heap's
  figures (then one line per 256, with the counts). Lesson for ports: never call `mallinfo` (or walk the heap) on a
  path that can repeat, and give every pool that grows a back-off after a failure. The heap's fragmentation itself
  (GPU blocks from the same newlib heap as every small allocation) is the cause left: h10.
- h10 `banjo_deko3d_gpu_region` (on; same pixels): deko3d memory blocks of 64 KB and more come from a region of their
  own at the heap's top (plume_deko3d.cpp, "The GPU region"; the ranges in plume_deko3d_region.h). The region grows
  down by lowering `fake_heap_end` (devkitPro newlib's `_sbrk_r` reads it at every call: checked in the disassembly),
  moved under `__malloc_lock` and never closer than 32 MB above newlib's break; blocks are taken from the top of the
  highest free range that fits (64 KB grain, growth in 2 MB steps), so free space gathers at the bottom, and once 128
  MB there is free all but 64 MB goes back to newlib. Storage handed to deko3d is zeroed and cache-flushed by plume
  (deko3d zeroes only storage it allocates). A block the region cannot place takes the old path (newlib, with h1/h9's
  guards). No size is fixed in advance: battd_nx's fixed GPU arena (bp_gpuarena.c, from cloverpit / pvz_fusion_nx,
  MIT) had to be re-sized by hand from measured peaks; its other lessons are taken (no logging under the region's
  lock, failures counted without I/O, a test of the allocator: its first version refused requests with a fitting
  run free). tools/test_gpu_region.cpp drives the ranges against a mock heap whose break moves (40 seeds x 20,000
  steps: merged, in bounds, no overlap, used + free = size, no refusal while a range fits or growth is allowed). The
  memory report's "GPU region" line gives its size, blocks, free ranges, growths and give-backs.
- Console A/B (night_final, renut_ab.toml, one 30 s half each, static city, GPU-bound at 40.6 ms): render thread with
  the item on minus off: 54 -0.71 ms, 55 -0.62, 56 -0.43, 52 -0.23, 59 -0.20, 61 -0.20, 62 0, 60 +0.12 (a driving
  item), 63 +0.13, 53 +0.64, 57 +0.99 (10 s windows vary by about 0.8 ms). 53 and 57 turned off (the user).
- GPU block (2026-10-28, after the night_final console run: the GPU, 40-45 ms, now sets the city's frame rate):
  - h4 `banjo_deko3d_depth_handover_readonly` [64] (on): a handed-over depth resolve is kept (not drawn) across a bind
    of its target for draws that write neither depth nor stencil (`BindTargets(t, depth_writes)`; the fast path binds
    a handed target again only for a draw that writes it). The target's `writes`, which the hand-over checks, moves
    only for such writes, so the image is the one the resolve read. The night_final city drew all 438 handed resolves
    a 10 s after all at such binds. Emulator title screen: none kept (its scene windows drew 264 of 264 after all for
    another cause): the hand-over line now counts the causes (a depth-writing draw's bind, a draw sampling the texture
    with its target bound, other).
  - h5 `banjo_renut_anisotropy_cap` (0, off; NOT pixel-identical, the user's call): the most anisotropic filtering a
    sampler gets. The heaviest shaders are texture-bound uber shaders (PS 9ED9E1A8 17.4 ms in the new area, the city's
    water PS 3277104C 4.9 ms; 14-21 texture reads a pixel). The log counts the samplers made by the game's ratio.
    renut_aniso4.toml sets 4.
  - h7/h8 `banjo_renut_dynamic_resolution` (on by default on the Switch, off on the PC: the user's
    call, NOT pixel-identical), `banjo_renut_dynamic_handheld_low` / `_high` (540 / 720),
    `banjo_renut_dynamic_docked_low` / `_high` (540 / 1080 since 1.0.0), `banjo_renut_dynamic_gpu_ms` (34.5, a decimal key since
    h11, the user's choice; 40 before),
    `banjo_renut_dynamic_test_s` (0; alternate every N s, for tests): dynamic resolution, a range per mode. The 3D
    image takes the low end while the GPU's time a frame (a moving average of the timestamps' busy time, about 16
    frames) is above the budget, and returns to the high end when the high end's predicted time (the low time times
    the pixel ratio, an overestimate) has been under 85 % of it for `banjo_renut_dynamic_up_s` in a row and that long
    since the last change (h11: 1 s, the user's "be aggressive"; h9 tried 20 s after a loading screen's light frames
    took the city to 720 for 12 s, each change making a set of targets: the h10 GPU region now keeps that memory
    apart); down at least 5 s after a change; a change of mode starts at its
    720 end (h9: docking at 900 made the biggest set of targets with the heap at its end). Each range has 720 at one end: that end draws at 1x (code 0, the game's own targets) and the other
    end takes the mode's code (4 handheld, 6 docked), set once for the session. The shaders read both heights from
    SharedConstants (`InternalHeightsWord`), so a change is the same switch of render targets the menus make (no
    shader change). A range without 720 at one end is refused with a warning (the fixed height). night_gpu5 (h19,
    the user's request): docked is 720 to 1080. An end of 1080 is code 2 (1.5x, the docked display's own size), as a
    fixed 1080 always was: the HUD is drawn into the scene without the upscale. Back up to 1080 needs about 13 ms at
    720 (34.5 x 0.85 / 2.25), so the busy city stays at 720 docked and lighter scenes go to 1080. The HUD stays at
    the display's resolution.
    Steps (night_gpu5, h19, the user's request; `banjo_renut_dynamic_steps`, 4): four heights between the ends,
    evenly spaced and multiples of 36. Handheld: 540 576 612 648 684 720. Docked: 720 792 864 936 1008 1080. Down goes
    to the highest step predicted to fit the budget (the average times the pixel ratio), the lowest if none. Up goes to
    the highest step predicted under 85 % of it, once the next step has been under that for `banjo_renut_dynamic_up_s`.
    Each is one change, with the same holds as before. The shaders know only two settable scale codes (4 and 6, whose
    heights they read from SharedConstants), so steps other than 720 (code 0) and 1080 (code 2) take turns on those two
    (surfaces.cpp `CodeForHeightLocked`). A height takes the code that holds it, or else the code not in use now: that
    code's targets are retired first and built again at the new height at their next use. Its resolve destinations are
    made again at their next resolve (textures.cpp, by their size). A resolve texture still at an old height and
    sampled later would have to go two changes without a resolve into it. `banjo_renut_dynamic_test_s` walks the steps
    one at a time. Deko3d only (the PC back ends keep the two ends). The thread profile behind it (night_final city, a frame of 47 ms): GPU 44.5
    ms busy, render thread 31.7 ms of CPU, the game's main thread 27.9 ms, cores 0-2 about 82 % used: the GPU sets the
    frame rate. With the city at 40-45 ms of GPU at 648 lines, handheld will sit at 540 there (with 34.5 ms, back to
    720 needs the average under about 16.5 ms at 540 for 1 s; docked 720 to 900 under about 18.8 ms at 720).
    renut_dynamic_off.toml: the fixed 648 / 864.
    Console (night_gpu2, three cores, 2026-10-28): the city at 540: GPU 34-36 ms (42 at 648), render thread 39-43 ms,
    21-23 fps: the render thread is the limit again. Two shaders cost the same at 540 as at 648 (PS 9ED9E1A8 17.2 /
    17.4 ms in the new area, the water PS 3277104C 4.98 / 4.9 ms in the city): not pixel-bound (vertex work likely),
    so resolution does not reach them.
  - `banjo_renut_diag_push_lines` (Banjo/Debug): the pushed shared-constant lines by line and the float constant bytes
    by stage. Emulator title: the texture and sampler index lines are 80 % of the shared pushes (real changes); the
    whole constant stream is worth about 0.6 ms of the GPU front end a frame: not worth narrowing.
  - Looked at and left: predicated tiling (already drawn once: D3D's per-tile replay is left out); barriers (72
    fragment, 42 primitive, 19 full a city frame) match real read-after-write / write-after-read dependencies of about
    25 resolves and a 20-pass post chain; the full ones are the copy engine's (26 buffer copies a frame: memexport seeds
    and fetch data); colour resolves (the two full-scene ones are followed by more drawing into the target: a copy is
    needed); shadow maps (the shadow sort cost +0.65 ms of GPU on 10-27: not shader changes; likely triangle setup);
    the uber shaders (already through the exact passes; their per-pixel texel offsets from uniform sizes would need
    the GPU's reciprocal reproduced on the CPU to stay exact); colour and depth targets are already compressed; the
    post buffers already at the console's 8 bits (renut_post_process_precision = Game in renut.toml).
- Parked (2026-10-28): the memexport seeds written by the CPU straight into the export buffer's fresh windows (every
  plume deko3d buffer has a CPU address, uncached), which would drop each memexport draw's copy-engine copy and its
  full barrier (about 19 full barriers a frame in the d4 city). The windows are also written by the GPU, and a line
  of an earlier frame's export still in the GPU's L2 could shadow the CPU's write: the copy engine goes through the
  GPU's memory system, a CPU write does not. Needs a hardware check of deko3d's L2 handling at submission first.
- Also: f10, the shader record memo 4,096 sets a kind (1,024): N&B makes a shader object per mesh. f12 (GPU): a
  command list drops only the GPU caches whose contents changed (descriptor caches when descriptors were written,
  the shader instruction cache when code was loaded; every new texture emptied both).
- `banjo_renut_replay_group_pointers` [52] (on), `banjo_renut_replay_group_pointers_verify` (Banjo/Debug): a replay
  keeps a pointer to each float constant group's bytes where the op recorded them (the arena) and a draw reads its set
  groups there (`FloatConstants::Register`); the ops no longer copy their groups into the device image (64 bytes a
  group). A sorted run's groups go into the image and point there; a nested run gets the set groups written into the
  image first. The verify copies them too and compares every read with the image.
- `banjo_renut_texture_slot_skip` [53] (SLOWER ON THE CONSOLE, OFF since the night_final console A/B: render thread +0.64 ms a frame with it
  on), `banjo_renut_texture_slot_skip_verify` (Banjo/Debug): when a draw changes
  some texture slots, a slot whose texture object the object path answered last, with the same fetch words and the
  same frame, binding generation and layout epoch, into the fast path's own effects array, keeps its words and effects
  (only the depth counters count). The verify runs the object check for each kept slot and compares.
- Diagnostics: the sampler's per-thread report lists the callers of samples in `memcpy`, `memmove` and `memset`
  (`lrleaf image+0x... xN` lines), so the render thread's copies can be attributed. The fast-draw line splits the
  pipeline cache's misses ("made") by what the slot held: empty, a conflict (another pipeline's answer: the table is
  direct-mapped), provisional (this pipeline's from a past prebuild epoch) or invalidated (generation). The emulator's
  title screen still made about 10 % after f18 (g1 verify run). In the PGO session's route (run pgo7) every miss was
  a conflict, 2.3 % of the lookups (about 7,000 per 10 s): a two-way table would save about 0.06 ms a frame, not
  worth a change to the fast path's hottest function. The same session: 782 host shaders made, 3,291 shared (g3).
  ("Conflicts" counts every miss on an occupied slot, a key's first lookup included: the title screen's 10-23 % in
  the final verify go with 1,800-2,000 new render states per 10 s, its flyover's scene changes.)
- Final emulator verify (NRO 7c553231c275, profile from the final source, every verify key that leaves the new paths
  on): no difference in any verify, no fast-draw pipeline warning, chain breaks 0, the heap walk 8.5 ms. With the
  new-off keys (renut_new_off.toml) the same NRO boots and verifies clean: 1,022 host shaders made and 1,020 alive
  with the sharing off (332 and 330 with it on).
- Emulator verify of the g items (title screen, every verify key that leaves the new paths on, NRO 7e5bf155a682):
  no difference in any verify; one fast-draw pipeline warning (the generic/specialised epoch race, as in run f2);
  chain breaks 0 (f15; 2,000-4,800 per 10 s in run f2), "no state" about 200 per 10 s (f3); the heap walk 11.9 ms in
  the emulator (66,000 pieces), the O(1) line between; host shaders 332 made and 690 shared with a live object of
  the same shader (g3: two thirds of the makes gone, 330 alive instead of about 1,020); the list writer started.
- `banjo_system_core_uam` (Banjo/Threads, on; the user's request 2026-10-28): with four cores granted, the uam shader
  compile thread alone runs on the system core (core 3) at the lowest application priority, so the audio threads there
  go first, its background compiles held to the share below by their rests. The pipeline prebuild workers stay on
  cores 0-2 (`banjo_system_core_compiles`, off: the first four-core run had them, the compiles at a 25 % share and the
  XMA decoder on core 3, and audio stuttered). A compile a draw waits for runs at the waiting thread's priority, above
  the mixer's: if audio stutters again, set this key false.
- `banjo_deko3d_background_compile_share` now 10 (was 25) and the rest after a background compile up to 30 s (was 8 s,
  which held the share at 20 % or more for compiles of about 2 s). The c4 runs compiled run-time variants without end (50
  and 135 a run), a quarter of core 2. Same pixels: the generic variant draws until the specialised one is in.

### Host draw packets (2026-10-28, the user's mandate; h12-h15)

- `banjo_renut_host_packets` [65] (**OFF** by default, the user's call: NEUTRAL on the console. night_gpu5: render thread
  -0.54 ms a frame in each of three city rounds, 3-core, 15 s halves; +0.45 ms in the four-core check with 30 s halves
  and a control. The table, about 1.6 MB, takes much of the A57's 2 MB L2; exact): a replayed draw's derived state kept in a packet (render/draw.cpp `HostPacket`, 8,192 direct-mapped):
  the shader records, host shaders and cache entries, the declaration and strides, the targets, the depth-only answer
  and texture mask, the bools and alpha test, the viewport, the render-state words and their number. A later draw with
  the same packet key takes them instead of deriving them again (no shader or declaration lookup, no record line read
  for them: the records' lines are asked for at once instead). Checked besides the key: `g_packet_epoch` (raised when a
  shader or declaration record or a host shader goes: a few times a 10 s in the city), the surfaces, the tiles and the
  replay's predication. A packet is written only when the slot holds none current for the key.
- The key (OpCall::state_key, guest/command_buffers.cpp): made by the recording threads as they record, the XOR of a
  mixed 64-bit hash per 64-byte chunk the op copies, kept per device so an op hashes only its changed chunks. It covers
  only the device WORDS the answers read (kPacketWords, from the field tables: shaders, declaration, targets, the
  window/values/control and destination packets, bools, 0x29C0, predication; the executor's own dependency sets, which
  its verify checks). Census (`banjo_renut_diag_packet_census`, emulator title): keying the whole device state found the
  same state this frame or the last for 5.6 % of the draws; the field census showed why: the streams, index buffer and
  vertex fetch constants (17-27 % recent without them, 57-62 % alone) and the texture words (76 %) change from frame to
  frame, while each of the answers' fields alone is 99-100 % recent. With the narrow key: 64.9 %, packets found for
  58-70 % of the draws, the key always the state's own (4.48 M draws), verify 0 differences.
- Emulator A/B 65: no gain (the render thread 1-2 ms slower in the matched windows); the x86 JIT's caches hide the A57
  misses packets remove, while their cost shows. The console A/B decides (renut_ab.toml). Lesson for ports: key a
  derived-state cache by exactly the inputs of what it holds; whole-state keys miss on the parts that churn.
- A/B targets 64 and 65 (2026-10-28): the A/B lists dropped every target from 64 on (AbTargets, Stats::by_target); the
  limit is 128 now and a target past it is logged. night_gpu1-3's renut_ab.toml never measured 64.
- `banjo_renut_lazy_resolve_mips` [66] (on; exact; from sa2-nx's sa2_renut_lazy_resolve_mips, credit sa2-nx): a scaled
  colour resolve destination's host levels (HostTexture::extra_levels: docked, the 3D at 1.25x and the HUD at 1.5x)
  were made after every resolve into it (UpdateResolveMipsLocked: a scratch draw a level, copies and barriers), sampled
  or not. Now the resolves widen a pending region (mips_pending, mips_rect) and FlushResolveMipsLocked makes the levels
  at the texture's first BindTextureLocked, or before an Upload or CopyDepthWords writes level 0 (Retire drops them).
  Banjo's difference from sa2-nx: the draws that take a texture's words without BindTextureLocked hold by two kinds of
  epoch (the lean executor's kept slots by g_texture_epoch; the texture objects, slots' last answers and memo by the
  texture's binding buckets), so the first pending region bumps both (g_texture_epoch and BumpTextureBindings). The
  "fast draws" 10 s line counts the levels left for their first read and those made at it. For Banjo on the console it
  does nothing: host levels are made only at 2x or more (log2 of the scale), and Banjo's scales are 1.25x (docked 3D)
  and 1.5x (docked HUD); the docked emulator runs made none before or after (h16: 0 verify differences). It is for
  ports that render at 2x or more (a PC build at 4K).
- `banjo_renut_clear_repeat` [67] (on; exact; from sa2-nx's sa2_renut_clear_repeat, credit sa2-nx; h17): a whole clear
  of a target that holds exactly that clear already is not sent (render/draw.cpp ClearTargets). Held per HostSurface:
  the last whole clear's values (memcmp, bit for bit; depth and stencil each known or not) and the generation and writes
  it left. A clear repeats when the target is cleared whole again with the same values and both are unchanged. Everything
  else about a skipped clear stays: its writes are counted, BindTargets (and so the depth hand-over) runs before the
  check, and aliases are still cleared and lose what was known. Depth is skipped only when every plane being cleared is
  known to hold that value. The one condition for porting: every path that writes a target's image must bump `writes`
  or change `generation`. Banjo's writers: draws (the fast and full paths, by write mask, depth write or stencil
  writes), clears, alias clears, SMAA's final blend and the UI upscale (colour and depth) all bump writes. A depth
  hand-over swaps the image with its generation (SwapDepthImageLocked). A rebuild takes a new one. Every image's
  generation comes from g_next_generation, so a swap can never bring back the one a clear recorded. There are no target
  discards and nothing copies into a target's image. The "fast draws" 10 s line counts whole clears and those not sent.
  sa2-nx clears its scene target whole twice a frame (27,001 skipped in a 1080p demo). Banjo, emulator title loop
  (h17, A/B 67 at 5 s halves): 15-20 % of the whole clears repeat (about 2 a frame). All 20 captures drew correctly in
  both halves, with 0 verify differences.

### Render-thread round from the first line profile (2026-10-28 evening, h20)

night_gpu5's console run was the first with line tables for Renut's files (BANJO_SWITCH_RENDERER_LINES now gives
draw.cpp, textures.cpp, surfaces.cpp and command_buffers.cpp -g1). devkitA64's addr2line cannot read GCC's DWARF 5 line
tables (every line ":?"); llvm-symbolizer from MSYS2 clang64 can. City, four-core: FastDrawLocked 25.5 % of the render
thread, Replay 16, PushFloatConstants 8, FillTextures 7.5, WholeGeometryTry 4.5, memcpy 4.3; the hottest line 2 %.
Exact items from it:
- `banjo_deko3d_provisional_count` [68] (on): plume counts its provisional pipelines (made at creation, dropped by the
  refresh that finds every variant or by the destructor). While the count is zero, a draw does not read its pipeline
  object for Deko3DRefreshPipeline's check (0.9 % of the render thread, a line of the pipeline object at every draw,
  bound or not).
- LiteralRuns in one block (no key: a layout): the header, the runs and the values in one 64-byte aligned allocation.
  Before, two vectors in their own allocations: the shader change's prefetch loaded the object and both vectors'
  pointers before it could prefetch (0.49 %), and the push read three places.
- The geometry objects' and fetch pool's stamps (`last_used`, `proven`) stored only when they change. Before, each
  draw dirtied those lines, which were then written back to memory.
- Host draw packets off by default (above): their key, lookup, store and prefetch lines were about 1.5 % of the
  render thread, but they saved about as much, so the net was neutral.
- Looked at and left:
  - The shader record check in PrefetchShaderBehind (2 %) is the first touch of a record the draw reads anyway. Taking
    it out moves the wait, and prefetching the record an op earlier (57) measured nothing.
  - The fetch data memcpy is data the game rewrites each frame (bone palettes; the fetch cache already skips about
    2 GB per 10 s of data that stays the same).
  - The geometry proofs are already once per kick.
- The diagnostics in renut.toml (the GPU profile by shader, the resolve trace) cost about 0.4 % of the render thread in
  per-draw checks and counts. renut_play.toml turns them off.

## Other keys

Not enhancements, but they decide how the ones above run or are measured.

| Key | Default | What it does |
|---|---|---|
| `banjo_cpu_verify` | `false` | every self-check on every use, natives run both ways; the `[verify]` line's differences must be 0. Slow |
| `banjo_hot_guard_calls`, `banjo_hot_guard_period` | `64`, `4096` | the localized copies' guard: the first N calls of each thread, then 1 in P |
| `banjo_renut_exact_streams` | `true` | Renut's renderer (package 27): a converted vertex stream or index range is hashed whole at its first use after each kick and converted again if the guest rewrote it (Renut checked them on a background thread every 4 frames). Correctness, not an enhancement; it costs: PC +1.47 ms of render thread a frame (5.00 -> 6.47), an estimated 6-8 ms on the A57; in-run A/B target 6 of `banjo_renut_ab_target` |
| `banjo_renut_memory_policy` | `true` on the Switch | Renut's renderer (package 27): limits for the console's memory instead of a PC's: cold textures released below `banjo_renut_textures_reserve_mb` (`384` on the Switch) of the budget or with the bindless table three quarters full, the stream cache at `banjo_renut_stream_cache_mb` (`128`) and `banjo_renut_stream_cache_idle_s` (`20`), upload ring chunks trimmed after `banjo_renut_upload_trim_frames` (`120`) |
| `banjo_renut_keep_gpu_written` | `true` | Renut's renderer (package 27): a texture the GPU wrote is never released (made again from guest memory it would lose what the GPU wrote) |
| `banjo_renut_diag_poison_slots` | `false` | a test (package 27): released descriptor slots point at a magenta texture instead of the null one |
| `banjo_renut_diag_build_delay_ms` | `0` | a test (package 27c): every pipeline build of Renut's renderer waits this long first, so the PC compiles as slowly as the console |
| `banjo_renut_diag_trim_poison` | `false` | a test (package 28): the channels `banjo_gpu_output_trim` leaves out are 60000 instead of 0 (the picture must not change) |
| `banjo_renut_diag_resolve_trace` | `false` (`true` in the Switch `renut.toml` since package 28) | a measurement (package 28): one frame every 30 s of Renut's resolves, clears, target changes (with their draw counts) and first samples of resolved textures, as `[trace]` lines |
| `banjo_renut_ab_target`, `banjo_renut_ab_s` | | the in-run A/B (above) |
| `switch_profile_stacks` | `true` in `renut.toml` | the SDK's sampling profiler (`logs/rex/rex_profile.log`) |
| `banjo_cpu_profiler` | `false` | the app's own sampler (`banjo_profile.toml`) |
| `banjo_pool_lock` | `true` | a fix, not an enhancement: serializes the game's resource-pool reset and its getter (the start-up crash "read of guest 0x24") |

## Audit

`python tools/enhancement_keys.py --audit docs/enhancements-reference.md` reads the keys from the sources (the rows of
`LogEnhancements()` and the localized copies' list `app/generated/hot/banjo_hot_list.inc`, their definitions and the
shipped `renut.toml`) and checks this page against them: every key name in backquotes is defined somewhere, every row of the table
above has the key's group and shipped value, every key appears on the page, every premade config named exists in
`dist/switch/configs`, and the A/B target table matches the key's own description. Run it after adding a key.

It was written when this page also listed the earlier renderer's keys, so it reports those as missing, and it reports the premade configs that are no longer listed. Read its output with that in mind.
