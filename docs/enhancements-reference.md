# Enhancements reference

Performance changes that keep the picture and the game's behaviour the same, and the renderer fixes that are switched
the same way. Each has its own key in `renut.toml` (`sdmc:/switch/banjo-nx/renut.toml`) and belongs to a group whose
master key must also be on:

| Master key | Shipped | Covers |
|---|---|---|
| `banjo_cpu_enhancements` | `true` | host CPU work: the game code's native functions, the ring thread's fast paths, waits, the runtime |
| `banjo_noncpu_enhancements` | `true` | GPU, driver and storage: EDRAM work, shader variants, command recording, the renderer fixes |

The framework is `app/src/cpu/banjo_enhancements.{h,cpp}`. This page lists every key: what it changes, why the result
is the same, how it checks itself, and what was measured. The history behind them (the console runs, the order of the
work, what was tried and dropped) is in `docs/enhancements.md` and `docs/d3d-native.md`. The changes made to the
generated game code at build time have no run-time key and are on their own page, `docs/codegen-enhancements.md`.

`python tools/enhancement_keys.py --audit docs/enhancements-reference.md` checks this page against the sources (see
"Audit" at the end).

## How a key takes effect

- An enhancement is active when its own key **and** its group's master key are on (`EnhancementActive`).
- Almost every key is read once at start (`kInitOnly`). Change the toml and restart.
- masseffect-nx's ring fast paths are off in the code and on in `renut.toml`. A toml from an older package that lacks
  their keys would leave them off silently, so at start, when the toml, the environment and the command line do not
  set such a key and its master is on, it gets its "on" value (`kDefaultsOn`). A key the toml sets is always
  respected. The `[enhancements]` line counts how many were switched on that way.
- With `banjo_cpu_enhancements = false` the code also resets those masseffect-nx keys and the two SDK keys
  (`kernel_critsec_pcr_owner`, `thread_signal_skip_idle`), whose code reads the key directly.
- `log_file_writer` is read when logging starts, before the master switches: the off configs set it to `false`
  themselves.

## What the log shows

- `[enhancements] optimised codegen (...); cpu master on, non-cpu master on | N of M active, K switched on because
  the toml does not set them | <key> ON, <key> off, <key> off (master), ...`: the first line. `off (master)` means the
  key is on but its group is off. It also names the prewarm thread count and whether the two profilers are on.
- `[enhancements] instruction forms self-check: N of N results as expected`: every helper that has a form for one
  processor only, run against a plain C++ reference at every start (248 results on the console).
- The renderer's report every 10 s: `ring thread:` (work per Swap and the stages of a draw), `GPU per Swap` (GPU time
  per pass category and the idle gap), and one line per enhancement that counts its uses (for example `overwrite
  proof`, `tiles once`, `EDRAM mode4 split tiles`, `[native] Direct3D Clear markers`, `PMULL fingerprints`).
- `[hot] <function> ON: about N calls so far, N compared with the original, N not comparable, N differences`: the
  native copies of game functions.
- `[verify] checks done: ... | differences: D`: D must be 0.
- `DIFFERENCE` or `MISMATCH`: a self-check found a result that differs from the original path. The enhancement
  switches itself off for the rest of the session, and the line counts into the verify line's D. None appeared in
  the console runs of 2026-10-13 and 2026-10-14.
- In an A/B run: `A/B: target N` at every switch, `ring thread A/B` (the ring thread's time per Swap in both halves)
  and `GPU A/B` (GPU time per category in both halves).

## Premade configs

`python tools/make_enhancement_configs.py dist/switch/banjo-nx/renut.toml dist/switch/configs` deletes the old ones and
writes these from the shipped `renut.toml`. Each differs from it only in the keys its first line lists, so two logs
compare directly. Copy one over `sdmc:/switch/banjo-nx/renut.toml`.

| File | Change from `renut.toml` |
|---|---|
| `banjo_cpu_on.toml` | none (as shipped, profilers on) |
| `banjo_cpu_off.toml` | the CPU group off (`log_file_writer` too) |
| `banjo_noncpu_off.toml` | the non-CPU group off |
| `banjo_all_off.toml` | both groups off: the original paths everywhere |
| `banjo_cpu_verify.toml` | `banjo_cpu_verify = true` and `banjo_direct_state = true`: every self-check on every use; profilers off; slow |
| `banjo_profile.toml` | also the app's own sampler (`banjo_cpu_profiler`, `logs/rex/banjo_profiler.log`) |
| `banjo_direct_ab.toml` | `banjo_direct_state` on, in-run A/B target 1 |
| `banjo_memo_ab.toml` | in-run A/B target 2 (`banjo_ring_edram_memo`) |
| `banjo_tiles_once_ab.toml` | in-run A/B target 9 (`banjo_gpu_tiles_once`) |
| `banjo_vertex_persistent_ab.toml` | in-run A/B target 11 (`banjo_ring_vertex_persistent`) |
| `banjo_shader_load_memo_ab.toml` | in-run A/B target 12 (`banjo_ring_shader_load_memo`) |
| `banjo_dead_draws_ab.toml` | in-run A/B target 22 (`banjo_gpu_skip_dead_draws`) |
| `banjo_early_alpha_ab.toml` | in-run A/B target 26 (`banjo_gpu_early_alpha`) with the per-shader GPU groups |
| `banjo_texture_signs_ab.toml` | in-run A/B target 27 (`banjo_gpu_texture_signs_per_slot`) with the per-shader GPU groups |
| `banjo_clear_merge_ab.toml` | in-run A/B target 28 (`banjo_gpu_edram4_clear_merge`) with the GPU groups |
| `banjo_vfetch_block_ab.toml` | in-run A/B target 29 (`banjo_gpu_vfetch_block`) with the per-shader GPU groups |
| `banjo_branchless_ab.toml` | in-run A/B target 30 (`banjo_gpu_branchless_select`) with the per-shader GPU groups |
| `banjo_bool_spec_ab.toml` | in-run A/B target 31 (`banjo_gpu_bool_spec`) with the per-shader GPU groups |
| `banjo_stencil_fetch_3d_ab.toml` | in-run A/B target 32 (`banjo_gpu_stencil_fetch_3d`) with the GPU groups |
| `banjo_lean_barriers_ab.toml` | in-run A/B target 33 (`banjo_gpu_edram4_lean_barriers`) with the GPU groups |
| `banjo_resolve_clear_merge_ab.toml` | in-run A/B target 36 (`banjo_gpu_resolve_clear_merge`) with the GPU groups |
| `banjo_pred_select_ab.toml` | in-run A/B target 37 (`banjo_gpu_pred_select`) with the per-shader GPU groups |
| `banjo_pred_select_ps_ab.toml` | in-run A/B target 38 (`banjo_gpu_pred_select_ps`) with the per-shader GPU groups |
| `banjo_tess_grid_ab.toml` | in-run A/B target 39 (`banjo_gpu_tess_grid`) with the per-shader GPU groups |
| `banjo_shaders21_ab.toml` | in-run A/B target 40: targets 29, 37, 38 and 39 together (the shader round of 2026-10-16) |
| `banjo_clamp_cbuf_ab.toml` | in-run A/B target 49 (`banjo_gpu_clamp_cbuf`) with the per-shader GPU groups |
| `banjo_sign_mask_skip_ab.toml` | in-run A/B target 50 (`banjo_gpu_sign_mask_skip`) with the per-shader GPU groups |
| `banjo_vs_outputs_unwritten_ab.toml` | in-run A/B target 51 (`banjo_gpu_vs_outputs_unwritten`) with the per-shader GPU groups |
| `banjo_bone_padding_ab.toml` | in-run A/B target 52 (`banjo_gpu_bone_padding`) with the per-shader GPU groups |
| `banjo_robustness_off_ab.toml` | in-run A/B target 53 (`banjo_gpu_robustness_off`) with the per-shader GPU groups |
| `banjo_edram_checks_cpu_ab.toml` | in-run A/B target 54 (`banjo_gpu_edram_checks_cpu`) with the per-shader GPU groups |
| `banjo_shaders25_ab.toml` | in-run A/B target 55: targets 49-54 together (the shader round of package 25) |
| `banjo_shaders25_off.toml` | package 25's six keys off for a whole run |
| `banjo_literals_cbuf_ab.toml` | in-run A/B target 56 (`banjo_gpu_literals_cbuf`) with the per-shader GPU groups (package 26) |
| `banjo_stencil_clear_partial_ab.toml` | in-run A/B target 57 (`banjo_gpu_stencil_clear_partial`) with the GPU groups (package 26) |
| `banjo_import9_steps.toml` | diagnostic: every 9-pass EDRAM import timed per step in the GPU report (`banjo_diag_import9_steps`, package 26) |
| `banjo_renderer_renut.toml` | package 26: Renut's renderer (`banjo_renderer = "renut"`, `docs/renut-renderer-port.md`) |
| `banjo_renut_tuning_off.toml` | Renut's renderer without banjo-nx's tuning of it |
| `banjo_renut_upload_uncached.toml` | Renut's renderer with uncached upload memory (`banjo_renut_upload_cached = false`) |
| `banjo_renut_barriers_full.toml` | Renut's renderer with full barriers (`banjo_renut_precise_barriers = false`) |
| `banjo_renut_xxh3.toml` | Renut's renderer hashing with XXH3 (`banjo_renut_pmull_hash = false`) |
| `banjo_renut_unpinned.toml` | Renut's renderer with its threads not pinned (`banjo_ring_core` and `banjo_background_compile_core` -1) |
| `banjo_renut_nomsaa.toml` | Renut's renderer with the game's MSAA off (`disable_msaa`): one sample per pixel, the image banjo-nx's renderer draws on the console |
| `banjo_renut_post_high.toml` | Renut's renderer with its own default 16-bit post-processing buffers (the others use the console's 8-bit) |
| `banjo_renut_dedupe_ab.toml` | Renut's renderer, in-run A/B target 3 (`banjo_renut_shared_dedupe`) |
| `banjo_renut_push_ab.toml` | Renut's renderer, in-run A/B target 4 (`banjo_renut_push_point_only`) |
| `banjo_renut_pool_ab.toml` | Renut's renderer, in-run A/B target 5 (`banjo_renut_capture_pool`) |
| `banjo_renut_barriers_ab.toml` | Renut's renderer, in-run A/B of `banjo_renut_precise_barriers` (`banjo_renut_ab_target = 1`) |
| `banjo_renut_pin_ab.toml` | Renut's renderer, in-run A/B of the render thread's pin (`banjo_renut_ab_target = 2`, the Switch) |
| `banjo_renut_cpu_off.toml` | Renut's renderer with the CPU master switch off |
| `banjo_renut_exact_ab.toml` | package 27: Renut's renderer, in-run A/B of `banjo_renut_exact_streams` (`banjo_renut_ab_target = 6`: what the exact rule costs) |
| `banjo_renut_exact_off.toml` | package 27: Renut's renderer with Renut's own stream check only (the `[renut]` line counts the copies it finds late) |
| `banjo_renut_memory_policy_off.toml` | package 27: Renut's renderer with Renut's PC-sized memory limits |
| `banjo_renut_eviction_test.toml` | package 27, a test: texture eviction forced every 32 frames and released slots poisoned magenta (any magenta on screen is a bug) |
| `banjo_renut_boolspec_ab.toml` | package 27: Renut's renderer, in-run A/B target 7 (`banjo_gpu_bool_spec`) |
| `banjo_renut_blendskip_ab.toml` | package 27: Renut's renderer, in-run A/B target 8 (`banjo_gpu_blend_skip`) |
| `banjo_renut_tessgrid_ab.toml` | package 27: Renut's renderer, in-run A/B target 9 (`banjo_gpu_tess_grid`) |
| `banjo_renut_checkworker_ab.toml` | package 27c: Renut's renderer, in-run A/B target 10 (`banjo_renut_check_worker`) |
| `banjo_renut_depthonly_ab.toml` | package 28: Renut's renderer, in-run A/B target 11 (`banjo_gpu_depth_only`) |
| `banjo_renut_trim_ab.toml` | package 28: Renut's renderer, in-run A/B target 12 (`banjo_gpu_output_trim`; its off half compiles the untrimmed pipelines on first use: stalls in the first minutes) |
| `banjo_renut_words_ab.toml` | package 28: Renut's renderer, in-run A/B target 13 (`banjo_gpu_depth_words_view`) |
| `banjo_renut_trim_poison_test.toml` | package 28, a test: the channels `banjo_gpu_output_trim` leaves out are 60000 instead of 0; the picture must be the same as with `renut.toml` |
| `banjo_renderer_native.toml` | package 28: `renut.toml` with banjo-nx's renderer (`renut.toml` boots Renut's since package 28; the configs of banjo-nx's renderer's own keys boot that renderer too) |
| `banjo_ring_core_ab.toml` | in-run A/B target 41: the ring thread pinned to core 1 against the whole core mask |
| `banjo_ring_decoder_ab.toml` | in-run A/B target 43: the ring split (`banjo_ring_decoder`) against its serialized off half |
| `banjo_vs_hash_given_ab.toml` | in-run A/B target 44 (`banjo_ring_vs_hash_given`) |
| `banjo_d3d_records_ab.toml` | in-run A/B target 45 (`banjo_d3d_records`) |
| `banjo_d3d_records_native_ab.toml` | in-run A/B target 46 (`banjo_d3d_records_native`) |
| `banjo_d3d_host_submit_ab.toml` | in-run A/B target 47 (`banjo_d3d_host_submit`) |
| `banjo_d3d_records_off.toml` | steps 4-6 off for a whole run (`banjo_d3d_records`, `banjo_d3d_host_submit`) |
| `banjo_ring_decoder_off.toml` | the ring split off for a whole run |
| `banjo_ring_decoder_core0.toml` | the decoder thread on core 0 instead of core 2 |
| `banjo_stencil_fetch_3d_probe.toml` | `masseffect_native_edram4_stencil_copy_min_tiles = 2048`: every EDRAM stencil import through the 3D engine's masked passes (exact either way); measures the late stencil fetch's copy-engine cost |
| `banjo_shaders_old.toml` | the shader-size work of 2026-10-15 off for a whole run (the five keys above and `masseffect_native_vs_pruned_outputs`); compare with a run of `renut.toml` |
| `banjo_index_persistent_ab.toml` | in-run A/B target 25 (`banjo_ring_index_persistent`) |
| `banjo_deferred_ab.toml` | deferred recording with its two companions, in-run A/B target 16 |
| `banjo_deferred_ab_core2.toml` | the same with the worker on core 2 (with the ring split's decoder) instead of 0 |
| `banjo_deferred_off.toml` | deferred recording off for a whole run (on since package 25) |
| `banjo_pacing_game.toml` | the game's own frame pacing instead of `renut.toml`'s `target_refreshRate = 0` + `frame_cap = "30"` (package 26) |
| `banjo_zcull_greater.toml` | `banjo_gpu_zcull_greater = true` (console trial) |
| `banjo_gpu_test.toml` | `banjo_gpu_zcull_greater = true` and `banjo_diag_gpu_groups = 2` |
| `banjo_gpu_groups.toml` | `banjo_diag_gpu_groups = 2` (measurement only) |
| `banjo_fragments.toml` | fragments shaded per pass category every frame (`masseffect_native_stats_pipeline`, the `Mfrag` figures of the `GPU per Swap` lines) and `banjo_diag_gpu_groups = 2`; measurement only |
| `banjo_diagnostics.toml` | every GPU measurement at once: `masseffect_native_stats_per_draw_s = 10` (one serialized frame every 10 s: GPU time, draws, fragments and ns per fragment per pixel shader), `banjo_diag_gpu_groups = 2`, `vulkan_pipeline_executable_info` + `banjo_diag_shader_stats = "all"`, `banjo_diag_vk_calls`, `banjo_cpu_profiler` (slower; for one measuring run) |
| `banjo_all_tests.toml` | `banjo_diag_gpu_groups = 2` and in-run A/B target 20 (the Clear markers) |
| `banjo_api_clear_poison.toml` | `banjo_diag_clear_padding = 1`, in-run A/B target 20 every 2 s (a PC test with a recording) |
| `banjo_api_ops_off.toml` | `banjo_d3d_api_clear`, `banjo_d3d_api_resolve` and `banjo_gpu_skip_dead_draws` off |
| `banjo_transfers_off.toml` | `banjo_gpu_dead_raster`, `banjo_gpu_rect_split`, `banjo_gpu_proof_depth_only`, `banjo_gpu_edram4_split_tiles` and `banjo_d3d_api_clear` off |
| `banjo_emulation_items_off.toml` | the EDRAM items of 2026-10-13 (the two proof keys, colour consumer, alias by shape, copy batch), `banjo_gpu_tiles_once` and `banjo_ring_vertex_persistent` off |
| `banjo_d3d_native_off.toml` | `banjo_d3d_native_draw_indexed` and `banjo_d3d_native_draw_vertices` off |
| `banjo_hot_functions_off.toml` | the 24 `banjo_cpu_native_*` keys off |
| `banjo_hand_natives_off.toml` | the four `banjo_hand_*` keys off |
| `banjo_cpu_rounds_off.toml` | `banjo_cpu_guest_cas_no_barrier`, `kernel_critsec_pcr_owner` and `thread_signal_skip_idle` off |
| `banjo_pmull_off.toml` | `banjo_ring_pmull_fingerprint` off |
| `banjo_upload_uncached.toml` | `masseffect_native_upload_memory = 0` |
| `banjo_logwriter_off.toml` | `log_file_writer = false` |

The profilers (`switch_profile_stacks`, `masseffect_gpu_marks_categories`) are on in `renut.toml` and in every config
except `banjo_cpu_verify.toml` (the user's choice: every test run brings its profile). They are plain keys outside the
master switches, so a config with a master off is profiled the same way as its partner.

## How to measure

Compare the same stretch of the game with one thing changed. Better, use an in-run A/B: with
`banjo_direct_state_ab_s = N` the enhancement that `banjo_ab_target` names turns off and on every N seconds (at a
Swap), and the `ring thread A/B` and `GPU A/B` lines give both halves from one run, under the same load and scenes.
The PC is a noisy proxy (other projects share it, and its CPU and GPU are nothing like the console's); every
enhancement is judged by what it does on the console's A57 at 1020 MHz. On the PC the picture check is a recording
of an in-run A/B: `tools/ab_flicker.py --log <run log>` finds the flips and the frame differences around them, which
must look like those anywhere else.

| `banjo_ab_target` | What it switches |
|---|---|
| 1 | `banjo_direct_state` |
| 2 | `banjo_ring_edram_memo` |
| 3 | `banjo_ring_constants_dedupe` |
| 4 | `banjo_gpu_proof_tess_leftover` |
| 5 | `banjo_gpu_edram4_color_consumer` |
| 6 | `banjo_gpu_edram4_alias_by_shape` |
| 7 | `banjo_gpu_proof_triangle` |
| 8 | `banjo_gpu_edram4_copy_batch` |
| 9 | `banjo_gpu_tiles_once` |
| 10 | targets 4 to 9 together |
| 11 | `banjo_ring_vertex_persistent` |
| 12 | `banjo_ring_shader_load_memo` |
| 13 | `banjo_ring_vertex_epoch` |
| 14 | `banjo_gpu_dead_raster` |
| 15 | `banjo_gpu_rect_split` |
| 16 | `masseffect_native_deferred_recording` (the key itself must be on) |
| 17 | `banjo_gpu_proof_depth_only` |
| 18 | `banjo_gpu_edram4_split_tiles` |
| 19 | the native Direct3D draw path (`banjo_d3d_native_draw_indexed`, `banjo_d3d_native_draw_vertices`) |
| 20 | the ring's use of the Clear markers (`banjo_d3d_api_clear`) |
| 21 | the ring's use of the Resolve markers (`banjo_d3d_api_resolve`) |
| 22 | `banjo_gpu_skip_dead_draws` |
| 23 | `banjo_ring_vertex_lock_rule` |
| 24 | `banjo_ring_texture_invalidate_range` |
| 25 | `banjo_ring_index_persistent` |
| 26 | `banjo_gpu_early_alpha` |
| 27 | `banjo_gpu_texture_signs_per_slot` |
| 28 | `banjo_gpu_edram4_clear_merge` |
| 29 | `banjo_gpu_vfetch_block` |
| 30 | `banjo_gpu_branchless_select` |
| 31 | `banjo_gpu_bool_spec` |
| 32 | `banjo_gpu_stencil_fetch_3d` |
| 33 | `banjo_gpu_edram4_lean_barriers` |
| 36 | `banjo_gpu_resolve_clear_merge` |
| 37 | `banjo_gpu_pred_select` |
| 38 | `banjo_gpu_pred_select_ps` |
| 39 | `banjo_gpu_tess_grid` |
| 40 | 29, 37, 38 and 39 together |
| 41 | `banjo_ring_core` (the ring thread pinned against the whole core mask) |
| 42 | (removed 2026-10-16: the ring prover) |
| 43 | `banjo_ring_decoder` (the ring split; off half: one item at a time) |
| 44 | `banjo_ring_vs_hash_given` |
| 45 | `banjo_d3d_records` (records of every Direct3D call's packets) |
| 46 | `banjo_d3d_records_native` (the native draw path writes its records itself) |
| 47 | `banjo_d3d_host_submit` (the kicks through a host queue) |
| 48 | (removed 2026-10-16: `banjo_ring_prover_fine`) |
| 49 | `banjo_gpu_clamp_cbuf` |
| 50 | `banjo_gpu_sign_mask_skip` |
| 51 | `banjo_gpu_vs_outputs_unwritten` |
| 52 | `banjo_gpu_bone_padding` |
| 53 | `banjo_gpu_robustness_off` |
| 54 | `banjo_gpu_edram_checks_cpu` |
| 55 | 49-54 together (package 25's shader round) |
| 56 | `banjo_gpu_literals_cbuf` (package 26) |
| 57 | `banjo_gpu_stencil_clear_partial` (package 26) |

Renut's renderer has its own in-run A/B (`banjo_renut_ab_target`, half-period `banjo_renut_ab_s`, the "[renut A/B]"
line): 1 `banjo_renut_precise_barriers`, 2 the render thread's pin to `banjo_ring_core`, 3
`banjo_renut_shared_dedupe`, 4 `banjo_renut_push_point_only`, 5 `banjo_renut_capture_pool`, and since package 27
6 `banjo_renut_exact_streams`, 7 `banjo_gpu_bool_spec`, 8 `banjo_gpu_blend_skip`, 9 `banjo_gpu_tess_grid`, and since
package 27c 10 `banjo_renut_check_worker`, and since package 28 11 `banjo_gpu_depth_only`, 12 `banjo_gpu_output_trim`,
13 `banjo_gpu_depth_words_view`.

## Every key

"Shipped" is the value in the shipped `renut.toml`, or the code's default where the toml does not set the key.
"Kind": **speed** (same output, less work), **fix** (makes the output match the console), **trial** (off until a
console run decides), **not exact** (off, and stays off), **measurement off** (the gain is turning a measurement off).

| Key | Group | Shipped | Kind | Section |
|---|---|---|---|---|
| `banjo_wait_blocking_ring` | CPU | `true` | speed | Game threads |
| `masseffect_notify_no_lock` | CPU | `true` | speed | Game threads |
| `banjo_d3d_native_draw_indexed` | CPU | `true` | speed | Game threads |
| `banjo_d3d_native_draw_vertices` | CPU | `true` | speed | Game threads |
| `banjo_no_draw_pairing` | CPU | `true` | speed | Game threads |
| `banjo_note_shader_fast` | CPU | `true` | speed | Game threads |
| `banjo_direct_state` | CPU | `false` | trial | Game threads |
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
| `masseffect_native_flat_identity` | CPU | `true` | speed | Ring thread: PM4 and shaders |
| `masseffect_native_fast_pair` | CPU | `true` | speed | Ring thread: PM4 and shaders |
| `masseffect_native_raw_microcode` | CPU | `true` | speed | Ring thread: PM4 and shaders |
| `masseffect_native_pm4_fast` | CPU | `true` | speed | Ring thread: PM4 and shaders |
| `banjo_ring_shader_load_memo` | CPU | `true` | speed | Ring thread: PM4 and shaders |
| `banjo_ring_identity_once` | CPU | `true` | speed | Ring thread: PM4 and shaders |
| `banjo_ring_registers_in_place` | CPU | `true` | speed | Ring thread: PM4 and shaders |
| `banjo_ring_wait_vblank` | CPU | `true` | speed | Ring thread: PM4 and shaders |
| `banjo_ring_estimator_reason` | CPU | `true` | speed | Ring thread: PM4 and shaders |
| `masseffect_native_upload_memory` | CPU | `1` | speed | Ring thread: vertices and indices |
| `masseffect_native_fast_copy` | CPU | `true` | speed | Ring thread: vertices and indices |
| `banjo_repack_cached` | CPU | `true` | speed | Ring thread: vertices and indices |
| `masseffect_native_fingerprint_mode` | CPU | `2` | speed | Ring thread: vertices and indices |
| `banjo_ring_pmull_fingerprint` | CPU | `true` | speed | Ring thread: vertices and indices |
| `masseffect_native_dedupe_max_fingerprint` | CPU | `16384` | speed | Ring thread: vertices and indices |
| `banjo_ring_vertex_containment` | CPU | `true` | speed | Ring thread: vertices and indices |
| `banjo_ring_vertex_epoch` | CPU | `true` | speed | Ring thread: vertices and indices |
| `banjo_ring_vertex_persistent` | CPU | `true` | speed | Ring thread: vertices and indices |
| `banjo_ring_vertex_proven_first` | CPU | `true` | speed | Ring thread: vertices and indices |
| `banjo_ring_vertex_lock_rule` | CPU | `false` | not exact | Ring thread: vertices and indices |
| `masseffect_native_indices_cache` | CPU | `true` | speed | Ring thread: vertices and indices |
| `banjo_ring_index_epoch` | CPU | `true` | speed | Ring thread: vertices and indices |
| `banjo_ring_index_persistent` | CPU | `true` | speed | Ring thread: vertices and indices |
| `banjo_ring_decoder` | CPU | `true` | speed | Ring thread: recording |
| `banjo_ring_decoder_core` | (parameter) | `2` | where | Ring thread: recording |
| `banjo_ring_vs_hash_given` | CPU | `true` | speed | Ring thread: vertices and indices |
| `banjo_d3d_records` | CPU | `true` | speed | Ring thread: recording |
| `banjo_d3d_records_native` | CPU | `true` | speed | Ring thread: recording |
| `banjo_d3d_host_submit` | CPU | `true` | speed | Ring thread: recording |
| `banjo_d3d_records_barriers` | CPU | `true` | speed | Ring thread: recording |
| `banjo_d3d_records_static` | CPU | `true` | speed | Ring thread: recording |
| `banjo_records_zero_ahead` | CPU | `true` | speed | Ring thread: recording |
| `banjo_ring_memexport_bounds` | CPU | `true` | speed | Ring thread: vertices and indices |
| `banjo_vertices_base_zero_r0` | CPU | `true` | speed | Ring thread: vertices and indices |
| `banjo_ring_constants_dedupe` | CPU | `true` | speed | Ring thread: constants, samplers, targets |
| `banjo_ring_constants_adaptive` | CPU | `true` | speed | Ring thread: constants, samplers, targets |
| `masseffect_native_compare_fast` | CPU | `true` | speed | Ring thread: constants, samplers, targets |
| `banjo_ring_sampler_fast` | CPU | `true` | speed | Ring thread: constants, samplers, targets |
| `banjo_ring_texture_invalidate_range` | CPU | `true` | speed | Ring thread: constants, samplers, targets |
| `banjo_ring_target_memo` | CPU | `true` | speed | Ring thread: constants, samplers, targets |
| `banjo_ring_edram_memo` | CPU | `true` | speed | Ring thread: constants, samplers, targets |
| `banjo_ring_bound_plan_memo` | CPU | `true` | speed | Ring thread: constants, samplers, targets |
| `masseffect_native_deferred_recording` | CPU | `true` | speed | Ring thread: recording |
| `banjo_deferred_packets` | CPU | `true` | speed | Ring thread: recording |
| `masseffect_native_diag_reuse` | CPU | `false` | measurement off | Ring thread: recording |
| `banjo_gpu_boost` | non-CPU | `true` | speed | Driver, clocks and storage |
| `banjo_mesa_disk_cache_off` | non-CPU | `true` | speed | Driver, clocks and storage |
| `masseffect_native_pipelines_prewarm` | non-CPU | `true` | speed | Driver, clocks and storage |
| `masseffect_nvk_cache_per_stage` | non-CPU | `true` | speed | Driver, clocks and storage |
| `banjo_gpu_nvk_draw_contract` | non-CPU | `true` | speed | Driver, clocks and storage |
| `banjo_gpu_zcull_greater` | non-CPU | `false` | trial | Driver, clocks and storage |
| `banjo_gpu_proof_tess_leftover` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_proof_triangle` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_proof_depth_only` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_edram4_clear_color0` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_edram4_color_consumer` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_edram4_alias_by_shape` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_edram4_copy_batch` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_edram4_clear_merge` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_stencil_fetch_3d` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_edram4_lean_barriers` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_resolve_clear_merge` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_depth1x_zcull` | non-CPU | `false` | trial | EDRAM |
| `banjo_gpu_edram4_split_tiles` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_dead_raster` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_rect_split` | non-CPU | `true` | speed | EDRAM |
| `banjo_d3d_api_clear` | non-CPU | `true` | speed | EDRAM |
| `banjo_d3d_api_resolve` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_skip_dead_draws` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_tiles_once` | non-CPU | `true` | speed | EDRAM |
| `banjo_gpu_resolve_draw` | non-CPU | `true` | speed | Command recording |
| `banjo_gpu_push_once` | non-CPU | `true` | speed | Command recording |
| `banjo_gpu_pass_narrow_end` | non-CPU | `true` | speed | Command recording |
| `banjo_gpu_blend_skip` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_structured_cf` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_early_alpha` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_texcoords_unswapped` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_no_texture_signs` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_texture_signs_per_slot` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_vfetch_block` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_branchless_select` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_pred_select` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_pred_select_ps` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_clamp_cbuf` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_sign_mask_skip` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_vs_outputs_unwritten` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_bone_padding` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_robustness_off` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_edram_checks_cpu` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_literals_cbuf` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_stencil_clear_partial` | non-CPU | `true` | speed | EDRAM |
| `banjo_renut_pipeline_cache` | non-CPU | `true` | compile time | Renut's renderer |
| `banjo_renut_precise_barriers` | non-CPU | `true` | speed | Renut's renderer |
| `banjo_renut_upload_cached` | CPU | `true` (Switch) | speed | Renut's renderer |
| `banjo_renut_pmull_hash` | CPU | `true` (ARM) | speed | Renut's renderer |
| `banjo_renut_push_point_only` | CPU | `true` | speed | Renut's renderer |
| `banjo_renut_capture_pool` | CPU | `true` | speed | Renut's renderer |
| `banjo_renut_neon_swap` | CPU | `true` (ARM) | speed | Renut's renderer |
| `banjo_renut_shared_dedupe` | CPU | `true` | speed | Renut's renderer |
| `banjo_renut_queue_notify_idle` | CPU | `true` | speed | Renut's renderer |
| `banjo_renut_linked_stages` | non-CPU | `true` | speed | Renut's renderer |
| `banjo_renut_check_worker` | CPU | `true` | speed | Renut's renderer |
| `banjo_gpu_depth_only` | non-CPU | `true` | speed | Renut's renderer |
| `banjo_gpu_output_trim` | non-CPU | `true` | speed | Renut's renderer |
| `banjo_gpu_depth_words_view` | non-CPU | `true` | speed | Renut's renderer |
| `banjo_renut_cache_save_quiet` | non-CPU | `true` | stutter | Renut's renderer |
| `banjo_gpu_tess_grid` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_bool_spec` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_bool_spec_direct` | non-CPU | `true` | compile time | Shader package |
| `banjo_gpu_spec_per_stage` | non-CPU | `true` | compile time | Shader package |
| `masseffect_native_vs_pruned_outputs` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_identity_remap` | non-CPU | `true` | speed | Shader package |
| `banjo_gpu_index_base_held` | non-CPU | `true` | speed | Shader package |
| `banjo_native_partial_resolve_inside` | non-CPU | `true` | fix | Renderer fixes |
| `banjo_native_resolved_retire_idle` | non-CPU | `true` | fix | Renderer fixes |
| `banjo_native_tfetch_filters` | non-CPU | `true` | fix | Renderer fixes |
| `banjo_native_mip_range` | non-CPU | `true` | fix | Renderer fixes |
| `banjo_native_stacked_baseless` | non-CPU | `true` | fix | Renderer fixes |

## CPU group

### Game threads

#### `banjo_wait_blocking_ring`

Direct3D's waits for the GPU (`sub_8264DCD0`, called by six loops: ring space, Swap, fences) sleep until the ring
thread writes the read pointer back or a fence, instead of spinning. The first check of each wait never sleeps (Swap
tests its condition after it).

- **Why exact:** the game reads the same values, only later.
- **Measured:** on the console three game threads spun at about 55 % of a core each before it.
- **File:** `app/src/me/me_ring_wait.cpp`.

#### `masseffect_notify_no_lock`

`NotifyRingProgress` (one call per fence write and ring segment) takes the progress mutex only when a game thread's
wait is registered as sleeping, with a sequentially consistent handshake on both sides. From masseffect-nx.

- **Why exact:** the same wake-ups; a sleeper registers before it rechecks.

#### `banjo_d3d_native_draw_indexed`, `banjo_d3d_native_draw_vertices`

`D3DDevice_DrawIndexedVertices` (`826567C8`, 79-86 % of all the PM4 the game writes) and `D3DDevice_DrawVertices`
(`8222C7A0`) as native code: the inlined state flush and the draw packets, with and without the predicated-tiling
wrapper. They write the same PM4, byte for byte, into Direct3D's own command buffer and leave the device as Direct3D
does. Direct3D's own code still does the draw-time shader load and two rare blocks; a call it cannot do plainly (over
65535 vertices, no room in the command buffer segment) goes to the original. Full account: `docs/d3d-native.md`.

- **Why exact:** byte-identical packets and device state, which the check compares.
- **Checked:** the first 64 calls of each thread, then 1 in 4096 (every call with `banjo_cpu_verify`): the native
  code and the original each run on a private copy of the device into the same scratch command buffer and are
  compared byte for byte (packets, the whole device, the tile-extents entry); one checked call in eight sends every
  block through Direct3D's own writers. A difference switches that entry point off. Verify run: 357,931 and 6,573
  calls compared, 0 differences.
- **Measured (PC, in-run A/B target 19):** 0.681 against 0.840 us per DrawIndexedVertices call (-19 %), 0.583
  against 0.627 us per DrawVertices call; the ring thread unchanged (13.79 / 13.91 ms of work a Swap). A57 estimate
  from the compiler's output: about 865 -> 500 instructions for the replaced part.
- **Off:** `banjo_d3d_native_off.toml`.

#### `banjo_no_draw_pairing`

Skips the Draw* record pairing inherited from masseffect-nx. It never pairs a draw in Banjo (its device address and
shader offsets are Mass Effect's: 0 of 10,176,920 draws paired on the PC). The ring identifies every shader from the
IM_LOADs by itself. No record per draw on the game threads, no search per draw on the ring.

- **Why exact:** the same shaders. Verify mode still runs the pairing and compares.

#### `banjo_note_shader_fast`

The draw threads' `NoteShaderObject` (twice a draw) remembers finished shader objects in one shared table of 4096
relaxed atomics instead of a 512-slot thread-local table. On the Switch every thread-local access is a call to
`__aarch64_read_tp`, and each miss took the shared mutex and a hash-set insert (4-5 % of the PC's draw threads).
From sa2-nx.

- **Why exact:** "finished" only ever becomes true; a hit returns where the locked path would.

#### `banjo_direct_state` (off)

The direct renderer, step 1 (from sa2-nx): the state packets of each Direct3D draw call become a host record that the
ring applies with the same register functions at the same place in the stream (one NOP packet instead of about 14 to
parse).

- **Why exact:** the same register writes in the same order; every record is checked at the draw and again by the
  ring (the first 4096 / 2048, all of them with `banjo_cpu_verify`).
- **Why off:** the ring thread was slower with it in both console A/Bs (the second: 47.2 against 45.0 us per draw),
  and 4 % slower on the PC.
- **A/B:** `banjo_direct_ab.toml` (target 1).

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
second, sooner at 64 KB or on a warning. From futari-nx, whose console run had every frame over 100 ms while the ring
thread wrote its 10 s report into the SD card.

- **Why exact:** the same lines, written up to a second later. Past `log_file_writer_buffer_kb` (4 MB) lines are
  dropped and counted, never waited for; a crash or an exit still writes everything.
- **Off:** `banjo_logwriter_off.toml`.

### Ring thread: PM4 and shaders

#### masseffect-nx's ring fast paths: `masseffect_native_flat_identity`, `masseffect_native_fast_pair`, `masseffect_native_raw_microcode`, `masseffect_native_pm4_fast`

From masseffect-nx's measured best profile; off in the code, on in `renut.toml` (and switched on when the toml lacks
them, see above).

- `masseffect_native_flat_identity`: shader identity checks (VS fetch-patch match, PS microcode match) memoised in
  small direct-mapped tables keyed by (library entry, load generation) instead of a map and a word-by-word compare.
- `masseffect_native_fast_pair`: the draw/record pairing without cold cache lines and locks. (With
  `banjo_no_draw_pairing` Banjo does not pair draws at all.)
- `masseffect_native_raw_microcode`: an IM_LOAD of the same address and size as the stage's last load compares the
  guest words with a raw copy of that load (one `memcmp`) before byte-swapping anything.
- `masseffect_native_pm4_fast`: runs of constant and fetch register writes are byte-swapped, compared and stored four
  words at a time with NEON.
- **Checked:** each compares itself with the regular path for the first `masseffect_native_verify_n` uses (2048;
  every use with `banjo_cpu_verify`), logs DIFFERENCE and falls back if they ever disagree.

#### `banjo_ring_shader_load_memo`

Direct3D loads a shader program (IM_LOAD) before most draws, about a thousand times a frame with another program than
the stage's last. Each load was copied three times and hashed to find its entry. A memo by load address (4,096 slots)
keeps the words, their hash, the entry and the identity generation; a load from that address with the same raw words
(one `memcmp`) takes them from the memo.

- **Checked:** against the content memo on the first 4096 uses (all in verify mode).
- **Measured (PC):** in-run A/B target 12: the ring's PM4 and identification part 2.41 -> 2.09 us per draw.

#### `banjo_ring_identity_once`

A draw asked up to four times whether its vertex shader entry matches the loaded program (`MatchesVertexShaderIdentity`,
2.3 % of the console's ring thread). It is asked once; when it holds, the two blocks that could only change the entry
when it does not are skipped.

- **Why exact:** the same answers; when it does not hold, everything runs as before.

#### `banjo_ring_registers_in_place`

A proven rectangle list changes the two registers its overwrite proof needs in the ring's register file and puts them
back, instead of copying the whole register file for every rectangle list.

- **Why exact:** the same proof; nothing else in it reads those two registers.

#### `banjo_ring_wait_vblank`

The ring's WAIT_REG_MEM loop polled every 50 us. Its one long wait (memory `1FB04006`, the frame pacing word, about
13 ms a frame on the PC) cost about 260 kernel sleeps and wake-ups a frame on the Switch. After 16 polls it now
sleeps until the next vblank interrupt or submission wakes it, or 250 us.

- **Why exact:** timing only; the same word is read until it matches.
- **Measured (PC):** 24 million polls per 10 s -> about 6,000.

#### `banjo_ring_estimator_reason`

The draw-extent estimator keeps its reject reason as a pointer to a literal instead of a `std::string` (a heap
allocation per call). From sa2-nx.

- **Why exact:** diagnostics text only.

### Ring thread: vertices and indices

#### `masseffect_native_upload_memory = 1`

The upload buffer (every draw's vertices and indices) in CPU-cached memory, published with `vkFlushMappedMemoryRanges`
before each submit. sa2-nx's console wrote 5,251 MB/s there against 2,506 in the uncached type. Switch only: on the
PC's discrete GPU the cached type is system memory and the ring got 2.5-3 times slower per draw, so PC test tomls
keep 0.

- **Why exact:** the same bytes reach the GPU.
- **A/B:** `banjo_upload_uncached.toml`.

#### `masseffect_native_fast_copy`

The vertex copy into the upload buffer moves 64 bytes per iteration with the guest source prefetched ahead (the guest
data is cold: written frames ago by another core). From masseffect-nx.

- **Checked:** against the plain loop for the first `masseffect_native_fast_copy_verify` copies (256).

#### `banjo_repack_cached`

k_10_11_11 / k_11_11_10 vertex attributes are converted in a cached scratch copy and written to the upload buffer
once, instead of converted in place (with the uncached upload buffer every word was read back from it).

- **Why exact:** the same bytes.

#### `masseffect_native_fingerprint_mode = 2`, `banjo_ring_pmull_fingerprint`

The content fingerprints of vertex windows, index ranges and sampled texture blocks. Mode 0 is XXH3 seeded per block,
1 unseeded XXH3 with a bijective mix (what the enhancement table sets when the toml does not), 2 the A57's hardware
CRC32 pair (`masseffect_crc_fingerprint.h`, sa2-nx round 6; the PC has no CRC instructions and keeps XXH3).
`banjo_ring_pmull_fingerprint` replaces the CRC32 pair, where mode 2 uses it and for the texture recheck's
whole-content hash, by a 64-bit CRC (ECMA-182 polynomial) folded with PMULL carry-less multiplies
(`banjo_pmull_fingerprint.h`): four chains folded by x^512, a Barrett reduction, the seed in the first block and the
length in a bijective final mix. Its main loop is 8 PMULL, 2 LDP and 8 EOR per 64 bytes, about 8 cycles against the
CRC32 pair's 16 (all CRC instructions issue in the A57's single M pipeline).

- **Why exact:** the fingerprints only decide reuse. A change spanning fewer than 64 bits of a block is always seen;
  other changes escape with probability 2^-64 (both the CRC32 pair and the CRC-64).
- **Checked:** `sdk/tests/pmull_fingerprint_test.cpp` through simde's model of the intrinsics: 2,811 lengths and seeds
  against the bit-by-bit reference, every one of 28,192 single- and short two-bit changes seen. On the console the
  first 256 calls and then 1 in 65536 (1 in 64 with `banjo_cpu_verify`) are recomputed by the reference; a difference
  returns to the CRC32 pair for the session.
- **Measured:** the console's ring thread spent 8.8 % of its samples in `RangeFingerprint` (sixteenth package, intro).
  The PMULL form has not run on a console yet.
- **Off:** `banjo_pmull_off.toml` (fingerprints are compared across draws, so the key cannot switch inside a run).

#### `masseffect_native_dedupe_max_fingerprint = 16384`

The same-frame vertex dedupe matches ranges larger than 16 KB by address, size and byte order alone, without the
content fingerprint (static meshes; the transient buffers that need the check are small). With
`banjo_ring_vertex_epoch` every size is matched by address inside a synchronization interval, so this bound now
matters only for the command buffer pages.

#### `banjo_ring_vertex_containment`

A draw's vertex window that lies inside a larger copy of the same vertex buffer already in this upload buffer reuses it
at an offset. From sasrt-nx.

- **Why exact:** the dedupe's rules (forgotten at every guest synchronization and every upload buffer, never for the
  command buffer pages).
- **Checked:** the first 4096 reuses (all in verify mode) compared word by word with guest memory: 17,878 checks, 0
  differences.
- **Measured (PC):** about 10-30 % of the vertex bytes.

#### `banjo_ring_vertex_epoch`

Inside a synchronization interval a vertex window of any size is matched to its earlier copy by address (as the
windows above 16 KB, their containment and the index cache already were); the small windows were fingerprinted
(every 8th block) on every draw. Ranges in Direct3D's command buffer pages keep their fingerprint.

- **Why exact:** the game cannot rewrite memory a draw already referenced before it learns that the GPU is done
  with it, and every point where it can learn that ends the interval (`banjo_ring_sync_fences`).
- **Checked:** the first 4096 small reuses (all in verify mode) compared with guest memory.

#### `banjo_ring_vertex_persistent` (+ `banjo_ring_vertex_persistent_submitted`, `banjo_ring_vertex_persistent_mb`)

A vertex window whose bytes were the same in three frames keeps its converted copy in a persistent buffer, the arena
(64 MB). A later draw of it binds the arena and copies nothing; the guest's bytes are fingerprinted whole once per
synchronization interval instead of copied. `banjo_ring_vertex_persistent_submitted` keeps a block proven past a
synchronization point for the draws the game had already submitted then (their vertices are final until the GPU has
run them). Details: `docs/enhancements.md`, "Static vertex buffers kept across frames".

- **Why exact:** a block is bound only while the fingerprint of all its guest bytes is the one it was made from.
- **Checked:** the first 4096 uses and one in 4096 after are compared byte by byte (every use with
  `banjo_cpu_verify`: 559,334 uses, 0 differences); a difference turns it off for the session.
  `banjo_diag_vertex_persistent_check` fingerprints again every use proven without reading: 2,235,916 uses, 0
  differences.
- **Measured:** PC gameplay: vertex bytes copied 14 -> 2.1 MB a frame. Console (second run): no gain on its own, the
  A57 fingerprints about as fast as it copies; it stays on because it is not slower and keeps the bytes out of the
  upload buffer, and `banjo_ring_index_persistent` and `banjo_ring_vertex_proven_first` build on it.
- **A/B:** `banjo_vertex_persistent_ab.toml` (target 11).

#### `banjo_ring_vertex_proven_first`

A vertex window already proven in the arena for this synchronization interval is bound from it before the upload
buffer's two reuse tables are searched (lookups that cannot find it).

- **Why exact:** the same binding.

#### `banjo_ring_vertex_lock_rule` (off, not exact)

A persistent block whose memory pages no Direct3D vertex or index buffer Lock has touched since its last fingerprint
would keep that fingerprint without reading its bytes.

- **Why off:** loading writes vertex memory without a Lock (file reads, decompression): 29 s into a PC run a 640 KB
  block changed with no Lock of its pages, and the persistent path's own check caught it. Direct3D's object versions
  are not a proof for this game's vertex memory. The key stays for the record (A/B target 23).

#### `masseffect_native_indices_cache`

Same-frame index cache (16-bit fast path): a draw that repeats an earlier draw's index range in this upload buffer
reuses its converted copy and its minimum and maximum. From masseffect-nx (the x5 per-light redraws there).

#### `banjo_ring_index_epoch`

The index cache hashed every range up to 16 KB (XXH3, about 36 % of the ring's hashing on the PC); above 16 KB it
already matched by address. Now every size matches by address within a synchronization interval.

- **Why exact:** the same rule as `banjo_ring_vertex_epoch`.
- **Checked:** the first 4096 hits (all in verify mode) hash and compare: 382,754 checks, 0 differences.

#### The ring prover (removed 2026-10-16)

`banjo_ring_prover`, `banjo_ring_prover_loads`, `banjo_ring_prover_fine` and `banjo_ring_prover_core` (A/B targets 42
and 48) are gone, with `app/src/me/masseffect/banjo_ring_prover.h`. Package 24b's console runs: the prover thread was
busy 41-43 % of a core (93 % of the windows it was asked about were already covered, half its time CRC), the CPU as a
whole at 227-280 % of 300 %; the run with it off had the CPU at 202 %, the same fps (17.8) and the executor at the
same cost per draw (24.6 against 25.0-26.1 us with it). The ring fingerprints the persistent windows itself again and
compares shader loads by their words. See `docs/d3d-full-replacement.md`, "The prover removed".

#### `banjo_ring_decoder` (+ `banjo_ring_decoder_core`)

The ring split (`app/src/me/banjo_ring_split.h`, step 3 of `docs/d3d-full-replacement.md`). The ring thread did the
PM4 work and the renderer's work one after the other; a decoder thread (core 2 by `banjo_ring_decoder_core`, the
ring's priority) now parses the stream, keeps the register file, identifies the shaders, does the tile passes'
bookkeeping and runs WAIT_REG_MEM, and the ring thread (the executor: it is the guest thread interrupts must run on)
gets the draws through a queue. Each item is preceded by the register pages (64 registers) the decoder wrote since the
item before, so the executor's register file, which the renderer reads, is the decoder's at that point of the stream.
What the game can see stays the executor's, in stream order: fence and memory writes (the fence counter read when it
is written), the tiling extents, the scratch write-back, interrupts, the read pointer and the GPU-wait notifications.
Copies, the Swap, the tile check and the reports run on the executor while the decoder waits for them (the tile passes
use their answers); a COND_WRITE and an occlusion query too (they read memory the executor may still be about to
write). The vertex program of each draw goes in a kept copy (the stage's own vectors change with the next load). The
MMIO reads of the game see the executor's file. Not with a traced frame; with `banjo_direct_state` the decoder applies the records.

- **Why exact:** the renderer sees the same register values, programs and stream positions at every draw; the game
  learns of GPU progress only after the draws before it have been executed.
- **Checked:** every 1024th draw (every 16th with `banjo_cpu_verify`) carries the decoder's whole register file and the
  executor compares it with its own; a difference is reported and every later draw carries the whole file (slow, but
  right). The kept vertex programs are compared with the stage's program every 1024th draw (every draw in verify).
- **Measured:** the "ring split" report line gives the executor's and the decoder's time per Swap, the decoder's wait
  for the executor's answers, items, register pages and calls per Swap, and the file checks.
- **A/B:** `configs/banjo_ring_decoder_ab.toml` (target 43): the off half makes the decoder wait for every draw, which
  is one thread's order of work with the hand-over's cost on top.

#### `banjo_d3d_records`, `banjo_d3d_records_native`, `banjo_d3d_host_submit`, `banjo_d3d_records_barriers`, `banjo_d3d_records_static` (steps 4-6b)

The decoder without PM4 (`app/src/me/banjo_records.h`, `docs/d3d-full-replacement.md` steps 4-6). Every Direct3D
call that writes into a command buffer has its packets made into host records right after it returns, on the thread
that made the call (the outermost hooked call only: a counter in the guest context); the command buffer keeps one
marker NOP per record, covering the packets, so every address Direct3D keeps stays what it was. A call is cut into
pieces at the indirect buffers and waits, which stay PM4 unless the records carry them. The decoder applies a
record with the same register functions and the same `Type3` the packets went through, at the same place in the
stream (tile passes and dead-raster tracking included). The native draw path writes its records itself
(`banjo_d3d_records_native`). Direct3D's kick hands its segments over through a host queue
(`banjo_d3d_host_submit`), with Direct3D's own packets in the primary ring as operations. Records also carry the
indirect buffers and waits (`banjo_d3d_records_barriers`), and Direct3D's
fixed buffers become records of their own after their second identical run (`banjo_d3d_records_static`). Records
live in the ring of records (the direct state's), with its side store for command buffers run again a frame or more
later (those are parsed: the packets stay under the marker).

- **Why exact:** a record is the packets it replaces, re-encoded and compared; the decoder runs them through the same
  code, in the same order, with the same packet addresses; the tile masks Direct3D patches after a call are read from
  the command buffer when the record is applied.
- **Checked:** at the call (`SamePiece`) and at the decoder (decoded again from the command buffer as it is when
  reached) for the first 4096 records, all in verify mode; the kick queue against the ring's words likewise.
- **Measured:** "Direct3D records" (calls, records, the words left as PM4 and why, the PM4 still parsed), "PM4 parsed
  outside records" (by where and opcode), "host submissions".
- **A/B:** `configs/banjo_d3d_records_ab.toml` (45), `banjo_d3d_records_native_ab.toml` (46),
  `banjo_d3d_host_submit_ab.toml` (47); `banjo_d3d_records_off.toml` for a whole run without.

#### `banjo_ring_index_persistent`

16-bit index ranges keep their converted copy across frames in the vertex arena, under the vertex blocks' proof; the
arena is bound as the index buffer. The console converted and copied about 9 MB of indices a frame.

- **Checked:** the first 4096 arena uses (all with `banjo_cpu_verify`) are converted again and compared: 5,967,140
  uses, 0 differences. Both the index and the vertex stage call `PersistSync()` first (the first draw after a wait
  could otherwise take an index block as proven by the interval that had just ended; found by the sasrt-nx port).
- **Measured (PC):** 146,000 draws per 10 s from the arena, 798 MB per 10 s neither converted nor copied.

#### `banjo_ring_memexport_bounds`

`MemexportRedirect` (every vertex stream of every draw) walked every memexport mirror whenever a stream fell inside the
mirrors' overall bounds (2.3 % of the PC ring's work). A sorted index of the live mirrors and their largest size
answers "does any mirror overlap this stream?" from the few that can reach it.

- **Why exact:** a mirror's base and size never change after it is made; verify mode runs the walk whenever the index
  says "none" (0 differences on the route with the water's memexport streams).

#### `banjo_vertices_base_zero_r0`

Draws whose vertex shader reads r0 (all of Banjo's) also take the base-zero vertex binding (the upload buffer bound
once, the draw shifted by firstVertex / vertexOffset), with the vertex index base compensated so r0 is unchanged.
From sa2-nx.

- **Measured (PC):** `vkCmdBindVertexBuffers` per draw 0.99 -> 0.05 (on NVK 5 words and a macro each, about 1 us on
  the A57).

### Ring thread: constants, samplers, targets

#### `banjo_ring_constants_dedupe`, `banjo_ring_constants_adaptive`

A new shader-constant generation whose bytes equal the block uploaded last keeps that block bound (no upload, no new
set-4 offset). From sa2-nx. `banjo_ring_constants_adaptive` stops comparing (and copying the shadow) for a shader stage
whose last 1024 compares found fewer than 16 equal blocks, for that stage's next 16384 uploads, then looks again.

- **Why exact:** an exact byte compare against a CPU copy of the last upload; the adaptive key only stops trying.
- **Checked:** verify mode reads the uploaded block back: 39,581 checks, 0 differences.
- **Measured (console):** the compare found 416 equal blocks in 51,649 vertex shader uploads and 0 in 75,221 pixel
  shader uploads, which is why the adaptive key exists.

#### `masseffect_native_compare_fast`

The per-draw state caches (sampler and fetch constants, pass key, shared block, push constants, viewport, pipeline
keys) compare their 8-130 byte keys inline instead of through a libc `memcmp` call (about 15 a draw). From
masseffect-nx.

#### `banjo_ring_sampler_fast`

A hit of the per-register sampler cache compares the three fetch-constant words inline and writes 1/size from the
entry instead of dividing on every draw.

- **Why exact:** the same words in the shared constants.

#### `banjo_ring_texture_invalidate_range`

A resolve that changes the texture an address is sampled from clears only the sampler cache entries whose texture
memory (base level and mips, at 16 bytes a texel on 32x32 tiles: never smaller than the real footprint) can overlap
the resolved range, instead of the whole cache. 36,003 of 37,052 cache misses per 10 s were such whole-cache clears.

- **Checked:** verify mode prepares every entry the whole-cache rule would have cleared and compares: 1,932,793
  entries, 0 differences; a difference switches the key off.
- **Measured (PC):** cache misses 33 % -> 11-14 %.

#### `banjo_ring_target_memo`

`GetTarget` / `GetDepth` (every slot of every draw) answered from a 4-slot memo of their inputs. From sa2-nx.

- **Why exact:** the maps never drop an entry, so the same inputs give the same image; verify mode runs the lookup on
  every hit (608,613 checks, 0 differences).

#### `banjo_ring_edram_memo`

A draw whose render-target registers equal the previous draw's reuses that draw's EDRAM preparation, only when the
full preparation would do no work (same images, their tiles owned in the current epoch, no deferred stencil
pending). From sa2-nx.

- **Checked:** the first 4096 hits (every hit with `banjo_cpu_verify`) run the full preparation too and compare the
  images, area, plan and stencil state.
- **Measured:** 92-93 % of the draws are prepared this way.
- **A/B:** `banjo_memo_ab.toml` (target 2).

#### `banjo_ring_bound_plan_memo`

`PlanEdramBoundRanges` (a pure function of its request) is reused when the request equals the previous draw's (PC:
about 80,000 reuses per 10 s). From sa2-nx.

- **Checked:** verify mode recomputes and compares (0 differences).

### Ring thread: recording

#### `masseffect_native_deferred_recording` (on since package 25, the worker on core 0)

The ring queues its Vulkan calls and a worker thread on another core records them, in the same order into the same
command buffers. Its companions `masseffect_deferred_native_fast` (batched publishing) and
`masseffect_deferred_native_update` (descriptor updates queued too) are set with it by the two configs.

- **On since package 25** (2026-10-16), with both companions and `banjo_deferred_core = 0`, after package 24b's
  console runs (the in-run A/B -10 to -12 % of the ring's time per draw). Package 24 and before had it off, because
  on the PC it makes the ring slower (27.1 against 12.9 ms of work a Swap in the same run): the ring
  waits for the worker at every frame's `vkEndCommandBuffer` (606 times per 10 s, 1.74 s in all) and at pool resets.
  futari-nx measured 16-18 -> 5-6 us per draw on its console; the user's console test of Banjo (whole run) was
  "slightly worse". The picture is the same on the PC (in-run A/B).
- **A/B:** `banjo_deferred_ab.toml` (target 16), `banjo_deferred_ab_core2.toml`; off for a whole run:
  `banjo_deferred_off.toml`.

#### `masseffect_native_diag_reuse = false`

Turns off a measurement: content fingerprints of every new texture on the ring thread. Changes nothing else.

## Non-CPU group

### Driver, clocks and storage

#### `banjo_gpu_boost`

In handheld mode asks for the official 0x92220008 profile (GPU 460.8 MHz, memory 1331.2 MHz, the one
UnleashedRecomp-NX, MarathonRecomp-NX and masseffect-nx use) through the SDK's `switch_apm.cpp`, which checks the real
clocks for 1.5 s and reverts if the memory clock moved. Docked, nothing is requested. From sa2-nx.

- **Why exact:** a clock, not a setting.
- **Risk:** more power draw in handheld mode.

#### `banjo_mesa_disk_cache_off`

`MESA_SHADER_CACHE_DISABLE=true` before the Vulkan device exists. The renderer's own pipeline cache file
(`cache/masseffect_native_pipelines.bin`) already keeps the compiled pipelines, and Mesa's SD-card writer held the
cache lock that the ring thread waited on during compiles.

#### `masseffect_native_pipelines_prewarm` (+ `banjo_prewarm_threads`)

At start-up, threads at the lowest priority re-create in the Vulkan cache the pipelines of the list in
`cache/masseffect_native_pipelines.bin` (the ones the ring created in earlier sessions, or the first-boot list made on
the PC by `tools/make_prewarm_list.py`) and destroy them: when the ring asks for them they are already compiled (no
70-160 ms hitch per pipeline). `banjo_prewarm_threads` (1) sets how many threads; more mostly queue on the console's
global malloc lock. `banjo_prewarm_threads_rebuild` applies instead when the session starts without the file's cache (none yet, another
package, or over the size limit), when the whole list compiles for real: 7-10 minutes on one thread. It was 2 for one
package (2026-10-15) and is 1 again: on the console the lowest priority the kernel accepts for this process is 0x3B,
the game threads' own, so a second thread took a core from the game instead of idle time.

`banjo_ring_core` (1; -1 = off, 2026-10-16) pins the ring thread to one core. The diagnostics run of package 21b had
the ring at 99.3 % of one thread (69 ms of work per 72 ms frame: the frame-rate limit) with the process's whole core
mask (0x7, no preferred core): Horizon moved it between the three cores, so the user's overlay never showed a core at
99 % and each move cost the ring its L1 contents. Core 1 is the ring's: the main (UI) thread is pinned to core 0, the
background compile threads to core 2. In-run A/B target 41 switches between the pin and the whole mask
(`RexSwitchUnpinCurrentThread`), and the ring report line says which is in force and how many core changes it saw.
`banjo_deferred_packets` (true, 2026-10-16): with deferred recording on, the draw path's per-draw commands go into the
queue as packed records (`masseffect_deferred_recording.h`, `Packet*`): one open chunk of the arena takes many draws and
becomes one queue slot when anything else is queued (which closes it first, so the order is kept), when it is full or
when the ring publishes. Before, each of a draw's ~15 Vulkan calls was its own slot (a tuple, an arena copy and a
publish test), and the PC A/B of the layer had the ring at 9.86 us a draw against 4.96 off. With the packets: 8.37
against 4.01, the recording part of it 3.06 against 3.17; what remains on the PC is the wait for the worker at the
frame's end (the PC's driver calls are cheap, so its worker is the slower thread). On the console the driver calls are
about 8.4 us of the ring's 26 a draw, which is what the layer moves to the worker core. The arena is 1 MB (was 512 KB).
`banjo_diag_vk_calls` does not count the packed calls.

`banjo_deferred_core` (2) pins `masseffect_native_deferred_recording`'s worker: it was created on the process's ideal
core with that core alone as its mask (libnx `threadCreate` with -2: core 0, the UI thread's), which is how the
console trial of 2026-10-14 ran.

`banjo_background_compile_core` (2; -1 = off, 2026-10-16) pins every background compile thread (the prewarm, the
light-specialisation compiles, the diagnostics' shader statistics copies, the shader preload) to one core, so together
they take at most one of the three cores from the game. The statistics copies (`banjo_diag_shader_stats`, uncached: one
full compile each) also wait for the prewarm to finish and rest three times as long as each took. Package 20d's
diagnostics run had them at 80 % of a core for the whole session, next to two prewarm threads in a cache rebuild.

- **Why exact:** the prewarmed pipelines are destroyed unused.

#### `banjo_pipeline_cache_max_mb` (128)

The driver's pipeline cache keeps every entry it ever loaded, so each new shader package left the previous one's
compiled shaders in the file. By 2026-10-15 the console's file held 258 MB, about three times what the pipelines in use
need. The driver kept all of it in memory, every save after the first failed for lack of memory (so the same
pipelines were compiled again in every session), and once the file passed 256 MB it was not read at all, prewarm list
included. Since then the file (version 2) carries a tag of the shader package (its size and header checksum), and it
is read by parts:

- the prewarm list is always kept;
- the cache is loaded only if it was built with the package in use and is at most this many MB;
- otherwise the session starts with an empty cache; the prewarm compiles the list into it again, and the first save
  replaces the file.

A version 1 file is loaded if it fits, and its first save adds the tag. The log says when and why a cache was left out
(`C6: the file's ... KB of pipeline cache were left out because ...`).

- **Why exact:** it only decides which compiled pipelines are reused; every pipeline is created from the same keys.

#### `masseffect_nvk_cache_per_stage`

Per-stage shader cache keys in the driver (its run-time `disable_lto`), so a vertex or pixel shader already compiled
for another pipeline is not compiled again. Needs the patched driver of the Mesa tree the app links.

#### `banjo_gpu_nvk_draw_contract`

NVK's per-draw contract is also looked up under the name the linked Mesa tree exports (`nvk_switch_dibujo`). The app
declared only `nvk_switch_draw`, which that tree does not export, so the four per-draw paths the
`masseffect_native_nvk_*` keys ask for (draw emission in one go, only the cbuf slots and dynamic state groups that
can be dirty, prefetch hints) were never requested in any Banjo build before 2026-10-13. No driver change: the paths
are in the driver already and sa2-nx ships with them. From sa2-nx.

- **Why exact:** each path checks itself inside NVK and its guard switches it off on a difference (logged as
  MISMATCH). On the console look for `C6 NVK per draw: improvements requested`.

#### `banjo_gpu_zcull_greater` (off, console trial)

Asks the driver for hierarchical depth culling in the GREATER direction (its own option, `NVK_ZCULL=greater`).
Banjo's scene is drawn with reversed depth (GREATER_EQUAL, cleared to 0), so the driver's default direction gives the
scene no culling.

- **Why it should be exact:** ZCULL only rejects pixels the depth test would reject.
- **Status:** one console run with it showed no change in the scene's time; it stays off. Configs:
  `banjo_zcull_greater.toml`, `banjo_gpu_test.toml`.

### EDRAM

The Xbox 360 renders into 10 MB of EDRAM that several surfaces share; the renderer keeps host images per view and
moves tiles between them when a surface takes memory another one held. Each key below removes transfers the game does
not rely on. Each was checked on the PC with 0 `DIFFERENCE`, and those with an A/B target also with a recorded in-run
A/B whose flips do not stand out.

#### `banjo_gpu_proof_tess_leftover`, `banjo_gpu_proof_triangle`, `banjo_gpu_proof_depth_only`

The overwrite proof (`ProveFullOverwrite`) lets a draw take tiles over without importing what they held when it
provably overwrites all of them.

- `banjo_gpu_proof_tess_leftover`: the tessellation register counts only in the explicit major mode, as the
  console and the reference renderer read it. The game leaves it set after its water, which refused 16 clears a frame.
- `banjo_gpu_proof_triangle`: one auto-indexed triangle that covers the whole viewport (or scissor, with clipping off)
  is a full overwrite, like a rectangle: the blur chain and the composite. Exact edge test in double precision.
- `banjo_gpu_proof_depth_only`: in the depth-only EDRAM mode no pixel shader runs, so the alpha test and
  alpha-to-coverage bits left on from earlier draws cannot stop a sample being written. The loading screen's depth
  clear moved the whole depth buffer to another representation and back every frame (console: 2,880 tiles and 16.2
  ms of GPU time a Swap).
- **Measured (PC gameplay, the first two with `banjo_gpu_edram4_color_consumer`):** EDRAM tiles moved per 10 s
  1,394,632 -> 311,400; nine-pass imports 546,600 -> 4,800. Depth-only: the loading transitions' representation
  switches 49,272 / 13,920 / 72,416 / 134,888 -> 12,144 / 4,664 / 14,752 / 15,240 tiles per 10 s.

#### `banjo_gpu_edram4_clear_color0`, `banjo_gpu_edram4_color_consumer`, `banjo_gpu_edram4_alias_by_shape`, `banjo_gpu_edram4_copy_batch`

Direct3D clears through a 4x alias of the target ("the 4x clear view").

- `banjo_gpu_edram4_clear_color0` (from sa2-nx): a depth + colour 0 clear through the alias is cleared in the 1x
  owners of both instead of drawn and transferred 4x -> 1x. Values near an 8-bit rounding tie are declined.
- `banjo_gpu_edram4_color_consumer`: the colour half goes straight into the view that pulled those tiles the last two
  times (768 tiles a frame before).
- `banjo_gpu_edram4_alias_by_shape`: a depth clear with no learned 1x target finds it by shape (same base, format and
  tile pitch). The first shadow map's clear view was never matched: 768 tiles converted every frame.
- `banjo_gpu_edram4_copy_batch`: consecutive one-tile copies between the same two views become one `vkCmdCopyImage`
  with all their regions (48 copies a frame, each between its own barriers).
- **Why exact:** the same pixels get the same values; a clear whose consumer turns out different is transferred as
  before.

#### `banjo_gpu_edram4_clear_merge`

A redirected depth clear (`masseffect_native_edram4_redirected_clear`) is cleared in the views that own its tiles,
one run of tiles at a time. Each run had a full barrier (MEMORY_READ / WRITE, ALL_COMMANDS: a cache flush and a wait
for idle on NVK), a CLEAR pass and another full barrier: 393 of the level's 427 barriers per frame and ~180 of its 258
passes on the console (package 19's `banjo_diag_vk_calls`). With the key, a redirected clear records one barrier
before and one after all of its clears, and one LOAD pass per owner image that clears every rectangle of that owner
with one `vkCmdClearAttachments` (rows of tiles with the same columns merged into one rectangle).

- **Why exact:** the same rectangles get the same values. NVK clears a CLEAR pass's render area with the same clear it
  records for `vkCmdClearAttachments`; the rest of the LOAD pass's box is loaded and stored unchanged. The owners are
  different images or disjoint rectangles of one, so the clears need no barrier between them. A clear that needs work
  first (an owner to prepare or restore, a partial stencil to fetch) takes the old path, counted in the report.
- **Measured (PC, console `renut.toml`, intro route):** barriers 404 -> 52 a frame (full flushes 399 -> 47), render
  passes 241 -> 65; ~1,500 redirected clears per 10 s, each ~36 runs of tiles, now one pass and one rectangle each,
  none on the old path. To be measured on the console.
- **A/B:** target 28.

#### `banjo_gpu_stencil_fetch_3d` (EDRAM removal step 1a, 2026-10-16)

The lazy stencil's late fetch (a stencil-using draw needs the stencil a depth-only import left behind) went through
the copy engine when it was 32 tiles or more: an empty import render pass, a compute dispatch to a buffer, a copy-engine
copy and a full barrier, three engine switches. On the console that was the one steady stencil import a frame (96
tiles), `edram_import9` 1.3 ms a frame at stock. With the key it takes the 3D engine's masked passes: a stencil clear
of the tile rows, then one pass per bit ever set in the source (none when no bit was set: a clear only).

- **Why exact:** the masked passes are the original import path; each sets bit k where the source's bit k is set,
  after a clear to 0, so they write the bytes the copy did.
- **Tried and dropped:** fetching only the tiles of the draw's raster area. The records it left were inherited again by
  depth-only draws and fetched piece by piece every frame (6 small imports a frame on the PC where there had been none).
- **Measured (PC, console toml, intro route):** `copy-engine stencil imports 0` in steady state; GPU A/B -0.2 ms of
  busy time a frame (the PC pays little for engine switches; the console's figure is the one to read).
- **A/B:** target 32 (`banjo_stencil_fetch_3d_ab.toml`). Recording compared at every flip: the same picture.

#### `banjo_gpu_edram4_lean_barriers` (EDRAM removal steps 2 and 3, 2026-10-16)

Every EDRAM clear pass (redirected and resolve clears, the tiles-once spare-row clear, the colour-0 halves, stencil
clears: 22 call sites) had a full barrier before and after (ALL_COMMANDS, MEMORY_READ / WRITE: a wait for idle and a
flush of every cache on NVK). With the key they are left out; only a pending batch of compute transfers gets its own
barrier first. The scene depth's resolve (`guestspace depth resolve`, 6 a frame) gets barriers that name what it reads
and writes instead of all commands and all memory.

- **Why exact:** each clear's render pass (`PassFor` / `CreatePass`) orders it: its begin waits for all earlier commands
  and makes earlier attachment and copy writes visible, its end makes the clear visible to shaders, copies and later
  passes. Compute writes are outside the pass's scope, so a pending compute batch is closed with its barrier first
  (standalone dispatches end with their own). The resolve's narrowed barriers cover every stage that writes the depth
  or reads / writes the R32F texture (fragment tests, colour output, transfer, compute, vertex and fragment shaders).
- **Measured (PC):** in the level, 27 barriers a frame, 10 of them full flushes; about 119 barriers a frame left out.
  GPU A/B: busy GPU 18.5 -> 16.9 ms a frame (-1.6 ms, intro window). The console pays 5-13x more per barrier.
- **A/B:** target 33 (`banjo_lean_barriers_ab.toml`): the same picture at every flip.
- Also from step 3: the depth resolve's GPU time is counted as `copies` now, not `edram_alias`: it is a resolve the
  game needs, so the EDRAM categories show only what is left of the emulation.

#### `banjo_gpu_resolve_clear_merge` (EDRAM removal step 5, 2026-10-16)

A resolve's clear through the EDRAM range plan recorded one CLEAR pass per rectangle (up to 3). With the key, a colour
clear records all of its rectangles in one LOAD pass with one `vkCmdClearAttachments` (the clear merge's form), and so
does a depth clear of an image without a ZCULL plane (TRANSFER_DST). Depth images with a plane keep one
`loadOp = CLEAR` pass per rectangle: NVK resets ZCULL only for those, and reloading a stale plane can kill the GPU
context (UnleashedRecomp's GPU loss with depth-clear changes, its `docs/SWITCH-PERFORMANCE-AUDIT.md`).

- **Why exact:** the same rectangles get the same values (`banjo_gpu_edram4_clear_merge`'s argument).
- **Measured (PC):** about 320 resolve clears merged per 10 s in the intro (one pass saved each); GPU A/B -0.3 ms busy.
- **A/B:** target 36 (`banjo_resolve_clear_merge_ab.toml`): the same picture at every flip.

#### `banjo_gpu_depth1x_zcull` (EDRAM removal step 1b, 2026-10-16; off)

1x depth views are created without TRANSFER_DST, so the driver can give the shadow maps a ZCULL plane (they were
excluded only for the copy engine's stencil imports, which step 1a no longer uses in steady state). **Off by
default**, with no config that turns it on: ZCULL's correctness can only be checked on the console, owners with a
plane lose the clear merge (they need CLEAR passes), and UnleashedRecomp lost the GPU context after 15-20 minutes with
changes to how depth buffers are cleared while ZCULL planes were in use, for a small gain (`NVK_ZCULL=off` changed no
pass at its hub). Try it only in a long session, with the shadows in view.

#### EDRAM removal: what was not done (2026-10-16)

- **Step 3's merged dispatch** (one depth resolve per tiles-once pair): the second copy's destination would have to be
  known before the first copy; the ring decides the pair after it, and the first copy's clear runs between the two.
  About 0.3 ms; the barriers were narrowed instead.
- **Step 4** (skip the second tile pass's command buffers): that pass still has effects (its resolve clears, fence and
  event writes the CPU waits on) that would have to be replayed exactly: the kind of record-and-replay that lost two
  console A/Bs. The largest CPU item left (2.3-3.5 ms of ring a Swap at stock); worth it once a console log shows the
  ring is the limit again.
- **Step 6** (loading-transition representation switches): loading hitches only; needs a trace of those frames.
- **Step 7** (per-view memo epochs, surface-level Clear): the memo already serves 92 % of draws; per-view epochs
  touch every place that changes tile ownership (12), where a missed one is a missing transfer. Under 1 % of the ring.

#### `banjo_gpu_edram4_split_tiles`

A draw that overwrites part of a tile another view holds, and touches nothing else of it (the edge strip of
Direct3D's Clear, the padding between a surface and its pitch), takes the tile without a transfer. The old owner is
noted as holding the rest. If the tile is overwritten whole before anything needs the rest, the rest never moves; if
something needs it first, the split is healed (the owner's part copied aside, the ordinary transfer, the part copied
back). Collapsed MSAA depth views take split tiles too when the rest is one rectangle (their heal imports only the
rest).

- **Checked:** `banjo_diag_split_tiles = 1` (every split healed at once) and `= 2` (every rest filled with magenta
  or a depth of 0.37), each with the key switched every 2 s (target 18): the same picture; verify run 0 differences.
- **Measured (PC gameplay):** EDRAM tiles moved per frame 176 -> 23.

#### `banjo_gpu_dead_raster`

Banjo's particle simulation runs as point-list draws whose vertex shader writes memory (memexport); what they
rasterize into the bound target is never used. A site (shaders, surface, formats, depth control, mask) that has been
overwritten whole four times in a row runs with colour mask 0 and depth test and write off; the memory writes stay.

- **Checked:** on every use the next draw that touches a target must overwrite every tile the group could have
  written, otherwise `DIFFERENCE` and the key is off for the session.
- **Measured (PC gameplay):** with `banjo_gpu_rect_split`, EDRAM tiles moved per frame 1,042 -> 171 (832 of them were
  pulled for these draws).

#### `banjo_gpu_rect_split`

An auto-indexed rectangle list of 2 to 8 rectangles (the L-shaped rest of a Clear outside whole tiles) is drawn as one
draw per rectangle, so the tiles pulled are those under each rectangle, not the box around them (42 -> 16 tiles a
frame for the 220x220 target).

- **Why exact:** the same pixels in the same order.

#### `banjo_d3d_api_clear`, `banjo_d3d_api_resolve`

The native Direct3D layer's first two operations (`app/src/me/banjo_d3d_clear.h`; `docs/d3d-native.md`, section 9).
A hook on Direct3D's one-rectangle clear (`82223560`) and on `D3DDevice_Resolve` (`8222D6C8`) writes a marker around
Direct3D's own packets (type-3 NOPs) giving the surface's size when the clear or resolve covers it whole. The ring then
widens that rectangle to the pitch and the end of the tile row, so the partly covered edge tiles are taken whole
instead of being copied in from the EDRAM's previous owner.

- **The one assumption:** nothing reads the padding through this surface, and a surface that shares the EDRAM clears
  its own area before using it. `banjo_diag_clear_padding = 1` fills the taken pixels with magenta or a depth of 0.37
  (`banjo_api_clear_poison.toml`): no change in a recorded A/B.
- **Measured:** PC: EDRAM transfers 0 tiles in every report of the level (about 6,900 a report before). Intro
  cutscene with the console's settings: about 87 tiles a frame with neither, about 24 with the Clear operation, 0 with
  both. In the console's level the EDRAM categories had been 23.4 ms of GPU time a frame at stock.
- **A/B:** targets 20 and 21; `banjo_api_ops_off.toml`.

#### `banjo_gpu_skip_dead_draws`

A draw that writes no colour, no depth, no stencil and no memory is skipped before its EDRAM preparation (the ring
answers occlusion queries itself). About 850 a frame in the intro. From sasrt-nx.

- **Checked:** verify mode draws them and checks that the renderer's own write analysis publishes nothing for any
  (253,154 draws).
- **Measured:** no change on the PC (vsync-bound); each costs the console's driver about 2.4 us and the GPU its draw.
- **A/B:** `banjo_dead_draws_ab.toml` (target 22).

#### `banjo_gpu_tiles_once`

Direct3D renders the 1280x720 2x-MSAA scene in two predicated tiles of the same EDRAM rows and sends the frame's
command buffers twice. The native targets are not EDRAM and can be as tall as both tiles, so the first pass draws
every shared draw once for both tiles' rows; the second pass's draws and copies are compared with the ones made for
them and dropped. Anything it does not recognise sends that frame back to one pass per tile.

- **Why exact:** the target pixels are the same. One difference by design: a draw that samples the resolved scene
  across the tile boundary sees this frame's rows there instead of the previous frame's.
- **Checked:** every second-pass draw is compared field by field with its counterpart; a difference is a `DIFFERENCE`
  and the key is off for the session.
- **Measured:** PC gameplay: about 512 second-pass draws a frame dropped, every one matched. Console (second run):
  250 to 295 scenes per 10 s drawn once, every dropped draw matched.
- **A/B:** `banjo_tiles_once_ab.toml` (target 9). `banjo_cpu_verify.toml` turns `banjo_direct_state` on, and this
  key stands down while that is on.

### Command recording

#### `banjo_gpu_resolve_draw`

A same-format RGBA8 colour resolve is one fragment pass on the 3D engine (`sa2_resolve_copy.frag`) instead of
`vkCmdCopyImage`, which NVK records on the copy engine with a wait for idle at each engine switch (about 30 resolves a
frame). From sa2-nx.

- **Why exact:** UNORM8 round-trips exactly through float.
- **Measured (PC):** GPU "copies" 5.7 -> 4.4 ms per Swap.

#### `banjo_gpu_push_once`

The three push-constant addresses (the set-4 shaders never read them) are pushed once per recorded state, not on every
draw whose constant offsets moved (NVK: a 6-word root-table upload per draw less; PC about 55,000 per report). From
sa2-nx.

- **Why exact:** unread values; a pipeline without set-4 constants would be a DIFFERENCE and turn it off.

#### `banjo_gpu_pass_narrow_end`

A render pass's end dependency names its consumers (shader, transfer and attachment access) instead of
`MEMORY_READ | MEMORY_WRITE`, so NVK does not invalidate its constant, QMD and macro caches at every pass end. From
sa2-nx.

- **Why exact:** the same ordering for everything that reads attachments. Banjo's memexport streams are shader storage
  writes, not covered by this dependency either way.

### Shader package

These keys pick specialization-constant variants of the shaders in `banjo_shaders.mesp` (built by
`tools/build_shader_library.sh`), so they need the shader package of the same date. A pipeline whose shader lacks a
variant keeps its old key.

#### `banjo_gpu_blend_skip`

Pixel shaders discard a pixel whose blend would leave the RGBA8 target unchanged (one colour target, no
alpha-to-coverage, no depth or stencil write, op ADD, destination factor 1 once the source is 0). 683 pixel shaders
have the variant. From sa2-nx (UnleashedRecomp-NX's idea).

- **Why exact:** the pixel would not change; a NaN output is never discarded; occlusion queries are answered on the
  CPU.

#### `banjo_gpu_structured_cf`

Shaders whose forward-only control flow the translator proved equivalent (every jump outcome) run as nested if / else
instead of an `if (skipTo <= pc)` per control-flow instruction. Only 3 of Banjo's shaders fit. From sa2-nx.

#### `banjo_gpu_early_alpha`

An alpha-tested pixel shader runs a copy of its alpha computation right after the last instruction the alpha needs
(`tools/early_alpha.py`, a step of the library build, spec bit 23), and a 2x2 quad whose four pixels all fail the test
there stops before the rest of the shader. Aimed at Nuts & Bolts' shadow-receiving materials (PS536 and its family,
about 30 ms of GPU a frame in the console's intro at stock), whose alpha needs only the first fetch or two but is
tested after three shadow cascades and the lights.

- **Why exact:** the copy is the same operations on the same values, only the components the alpha needs. A product
  of a constant that is 0 at run time and a finite value is replaced by 0 under a per-draw guard. A fetch is repeated
  only where all four lanes of every quad sample there. A pixel fails early only when the test fails for every alpha
  within (|alpha| + |ref|) * 2^-12 + 1e-30 of the copy's value, which covers a multiply-add the driver fuses in one
  place and not the other and a flushed denormal. Whole quads only: no shaded pixel loses a derivative neighbour.
- **Checked:** at library build time every copy is run against the original by an interpreter (float32) on 24 random
  sets of inputs; a copy that differs once is dropped. 464 of 685 pixel shaders have one
  (`out/shaders/early_alpha.txt` lists every shader and why).
- **Measured (PC):** the alpha-tested PS536 and PS447 groups -9 % and -10 % of their GPU time, the small ones 30-54 %;
  no picture change in the in-run and recorded A/Bs. Not run on a console yet.
- **A/B:** `banjo_early_alpha_ab.toml` (target 26).

#### `banjo_gpu_texcoords_unswapped`, `banjo_gpu_no_texture_signs`, `banjo_gpu_identity_remap`, `banjo_gpu_index_base_held`

From sa2-nx's GPU round 2: per-vertex and per-pixel tests that are constant for a draw are removed by a variant.

- `banjo_gpu_texcoords_unswapped`: vertex shaders skip the swapped-texcoord test (`g_SwappedTexcoords` is always 0
  here; verify checks it).
- `banjo_gpu_no_texture_signs`: when no texture of the draw has a signed, biased or gamma component, the shaders skip
  the per-fetch sign test (about 61 % of the PC's draws). The skipped code is the early return the zero signs take.
- `banjo_gpu_identity_remap`: a pipeline whose vertex inputs keep their component order skips the per-input remap
  test (verify checks every code is 0xFFF).
- `banjo_gpu_index_base_held`: vertex shaders proven never to read the vertex index (301 of Banjo's) run with the
  index base held at 0, so consecutive draws keep the same shared constant block.

#### `banjo_gpu_texture_signs_per_slot`

`banjo_gpu_no_texture_signs` removes the per-fetch sign and gamma code only when no texture of the draw has signs.
The heavy material shaders (PS536 and its family: 31 fetches, 24 of them three shadow cascades) never qualified,
because one or two of their textures have signs, so every fetch kept a runtime test and the gamma / bias code:
2,300-3,100 NAK instructions on the console. Now specialization constant 61 (both stages) has a bit per fetch
constant: set when that register's texture has packed signs in the draw. The translator passes `sN_Signs` to every
fetch, and the fetches of the other registers fold to the path they take anyway.

- **Why exact:** a fetch constant whose texture words all have packed signs 0 gives 0 at run time too; the skipped code
  is that early return. A component with sign 1 ("signed") is returned as sampled by the same code, so only signs 2
  (biased) and 3 (gamma) count (`kSignsEffective`; from the twentieth package's second build, for
  `banjo_gpu_no_texture_signs` too): Nuts & Bolts' shadow atlas has sign 1 on every component. The bits are taken
  from the same words the shared block receives.
- **Checked:** with `banjo_cpu_verify`, every texture word of the draw (pixel shader slots, s16 / s17, vertex shader
  slots) is checked against the bits; a word with signs whose bit is clear is a `DIFFERENCE`.
- **Measured:** PC (NVIDIA's compiler): PS536 32,768 -> 18,432 bytes of machine code, all pixel shaders -39.7 %.
  Offline (the console's specialization, optimised SPIR-V): PS536 3,325 -> 2,075 instructions when no slot has signs; with `banjo_gpu_branchless_select`, 1,980 and 37 conditional branches (85 before). The register
  mask goes in the pipeline key's spare word, so draws whose textures differ in signs get their own pipeline.
- **A/B:** target 27.

- **Off for package 20e only (2026-10-16)**, on again: the ring's cost in steady state is 0 to +1 % (A/B 27, later
  windows). When it is off, the prewarm takes records made with it as the key the ring asks for (no_signs 0).

#### `banjo_gpu_vfetch_block`

Vertex fetches indexed by a computed register (the bone palettes of skinned meshes, `banjoVfetch`) read each 32-bit
word with a range test and a branch around the load: 48 per vertex in the 4-bone skinning shaders (VS n58, n620, n690,
n768 in package 19: 1,346-1,420 NAK instructions, 16,500 static cycles, 48-56 registers), so no two loads were in
flight together. These vertex shaders cost ~6 ms a frame on the console in the level's small pass alone. Now each word
is loaded at its index clamped into the buffer and replaced by 0 outside it, with no branch; an empty buffer gets a
readable address from the renderer (the upload buffer's start), so the shader needs no test for it.

- **Why exact:** every word is the one `banjoVfetchWord` returns: the loaded word when its index is inside the buffer,
  0 otherwise (a negative index wraps to a large unsigned one).
- **Measured (offline):** the four skinning shaders 2,341-2,511 -> 1,119-1,281 instructions, 100 -> 4 conditional
  branches.
- **A/B:** target 29. The old form is `SPEC_CONSTANT_VFETCH_WORDWISE` (bit 7), set on those vertex shaders when the key
  is off, so with the key on the pipelines keep the keys the prewarm records have.
- **16-byte bone rows (2026-10-16, same key):** a `k_32_32_32_32_FLOAT` fetch whose stride and offset are multiples
  of 4 words (the bone rows) is one 16-byte load (`banjoVfetchAligned4`) instead of four word loads, each with its own
  64-bit address. The renderer makes every indexed fetch buffer 16-byte aligned and readable to the next multiple of
  16 bytes (the upload is rounded up); a draw whose buffer it cannot place that way (a memexport stream, a reused copy
  aligned otherwise) gets `SPEC_CONSTANT_VFETCH_WORDWISE`, which takes the word path. The ring report counts those
  draws ("word by word: bone buffer not 16-byte placeable"). Exact: the words are the ones `banjoVfetchWords` gives
  (each word past the buffer's end is 0; an index that is a multiple of 4 cannot wrap partly). Offline, the four
  skinning shaders 1,119-1,281 -> 671-851 instructions.

#### `banjo_gpu_branchless_select`

With HLSL 2021 a scalar `?:`, `&&` or `||` short-circuits: DXC writes a branch and a phi, and NAK keeps every branch (it
flattens nothing, as UnleashedRecomp-NX found). The translator wrote predicate set / push / pop / restore, the vector
predicate push and the kill tests that way, and `tools/early_alpha.py` copied them. Now they are `banjoSel`,
`banjoSel0`, `banjoAnd` and `banjoOr` (`shader_common.h`): `select()`, `and()` and `or()`.

- **Why exact:** the operands have no side effects; the same values are chosen.
- **Measured (offline):** PS536 54 -> 37 conditional branches, 2,075 -> 1,980 instructions.
- **A/B:** target 30. `SPEC_CONSTANT_SHORT_CIRCUIT` (bit 3) gives the short-circuit forms when the key is off (the same
  forms through a macro; not the byte-identical code of package 19). `early_alpha.py` reads both.

#### Package 25's shader round: `banjo_gpu_clamp_cbuf`, `banjo_gpu_sign_mask_skip`, `banjo_gpu_vs_outputs_unwritten`, `banjo_gpu_bone_padding`, `banjo_gpu_robustness_off`, `banjo_gpu_edram_checks_cpu`

Items 3, 5, 16, 15, 4 and 61 of `docs/shader-translator-ideas.md`, measured against the console's NAK assembly of
package 24b. Specialization constant 0 has no free bit left, so the translator's old forms are bits of a second one,
constant 2 (`g_SpecConstants2`, `SPEC2_*` in `shader_common.h`), kept in the pipeline key's `spec2` word (the former
`bools_reserved`, 0 in older prewarm records: they get the new forms). `kSpec2Robust` (bit 8) is the renderer's only:
it keeps the two robustness forms apart as pipelines.

- **Clamp bounds** (`banjo_gpu_clamp_cbuf`, A/B 49): the translator writes `clamp(x, BANJO_FLT_LO, BANJO_FLT_HI)`; the
  macros read words 196 / 197 of the shared block (bytes 784 / 788, `kWordClampBounds`, written every draw), or are
  `FLT_MIN` / `FLT_MAX` in the old form. NAK's `fmnmx` takes a constant-buffer operand for free and moves a 32-bit
  immediate first (`nak/sm50.rs`, `OpFMnMx` legalisation). `tools/early_alpha.py` knows the two names as scalars, and
  the flattening pass allows them (it refused the blocks that held them in the first build: 25 branches instead of 19).
- **Sign mask** (`banjo_gpu_sign_mask_skip`, A/B 50): `EffectiveSigns` clears the fields that are 1 in the slot word
  (2 and 3 stay), so the top byte is 0 exactly where constant 61 (and `SPEC_CONSTANT_NO_TEXTURE_SIGNS` for the whole
  draw) says there is no effective sign; `banjoIndexWithoutSigns` skips the mask there.
- **Unread outputs** (`banjo_gpu_vs_outputs_unwritten`, A/B 51): the outputs are `banjo_o*` in the signature, `main`
  writes locals of the old names, and every exit copies a local where the pixel shader reads it (else writes 0 in the
  old form, `SPEC2_OUTPUTS_WRITE_ZERO`). An output never stored is an undefined value to NIR, whose store it drops, and
  NAK then has no attribute for it.
- **Bone padding** (`banjo_gpu_bone_padding`, A/B 52): each indexed fetch upload is followed by zeros to 16 bytes past
  its last 16-byte block (and an empty buffer points at 32 zero bytes); such uploads have their own dedupe tag
  (`kOrderPadded`), so a dedupe hit is one of them. `banjoVfetchAligned4` loads at `min(index, align4(words))` and
  `banjoVfetchWords` at `min(index, words)`: the padding makes every clamped index read 0, what the selects gave. A
  draw with a memory-export buffer keeps the selects (counted in the report line).
- **Robustness** (`banjo_gpu_robustness_off`, A/B 53): `VkPipelineRobustnessCreateInfoEXT` with storage buffers,
  uniform buffers and images `DISABLED` (vertex input as the device has it) on the game's pipelines; NVK's
  `clamp_desc_array_bounds` (`nvk_nir_lower_descriptors.c`) is then off, which drops two `imnmx` from every texture
  handle. The SDK enables `VK_EXT_pipeline_robustness` and its feature (`device.h`: `REX_UI_VULKAN_PIPELINE_ROBUSTNESS`).
- **EDRAM import checks** (`banjo_gpu_edram_checks_cpu`, A/B 54): the five multisample import shaders' `textureSamples`
  / `textureSize` checks are behind `kCheckImages` (constant 0); each import pass keeps a second set of pipelines with
  it false (`pipelines_fast`), bound when the key, its A/B half and the CPU's check of the source's sample count allow.
- **Report line:** "package 25 shader forms since the last report" (draws with each form, unpadded draws, robustness
  available or not) and "EDRAM depth imports with the image checks made on the CPU".
- **Also in this round** (no key of its own): the flattening pass allows the shader's literal registers (`cN`), so the
  in-place material shaders' light blocks are flattened like the others' (`ps_i1a1a53fc5f5efc71`: 25 -> 19 branches,
  12 -> 6 phis, offline); `banjo_gpu_pred_select_ps`'s A/B covers it.

#### `banjo_gpu_pred_select`, `banjo_gpu_pred_select_ps`

The Xenos predicates whole EXEC blocks and single instructions; the translator writes both as `if (p0) { ... }`. The
Switch's NVK flattens an `if` only when it holds at most 8 ALU operations and no load (`nak_nir.c`,
`nir_opt_peephole_select` with limit 8), so every bone of the 4-bone skinning shaders was a real branch (SSY / BRA /
SYNC on Maxwell): its three bone-row loads are issued only once the previous bone's block is done, four memory
latencies in a row per vertex. The level's skinned draws are small (about 2,000 vertices), too few warps to hide them;
the small pass's skinned draws cost 4.3 ms a frame with almost no pixels (package 20e, PS n538's VS n770).

Now `banjoFlattenPredicated` (`shader_recompiler.cpp`, a pass over the finished `main`) rewrites each block whose every
statement is an assignment to a register (r0-r127, `ps`, `p0`, `banjoVfetchIndex`) of arithmetic on registers, static
constants and indexed vertex fetches: the block runs unconditionally, each register it writes saved first and put back
with `select()` where the predicate (taken at the block's entry, as the `if` takes it) does not hold. Blocks with
texture fetches, kills, outputs, memory exports, relative constant indexes, nested control flow or too many statements
(24 in a vertex shader, 12 in a pixel shader) stay as they are.

- **Why exact:** the statements have no side effect and cannot fault (the vertex fetches are clamped into their
  buffer, the constants have static indexes); each register ends with the value the branch gives it. In a pixel
  shader no derivative can be inside one (none is on the list of allowed names), so later samples see the same values
  in every lane.
- **Measured (offline, the console's constant 0):** skinning shaders 851 -> 824 instructions and 4 -> 1 conditional
  branches each (with the 16-byte bone rows); VS n414 731 -> 708, 15 -> 10. The heavy material pixel shaders (PS536
  family and 11 others, unspecialised) 37 -> 19-25 branches, up to -1.5 % instructions.
- **Early alpha:** `tools/early_alpha.py` reads the construct as the `if (p0)` it replaces; its copies are the same text
  as without the pass (checked on the 13 heaviest pixel shaders).
- **A/B:** targets 37 (vertex shaders) and 38 (pixel shaders): separate keys because in a pixel shader a flattened block
  also runs for the warps a branch skipped whole. The old forms are `SPEC_CONSTANT_PRED_BRANCHES` (bit 2) and
  `SPEC_CONSTANT_PRED_BRANCHES_PS` (bit 4), both free in the Mass Effect build; with the keys on, the pipelines keep
  their keys. A package before 2026-10-16 has only the old forms.

#### `banjo_gpu_tess_grid`

Nuts & Bolts' water is tessellated triangle patches (VS n414; in the level 33 patches at 16 segments, two draws). The
renderer has no tessellation stages: it drew `patches * 3 * N^2` vertices as a triangle list, each invocation one
corner of a uniform barycentric grid over its patch (`banjoTessDomain`: two integer divisions and a square root to
find its grid point, then the shader's own work: three bone-style fetches and five texture fetches). Each grid point
is shared by up to six triangles, so the same point was computed up to six times: 25,344 invocations a draw for 5,049
points, 1.8-2.9 ms a frame on the console (package 20e).

Now the patches are drawn indexed: `(N + 1)(N + 2) / 2` points a patch, the vertex id is the point's number
(`patch * points + a (a + 1) / 2 + b`) and the index list, built once per patch count, segment count and winding
(`TessGridIndices`), gives each corner of the old list the point it decodes to. The shader takes the point from
`banjoTessDomain`'s bit 30 path. The ring report counts the draws and the invocations saved ("tessellated patches as a
grid").

- **Why exact:** a point's domain is computed from the same `(a, b)` and patch with the same operations, so every
  corner has the same vertex outputs as before; the list keeps the triangles, their order, their corners and their
  first vertex. Only the number of times each point is computed changes.
- **Limits:** up to 65,535 points a draw (16-bit indices); larger draws keep the old list.
- **A/B:** target 39. Needs a shader package from 2026-10-16 on (an older one ignores bit 30 and would decode the
  grid ids as list ids: the key must be off with an old package).

#### `banjo_gpu_bool_spec`

The material shaders test bool constants at run time: Nuts & Bolts' family of PS536 tests 14 (twelve light blocks and
two output paths), and every path is in the binary, each behind a uniform branch, with the registers of the largest.
Now each bool test reads specialization constant 1000 + N (`BANJO_BOOL` in `shader_common.h`; the default 2 means
"read at run time", as before), and the renderer creates a pipeline with the values of the bools its shaders test.
The list comes from the modules' SpecIds (DXC keeps only the constants a shader references). The values go in the
pipeline key (`PipelineKey::bools`, 31 bools at most), so each combination has its own pipeline.

- **Why exact:** the specialised value is the one the shader reads at run time for that draw (the same registers fill
  the shared block's bool words). Checked: with `banjo_cpu_verify`, every specialised bit is compared with the
  shared block's word.
- **Compiles:** a combination not created yet draws with the generic pipeline (the same key without bools: the same
  pixels) while a background thread at the prewarm's priority creates it (`banjo_gpu_bool_spec_async`, default on;
  false = the ring creates it at once). The prewarm list keeps the specialised pipelines for the next session
  (list version 3; version 2 lists are read with the new key words 0).
- **Measured (offline):** PS536 1,980 SPIR-V instructions unspecialised; 879 with every bool off, 1,813 with every
  bool on.
- **A/B:** target 31 (`banjo_bool_spec_ab.toml`, which turns the key on).
- **Ring cost (2026-10-16).** Off for one package (20e) on a first reading of +8.6 % ring work per draw (PC A/B 31,
  whole run); the later windows of the same A/B, once every combination had been created, give +2.4 to +2.9 %, with the
  same number of pipeline binds. The level is GPU-bound at stock, so it is on again. When it is off, the prewarm takes
  the list's specialised records as their generic keys.

#### `banjo_gpu_bool_spec_direct`

The first console session with an empty cache (2026-10-15) compiled 711 pipelines on the ring at 382 ms each and 316
more in the background at 431 ms each. For about 400 material draws the ring compiled the generic pipeline (every
light's code: the slowest variant there is) only for it to stand in while the background compiled the specialised one,
which then replaced it. Now the generic pipeline stands in only when it costs no compile (already created, or in the
cache); otherwise the ring creates the specialised one at once. That is a cache lookup when it is in the prewarm list,
otherwise one compile of the smaller variant instead of two. The prewarm also skips the generic twins of specialised
records (626 of the PC's 2,102), except in an A/B of target 31, whose "off" half draws with them.

- **Why exact:** the specialised and generic pipelines draw the same pixels (`banjo_gpu_bool_spec`); only which one
  is compiled changes.
- **Measured (PC, empty cache, 90 s, same start file):** ring compile time about 40 s → 12.6 s; background compiles
  1,061 → 0; prewarm 79 s → 62 s; total compile work about 151 s → 75 s.
- **A/B:** none in-run; it decides compiles, not the frame's work. The `bool specialization` report line counts the
  pipelines created at once ("from the cache" / "compiled") next to the background ones.

#### `banjo_gpu_spec_per_stage`

NVK caches each stage on its own (`masseffect_nvk_cache_per_stage`), keyed by the module and the specialization
constants it is given. Both stages were given everything:

- constant 0 whole, although the package's vertex shaders test bits 3, 5, 7, 13 and 19 and its pixel shaders bits 1,
  3, 6, 10, 11, 16-18 and 23;
- constant 61, which 17 of the 317 vertex shaders declare;
- the bools of both shaders, although 300 of the 317 vertex shaders test none.

So a vertex shader was compiled again for every light combination and alpha function of the pixel shaders it was
paired with. Each stage now gets only what its module reads. Constant 0 is reduced to the bits the module tests, found
in its SPIR-V: every use goes through `OpBitwiseAnd` with a constant, after logical right shifts by constants. All 849
modules of the package qualify; anything else makes a module "unknown", and it is given everything as before. 60, 61
and the bools are given only if the module declares them.

The analysis reads the final module: the library's SPIR-V for vertex shaders (rectangle-expanded ones are left unknown)
and the tracked code of the final pixel module, with the depth-half and float24 transforms analysed when they are
composed. On the PC, 4,965 stages were filtered and 15 left unknown. Changing what every pipeline is given changes every
cache key, so the pipeline file's tag carries a renderer epoch (`kPipelineCacheEpoch` = 2). The first session drops
the old cache, instead of keeping its entries unused, and the prewarm list rebuilds it.

- **Why exact:** a module reads only the constants it declares, and only the bits it tests of constant 0, so it sees
  the same values. Checked on the PC with two recordings, on and off: only timing differences between runs.
- **Estimated (the PC's pipeline list):** 1,393 vertex compiles → 610, 1,126 pixel compiles → 1,098, about a third
  fewer stage compiles.
- **A/B:** none in-run; the compiled code is the same, only how often it is compiled changes.

#### `masseffect_native_vs_pruned_outputs`

The renderer has always passed specialization constant 60 (the input locations the pipeline's pixel shader reads, 0
without one), but Banjo's translator never declared it, so every vertex shader computed all 22 outputs. Now the
vertex shader's exit sets the unread outputs to 0 and the math that only fed them folds away. In the non-CPU group from
2026-10-15. Depth-only pipelines (shadow maps without alpha) keep only the position.

- **Why exact:** the pixel shader never reads the outputs removed. Locations are TEXCOORD0-15 = 0-15 and COLOR0-5 =
  16-21 in both stages (DXC drops an unread input but keeps the others' locations).
- **Measured (offline):** the skinning shaders lose 89 instructions in a depth-only pipeline; with PS536 nothing, since
  they write only what it reads.

### Renderer fixes

These make the native renderer match the console and the emulated renderer. They are in the non-CPU group so that
`banjo_noncpu_enhancements = false` gives the renderer as it was, for comparisons.

#### `banjo_native_partial_resolve_inside`

A resolve counts as a later rectangle of an existing resolved texture only when it declares that texture's height (or
the rows left below its advanced base, the 2048x2048 shadow atlas resolved as 16 bands) and lies inside it. Without
it a whole new texture allocated a few pages above a freed one was drawn into the old one (black speckles on SA2's
console). From sa2-nx 1.0.1 with Banjo's second height rule (sa2-nx's rule alone split Banjo's atlas).

#### `banjo_native_resolved_retire_idle` (+ `banjo_native_resolved_idle_frames`, `banjo_native_resolved_budget_mb`)

A resolved texture nothing resolved to or read for 3,600 presented frames is released. Without it every address ever
resolved to keeps its image and GPU memory fills up in long sessions (sa2-nx and nfsmw-nx: shadows glitch, then black
textures after one to two hours). Banjo's additions: textures the game keeps reading long after drawing them (a
shadow atlas redrawn only when something moves) are kept until memory is short or the resolved textures pass 128 MB.
From sa2-nx.

#### `banjo_native_tfetch_filters`

A sampler takes the filters the shader's texture fetch instruction asks for (mag, min, mip, anisotropy, each unless
the instruction says "from the fetch constant"), as the console does. 21 shaders of the main menu override something.

#### `banjo_native_mip_range`

A sampler's mip range follows the reference rule: limited by the texture's size, level 0 only without a mip address,
from level 1 when the base address is null.

#### `banjo_native_stacked_baseless`

A stacked texture (2D layers sampled as 3D) whose fetch constant starts above level 0 is built from that level. The
game streams textures small levels first and points the base at other data until the largest level arrives. **This
was the dark patches on Banjo's fur.**

### Package 26 (2026-10-16/17)

#### `banjo_gpu_literals_cbuf`

A shader's literal constants that NAK cannot encode as immediates (any of the low 12 mantissa bits set) are read from
the constant registers Direct3D loaded them into, a free constant-buffer operand, instead of a move before each use
(107-117 moves in the in-place material shaders: their shadow taps' Poisson offsets). The renderer reads each
shader's literal table from its container and compares those registers with the definitions at every draw; a draw where
one differs (or whose table was not read) takes the old form, `SPEC2_LITERALS_IMMEDIATE`. Exact: the same values.
PC, intro route: 207,956 draws per 10 s, 13,552 of them in the old form. In-run A/B target 56.

#### `banjo_gpu_stencil_clear_partial`

A draw proven to replace the stencil of a rectangle without reading it (Direct3D's Clear: test ALWAYS, REPLACE or ZERO,
write mask 0xFF, the rectangle covered whole) no longer fetches the deferred stencil of every tile of its view first.
Tiles inside the rectangle need none, tiles it does not touch keep their deferred stencil (fetched by the first draw that
reads them), and only the tiles it covers in part are fetched, one clipped pass per strip of them. This is the once a
frame `edram_import9` of the levels (96 tiles before the 96x96 Clear) and the 512-tile one after the title. PC: 482-576
such draws per 10 s, 241-288 strips (1,446-1,728 tiles), 0 rectangles left tile by tile. In-run A/B target 57.
`banjo_diag_import9_steps` (diagnostic) times each step of a 9-pass import in the GPU report (VS 70 depth, 71-78 stencil
bits, 79 pass end, 80 after), to tell on the console whether its cost is per pixel or per pass.

#### Renut's renderer

`banjo_renut_pipeline_cache`, `banjo_renut_precise_barriers`, `banjo_renut_upload_cached`, `banjo_renut_pmull_hash`,
the thread placement through `banjo_ring_core` / `banjo_background_compile_core` / `banjo_prewarm_threads`, and the
translator changes of Renut's XenosRecomp: `docs/renut-renderer-port.md`, section 4. They act only with
`banjo_renderer = "renut"`.

### Package 27 (2026-10-17): Renut's renderer reads banjo-nx's keys

With `banjo_renderer = "renut"` these keys of banjo-nx's own renderer act on Renut's renderer too, each the same idea
on Renut's code (`docs/renut-renderer-port.md`, section 7, has what each does there and its counts):
`banjo_gpu_nvk_draw_contract` with `masseffect_native_nvk_emission`, `masseffect_native_nvk_cbufs`,
`masseffect_native_nvk_dynamic`, `masseffect_native_nvk_preload` and `masseffect_native_set4_differences` (the Switch:
NVK's per-draw improvements and dynamic descriptors by differences), `banjo_gpu_robustness_off` (Renut's pipelines
without buffer and image robustness), `banjo_gpu_bool_spec` (bool constants as specialization constants; specialised
pipelines built by the prebuild workers, the generic one meanwhile), `banjo_gpu_blend_skip` (pixels the blend would not
change discarded) and `banjo_gpu_tess_grid` (tessellated draws by grid vertex). Each is in its master-switch group as
for banjo-nx's renderer.

Renut's translator also has banjo-nx's literal constants from the constant bank (in Renut's form: the renderer writes
each shader's literal values into its constant registers, so no per-draw check), the bone rows in one load, bone
fetches without their branch, the moves as their operand and early alpha (`tools/renut_early_alpha.py`); they are
build-time switches of the shader cache, listed in section 7 of the port document.

Not enhancements (no master switch; "Other keys" below): `banjo_renut_exact_streams`, `banjo_renut_memory_policy`,
`banjo_renut_keep_gpu_written`, `banjo_renut_diag_poison_slots`.

#### `banjo_renut_check_worker` (package 27c; CPU group; in-run A/B target 10)

The checks of `banjo_renut_exact_streams` hashed by a worker thread (`app/src/renut_engine/gpu/banjo_renut_checks.cpp`):
when the game issues a kick, every draw of that interval has been issued, and nothing they read may change before the
render thread has processed the kick (its fence completes there). The worker hashes, at that moment, the ranges the same
interval of the last frame checked; the render thread takes those hashes and hashes only the rest.

- **Why exact:** a result is taken only for the interval it was made for, only if no memexport results landed in guest
  memory since (an epoch), and it is of the bytes at the kick, which is when Direct3D's GPU reads them (the render
  thread's own hash, at the draw, is earlier).
- **Checked:** `banjo_cpu_verify` hashes again every result the render thread takes: 0 differences in about 410,000
  comparisons every 10 s of a 260 s PC run.
- **Measured (PC route):** 77-90 % of the checks answered by the worker (70-85 % of the bytes); on the console those
  hashes were about a quarter of the render thread.
- The worker runs on core `banjo_renut_check_worker_core` (0) at priority 0x2D on the Switch.

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
`shader_common.h`). NAK drops what computed only them.

- **Why exact:** Vulkan writes only the channels the write mask and the format have; blending reads a channel's own
  source and the source alpha, so the alpha stays wherever a written channel blends with it, and with alpha to
  coverage. The test: `banjo_renut_diag_trim_poison` (`configs/banjo_renut_trim_poison_test.toml`) writes 60000
  instead of 0; the picture must not change.
- No new pipelines: the key holds the colour mask, blend and formats already.
- **Measured (PC route):** 26,000-30,000 draws per 10 s write part of a target's channels; A/B no change on the PC
  (its driver sees the write mask when it compiles; NAK does not). The poison test showed no change.

#### `banjo_gpu_depth_only` (package 28; non-CPU group; in-run A/B target 11)

A draw that writes no colour, with a depth target, no alpha test, no alpha to coverage, and a pixel shader that
neither kills, writes depth nor exports (the translator's `shaderFlags`, self-checked against each shader's text) is
drawn without its pixel shader: its constants and textures are not set up, and its pipeline has no fragment stage.

- **Why exact:** depth and stencil come from the rasterizer either way (no `SV_Depth`), and nothing else of the shader
  is visible.
- **Measured (PC route):** 0 draws: every depth-only draw without an alpha test (about 42,000 per 10 s) uses one of 27
  shadow-caster shaders whose kill is the game's alpha cut-out. Kept on for draws elsewhere that qualify.

#### `banjo_renut_cache_save_quiet` (package 28; non-CPU group)

The pipeline cache is saved once no pipeline was built for 15 s (at most once a minute; after 5 minutes of compiling
regardless), not every minute with new pipelines: the save holds the driver's cache lock, which a compile on the
render thread waits for. Unleashed saves at its loading screens. Same pipelines.

### Render thread night and the shadow sort (2026-10-27)

Measured on the console in `docs/record-time-draw-path.md` ("Console run of the shadowsort package"). All are exact;
the ones the A/B list switches have their target in brackets.

- `banjo_renut_touch_compare` [26], `banjo_renut_tags_apart` [27] (Banjo/Enhancements, on): the native D3D setters mark
  only values that change, and their tags stay out of the recording devices (fewer chunks compared a recorded op).
- `banjo_deko3d_texture_skip` [39] (on): plume's bindTextures skipped for a draw whose pipeline, descriptor epoch,
  image set and shared-constant lines are those of its last whole run; the deko3d line counts "bound textures N read,
  M skipped".
- `banjo_renut_shadow_sort` [40] (**off** since the 2026-10-27 console run: render thread +1.13 ms, GPU +0.65 ms): runs of
  depth-only draws replayed sorted by shader contents, render state and texture slots; `banjo_renut_shadow_sort_verify`
  (Banjo/Debug) compares each sorted draw's whole device with its recorded op's. Set it on for A/B 40.
- `banjo_renut_replay_lookahead` [29] (**off** since the 2026-10-27 console run: +2.15 ms): the replay prefetching the next
  op's shader lookups, guest headers and texture memo entries. Set it on for A/B 29.
- Without a key of their own (A/B only): 28 render-state numbers by content, 30 base-vertex draws, 31 no texture or pixel
  constant work for draws that sample nothing, 32 texture memo refresh, 34 resident literals, 35 texture epochs by page
  and address, 36 kept fetch data, 37 kept stream sources, 38 index copies bound by pool chunk, 41 the fast path's shader
  checks by cache entry, not by shader object.

### Four-core mode and mesh objects (2026-10-27 afternoon)

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
  `banjo_renut_diag_shadow_census` (Banjo/Debug): the depth-only draws by depth target and index count.
- `banjo_xma_frame_cache` (Banjo/Audio, on), `banjo_xma_frame_cache_verify` (Banjo/Debug), `banjo_xma_frame_cache_mb`
  (24): a decoded XMA frame kept under the chain of frames its codec decoded since it was opened (the format at the root,
  each packet's exact bytes); a sound played again from its start takes the samples of the first time instead of
  FFmpeg's decode. The codec catches up on the frames it skipped (decoded, output discarded) before the first frame the
  cache cannot answer, at most 256 in a row (short repeated sounds). Exact: FFmpeg's xmaframes decoder (WMA Pro) depends
  only on the frames since it was opened (overlap-add tail, previous block length, reused scale factors); the verify key
  decodes every frame and compares. `sdk/src/audio/xma_context.cpp`, XmaContext::DecodeFrame; the XMA log line every
  10 s counts the answers.

## Other keys

Not enhancements, but they decide how the ones above run or are measured.

| Key | Default | What it does |
|---|---|---|
| `banjo_cpu_verify` | `false` | every self-check on every use, natives run both ways; the `[verify]` line's differences must be 0. Slow |
| `masseffect_native_verify_n` | `2048` | how many first uses each ring fast path checks against the slow path |
| `masseffect_native_fast_copy_verify` | `256` | the same for `masseffect_native_fast_copy` |
| `banjo_hot_guard_calls`, `banjo_hot_guard_period` | `64`, `4096` | the localized copies' guard: the first N calls of each thread, then 1 in P |
| `banjo_ring_sync_fences` | `true` | correctness of the same-frame reuse (outside the master switches): fence and memory writes, interrupts and swaps end the reuse of vertex and index copies made before them |
| `banjo_ring_vertex_persistent_mb` | `64` | size of the vertex arena; when it fills, every block is given up |
| `banjo_native_textures_reserve_mb` | `192` | GPU memory kept free: below it cold textures are evicted (after the third console run's out-of-memory end) |
| `banjo_renut_exact_streams` | `true` | Renut's renderer (package 27): a converted vertex stream or index range is hashed whole at its first use after each kick and converted again if the guest rewrote it (Renut checked them on a background thread every 4 frames). Correctness, not an enhancement; it costs: PC +1.47 ms of render thread a frame (5.00 -> 6.47), an estimated 6-8 ms on the A57; in-run A/B target 6 of `banjo_renut_ab_target` |
| `banjo_renut_memory_policy` | `true` on the Switch | Renut's renderer (package 27): limits for the console's memory instead of a PC's: cold textures released below `banjo_renut_textures_reserve_mb` (`192`) of the driver's budget or with the bindless table three quarters full, the stream cache at `banjo_renut_stream_cache_mb` (`128`) and `banjo_renut_stream_cache_idle_s` (`20`), upload ring chunks trimmed after `banjo_renut_upload_trim_frames` (`120`) |
| `banjo_renut_keep_gpu_written` | `true` | Renut's renderer (package 27): a texture the GPU wrote is never released (made again from guest memory it would lose what the GPU wrote) |
| `banjo_renut_diag_poison_slots` | `false` | a test (package 27): released descriptor slots point at a magenta texture instead of the null one |
| `banjo_renut_check_worker_core` | `0` | Renut's renderer, the Switch (package 27c): the core of `banjo_renut_check_worker`'s thread (-1: not pinned) |
| `banjo_renut_diag_build_delay_ms` | `0` | a test (package 27c): every pipeline build of Renut's renderer waits this long first, so the PC compiles as slowly as the console |
| `banjo_renut_diag_trim_poison` | `false` | a test (package 28): the channels `banjo_gpu_output_trim` leaves out are 60000 instead of 0 (the picture must not change) |
| `banjo_renut_diag_resolve_trace` | `false` (`true` in the Switch `renut.toml` since package 28) | a measurement (package 28): one frame every 30 s of Renut's resolves, clears, target changes (with their draw counts) and first samples of resolved textures, as `[trace]` lines |
| `banjo_direct_state_ab_s`, `banjo_ab_target` | `0`, `1` | the in-run A/B (above) |
| `banjo_diag_gpu_groups` | `0` | 1: GPU time per pass split by draw state; 2: also by pixel shader |
| `switch_profile_stacks` | `true` in `renut.toml` | the SDK's sampling profiler (`logs/rex/rex_profile.log`) |
| `masseffect_gpu_marks_categories` | `true` | GPU time per pass category (the `GPU per Swap` lines) |
| `banjo_cpu_profiler` | `false` | the app's own sampler (`banjo_profile.toml`) |
| `banjo_diag_shader_stats` | `""` | measurement: the driver's statistics of each shader's first pipeline (registers, max warps per SM, instructions, static cycles, spills, code size), `"all"` or a list of pixel shader numbers; needs `vulkan_pipeline_executable_info`; one extra compile per shader on the ring |
| `banjo_diag_shader_asm` | `""` | measurement: for these shaders (`"538,449,201"`, `v173` = a vertex shader drawn without a pixel shader, or `"all"`) the statistics copy is also compiled with `VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR`; NVK's final NIR and NAK assembly of each stage go to `shader_asm/<ps or vs><n>_vs<n>_s<specialization>_b<bools>.txt` next to the executable (a few hundred KB each). These copies go to the front of the statistics queue. Works with `banjo_diag_shader_stats` empty |
| `vulkan_pipeline_executable_info` | `false` | SDK key: enables VK_KHR_pipeline_executable_properties when the driver has it (for `banjo_diag_shader_stats`) |
| `banjo_diag_vk_calls` | `false` | measurement: Vulkan commands per Swap, barriers (layout changes, MEMORY_READ / WRITE access masks, ALL_COMMANDS stages) and render passes by their load and store operations, and the call sites of both (return addresses relative to `masseffect::native::deferred::ReportCalls`: its address in the ELF or PDB plus the offset is the site), in the 10 s report |
| `masseffect_native_stats_pipeline` | `false` | measurement: fragments shaded per pass category (the `Mfrag` figures of the `GPU per Swap` line) |
| `masseffect_native_stats_per_draw_s` | `0` | measurement: one frame every N seconds with a query per draw (serialized, slower): GPU time, draws, fragments and ns per fragment per pixel shader. While it is set the per-pass counters of `masseffect_native_stats_pipeline` stay off |
| `banjo_pool_lock` | `true` | a fix, not an enhancement: serializes the game's resource-pool reset and its getter (the start-up crash "read of guest 0x24") |

## Audit

`python tools/enhancement_keys.py --audit docs/enhancements-reference.md` reads the keys from the sources (the rows of
`LogEnhancements()` and the localized copies' list `app/generated/hot/banjo_hot_list.inc`, their definitions and the
shipped `renut.toml`) and checks this page: every key name in backquotes is defined somewhere, every row of the table
above has the key's group and shipped value, every key appears on the page, every premade config named exists in
`dist/switch/configs`, and the A/B target table matches the key's own description. Run it after adding a key.
