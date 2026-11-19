banjo-nx Switch test build (native renderer)
=============================================

SD card layout:
  sdmc:/switch/banjo-nx/banjo-nx.nro
  sdmc:/switch/banjo-nx/renut.toml
  sdmc:/switch/banjo-nx/banjo_shaders.mesp        <- shader library of the native renderer (NEW: replace the old one)
  sdmc:/switch/banjo-nx/banjo_shaders.mesp.idx
  sdmc:/switch/banjo-nx/cache/masseffect_native_pipelines.bin   <- NEW: replace the old one (first-boot prewarm list)
  sdmc:/switch/banjo-nx/game_root/     <- copy everything from "D:\Banjo kazooie port\game\" here
                                          (Bundle, Debug, loctext, default.xex, ...; $SystemUpdate is not needed)

Launch with a title takeover (hold R while starting a game, then open the Homebrew Menu), or a forwarder
with a 39-bit address space. From the Album it will not have enough memory.

Latest changes (this build, 2026-10-16 15:45, package 23b: the ring split, the ring prover, the codegen agent's round)
-----------------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (61,184,197 bytes, md5 ba70120c7d83...), banjo_diagnostics.toml, banjo_fragments.toml and the
  configs folder. The shader package is package 21's (unchanged: no cache rebuild). Everything of 22 is in it.
  23b against 23: the decoder thread is a host thread like the ring's own, with a 512 KB stack (23 used a default
  thread, whose stack size on the console is not known). Use 23b.

  1. The ring split (banjo_ring_decoder, step 3 of the Direct3D replacement): a new thread (core 2) decodes the game's
     command stream; the ring thread (core 1) only executes the draws. The 10 s report has a "ring split" line with
     the executor's and the decoder's time per Swap.
  2. The ring prover (banjo_ring_prover, step 2, core 0): the vertex and index data the ring used to fingerprint
     itself (11 % of the ring's time on the console) is read by another thread as soon as the game submits. Shader
     loads too (banjo_ring_prover_loads, step 2b). Report lines "ring prover ...".
  3. The CPU codegen agent's round: the XAudio limiter computes log10 only when it is used (about 5 % of the game
     threads' time), threaded dispatch for the script interpreters, r1 as a local in 10,731 functions, redundant
     link-register write-backs gone, loop flush-mode checks hoisted.
  4. The vertex input key takes the program hash from its load (banjo_ring_vs_hash_given, step 7a).
  All of it checked on the PC with banjo_cpu_verify (300 s, 0 differences).

  Most useful runs (copy the file over renut.toml; send the logs folder after each):
    a) banjo_diagnostics.toml, 3-4 minutes of play: the profiler with the new threads, the ring split line.
    b) configs/banjo_ring_decoder_ab.toml (the split against one thread every 4 s: the "ring split A/B" line).
    c) configs/banjo_deferred_ab.toml (deferred recording, now with the split and packets).
  Also there: banjo_ring_prover_ab.toml (target 42), banjo_vs_hash_given_ab.toml (44), banjo_ring_core_ab.toml (41),
  banjo_ring_decoder_core0.toml (the decoder on core 0 instead of 2). If something goes wrong (a hang, a crash,
  wrong pictures): banjo_ring_decoder_off.toml, then banjo_ring_prover_off.toml, tell me which one fixes it.

Latest changes (this build, 2026-10-16 11:00, package 22: the ring thread pinned; deferred recording with draw packets)
-----------------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (61,356,229 bytes, md5 3be70d87f368...) and the configs; the shader package is package 21's
  (unchanged: no cache rebuild if you already ran 21 or 21b). Everything of 21b is in it.

  1. The ring thread (the frame-rate limit: 99 % of one thread) is pinned to core 1 (banjo_ring_core = 1). Before, it
     floated over cores 0-2, so your overlay never showed one core at 99 %; now core 1 should read near 100 % when the
     ring is the limit. The ring report line says "banjo_ring_core: pinned now, on core 1".
  2. Deferred recording (the Vulkan driver calls on a worker thread, core 2) with draw packets: one queue record per
     draw instead of ~15. Still OFF by default; test it with configs/banjo_deferred_ab.toml (switches it every 4 s: the
     "ring thread A/B" and "GPU A/B" lines give both halves) or configs/banjo_deferred_recording.toml (on all run).
  3. configs/banjo_ring_core_ab.toml switches the pin every 4 s (A/B target 41).

  Most useful run: banjo_deferred_ab.toml copied over renut.toml, 3-4 minutes of play. Send the logs folder.
  The full Direct3D replacement has started (plan: docs/d3d-full-replacement.md); this package is its step 0.

Earlier changes (package 21b, 2026-10-16 09:45: package 21 + the CPU agent's items 1-4)
--------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (61,335,749 bytes, md5 251b97f29618...) from D:\Banjo kazooie port\banjo-nx\dist\switch\banjo-nx\.
  The shader package is package 21's (banjo_shaders.mesp 49.2 MB, dated 08:25): copy it too if you have not yet.
  Everything of package 21 (shader round, water grid, NAK assembly capture) is in this build.

  CPU (game code), all exact; PC verify session of 300 s: 0 differences:
  1. dcbt prefetch fix (generator bug): the game's 1,785 prefetch hints went to the wrong address (RA alone, often the
     null page, instead of RA + RB). 1.85 % of the game threads' console samples were on those prefetches.
  2. Vector locals by value (BANJO_GEN_VECTOR_LOCALS_BY_VALUE): vector registers in loops no longer stored to the
     context every iteration. -1.4 % of game-code cost weighted by the console's samples; XAudio mixers -15 to -19 %.
  3. The XAudio limiter (log10 / log of every audio sample: 7.3 % of the game threads' samples) as a localized native
     copy with its 10 callees inlined (key banjo_cpu_native_sub_82B0D0C0): A57 instructions per call 1,581 -> 1,264
     (-20 %), 16 calls -> 0. Its guard compared all 52,289 calls of the PC verify run with the original: 0 differences.
  4. Two exact code-generator improvements: rlwinm / rlwimi as plain shifts and bit-field extracts (one instruction
     instead of two or three; checked on every shift and mask), and 2,730 more constants folded from read-only data
     inside loops (mostly float literals; the PC build checked 4.8 billion folded loads against memory: 0 differences).
  Game code .text 43.04 -> 42.62 MB.

  Judge with renut_normal.toml (CPU sampler on: the "[cpu]" and rex_profile lines). banjo_cpu_off.toml turns every
  CPU enhancement off for a comparison run.

Earlier changes (package 21, 2026-10-16: shader translator round, water grid, NAK assembly capture)
-------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (61,782,213 bytes, md5 1d4cb2a0efec...) AND the shader package (banjo_shaders.mesp + banjo_shaders.mesp.idx: changed, 49.2 MB) from
  D:\Banjo kazooie port\banjo-nx\dist\switch\banjo-nx\. The shader package is not in the zip (game data). The first
  run rebuilds the pipeline cache (new shader package: ~7-10 minutes of background compiling at one thread); judge
  the second run, with renut_normal.toml copied over renut.toml.

  Light specialisation (banjo_gpu_bool_spec) and per-slot texture signs are ON again (off in 20e), at your request.

  New, all exact (same pixels), each with its own key and in-run A/B:
  1. Bone rows in one 16-byte load (banjo_gpu_vfetch_block, A/B 29): the skinning vertex shaders read each bone row
     with one load instead of four (12 loads a vertex instead of 48). Offline 1,281 -> 851 instructions.
  2. Predicated blocks without branches, vertex shaders (banjo_gpu_pred_select, A/B 37): NVK keeps every if that has
     a load in it, so each bone of the skinning shaders waited for the previous bone's loads. Now the blocks run for
     every vertex and keep their results with select(). Skinning shaders 4 -> 1 branches, 851 -> 824.
     In 20e the small pass's skinned draws cost ~5.5 ms a frame with almost no pixels: that is what these target.
  3. The same for pixel shaders' short blocks (banjo_gpu_pred_select_ps, A/B 38): material shaders 37 -> 19-25
     branches. The least certain gain: its A/B decides.
  4. Water drawn as an indexed grid (banjo_gpu_tess_grid, A/B 39): the tessellated water ran its vertex shader once
     per triangle corner (25,344 a draw); now once per grid point (5,049 a draw), same triangles in the same order.
     VS n414 cost 1.8-2.9 ms a frame in 20e.
  5. Diagnostics: banjo_diagnostics.toml also writes NVK's NAK assembly and NIR of the 15 heaviest shaders to
     sdmc:/switch/banjo-nx/shader_asm/ (a few MB). Please send that folder back with the logs: it shows exactly
     which instructions the material shaders spend their time on.

  Configs next to the NRO: renut_normal.toml, banjo_fragments.toml, banjo_diagnostics.toml. A/B configs in configs/:
  banjo_shaders21_ab.toml switches 1-4 together every 4 s (the GPU A/B line gives both halves); banjo_vfetch_block_ab,
  banjo_pred_select_ab, banjo_pred_select_ps_ab, banjo_tess_grid_ab one at a time. In an A/B run the "off" halves
  compile their old-form pipelines the first time each scene is seen: use the later windows of the GPU A/B line.

  PC checks (console settings, intro route): verify session 0 differences; combined A/B (40) recorded twice with
  tools/ab_flicker.py: the frames at the flips differ only by camera motion and cuts; the water grid saved 5-6
  million vertex invocations every 10 s on the PC route.

Earlier changes (package 20e, 2026-10-16 07:00: light specialisation and per-slot signs off)
---------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (61,753,541 bytes: the same size as 20d because of page padding; this one is dated 06:58).
  The first run rebuilds the pipeline cache once (the pipelines changed); judge the second run, with
  renut_normal.toml copied over renut.toml (the diagnostics config compiles a statistics copy of every shader).

  1. banjo_gpu_bool_spec (light specialisation) and banjo_gpu_texture_signs_per_slot are off: fewer pipeline
     variants, less ring work per draw (PC: 3.60 us per draw, 4.18 with the light specialisation). Their A/B configs
     turn them on again for a measurement.
  2. The prewarm takes the old list's records as the keys the ring asks for now: PC list 2,129 -> 861 pipelines.
  3. Background compile threads (prewarm, shader statistics, shader preload) are pinned to one core
     (banjo_background_compile_core = 2): on the console their priority cannot go below the game threads' 0x3B,
     so before they could take up to three cores from the game. One prewarm thread in a rebuild again (was 2).
  4. Diagnostics' shader statistics copies wait for the prewarm and take at most a quarter of that core.

Earlier changes (package 20d, 2026-10-16 morning: Marathon's codegen review, EDRAM removal steps)
-----------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro only (61,753,541 bytes; shader package unchanged; the pipeline cache of 20c stays valid). The
  configs next to the NRO are refreshed (renut_normal / banjo_fragments / banjo_diagnostics); A/B configs for the new
  EDRAM keys are in the zip's configs folder.

  CPU (marathon-nx's proposals, audited and implemented here):
  1. Loop barriers fence the game's memory only, not the whole C++ state: registers no longer go to memory and back
     on every pass of a loop that has a barrier.
  2. The game's own prefetch hints (1,785 dcbt) are real prefetches now.
  3. 296 functions that compiled to longer code with the "locals" rewrites (they spilled) keep the plain form, chosen
     by compiling both forms and comparing (tools/locals_choice.py).
  Game code in the ELF: -0.34 % instructions, -0.87 % loads, -0.60 % stores against 20c.

  GPU (EDRAM emulation, docs/edram-removal-plan.md "Status"):
  4. Late stencil fetches through the 3D engine instead of the copy engine (1.3 ms a frame at stock in the level
     before; banjo_gpu_stencil_fetch_3d).
  5. No full GPU flush around the EDRAM clear passes; narrowed ones around the scene depth resolve
     (banjo_gpu_edram4_lean_barriers). PC: busy GPU -1.6 ms a frame; the console pays more per flush.
  6. A resolve's clear rectangles in one pass (banjo_gpu_resolve_clear_merge).
  Not taken: ZCULL for the shadow maps (written, off: UnleashedRecomp lost the GPU with ZCULL and depth-clear changes)
  and the items listed in the plan's status table.

  All checked on the PC: verify run 0 differences, A/B recordings of 4-7 the same picture at every flip.

Earlier changes (package 20c: faster shader compiles)
-----------------------------------------------------
  COPY: banjo-nx.nro only (61,835,461 bytes; shader package unchanged). The FIRST run rebuilds the pipeline cache once
  (the way pipelines are built changed, so the old cache is dropped; the log says "left out because ...") - let it
  run a few minutes, then judge the SECOND run.

  1. Light-specialised shaders compiled once, directly (banjo_gpu_bool_spec_direct): before, a new light combination
     made the game thread compile the big "every light" version just to stand in, and then a background thread
     compiled the small one again. Now only one is compiled. The prewarm also skips those stand-in versions (30 % of
     the list).
  2. Each shader stage gets only the settings it reads (banjo_gpu_spec_per_stage): 300 of 317 vertex shaders read no
     light switches, yet were compiled again for every pixel shader's light combination. About 56 % fewer vertex
     shader compiles.
  3. Two prewarm threads when the cache has to be rebuilt (banjo_prewarm_threads_rebuild), at the lowest priority.
  4. The diagnostics config's statistics copies compile on their own background thread: diagnostics runs no longer
     stall for a minute.
  PC, cache rebuild run: prewarm 79 s -> 31 s, compiling on the game thread ~40 s -> none. Same picture (recordings).

Earlier changes (package 20b: the pipeline cache fix)
-----------------------------------------------------
  COPY: banjo-nx.nro only (the shader package is the same as package 20). Three configs sit next to the NRO; the
  game only reads renut.toml, so copy the one you want over it:
    renut_normal.toml       - normal play (the same as renut.toml)
    banjo_fragments.toml    - fragments shaded per pass, per draw every 5 s
    banjo_diagnostics.toml  - every GPU measurement (slower: compiles a statistics copy of every shader)

  The pipeline cache file had grown to 258 MB, mostly shaders of older shader packages that the driver never drops.
  With it loaded there was no memory left to save new pipelines (so they were compiled again in every session), and
  once the file passed 256 MB it was ignored, prewarm list included. The file now records which shader package built
  it: a cache from another package, or one over banjo_pipeline_cache_max_mb (128), is left out; the prewarm list is
  always kept and compiles the cache again. Nothing to delete: an old file is handled on its own (the log says
  "left out because ..." once, then saves work again). From the second session on, what was compiled before
  comes from the cache (the background threads only show short bursts).

Earlier changes (2026-10-15 evening, twentieth package: smaller shaders, fewer GPU flushes, Peggle 2's
codegen proposals)
---------------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro, the shader package (banjo_shaders.mesp + .idx: NEW, replace the old ones) and the configs folder.
  renut.toml is unchanged (copying it again does no harm).

  THE FIRST RUN COMPILES A LOT: every shader changed, so the game's pipelines are compiled again (the prewarm list still
  names them). On top of that, the material shaders now get versions specialised for the lights that are on; a new
  version compiles in the background while the draw uses the general one, so it does not stall, but the first few
  minutes keep a core busy. Play a second run before judging the frame rate.

  All of the following give the same picture (checked on the PC: verify runs with 0 differences, and recordings
  compared frame by frame); each has its own key (docs/enhancements-reference.md) and they are all on in renut.toml.

  Shaders (docs/shader.md has the details; the console numbers come from your diagnostics run):
  1. Texture signs: every texture fetch of the heavy materials carried the signed / biased / gamma conversion and a
     test. The shadow atlas's "signed" setting turned out to be a no-op, so now no draw in the intro or the level needs
     that code (banjo_gpu_no_texture_signs, banjo_gpu_texture_signs_per_slot).
  2. Branch-free selects (banjo_gpu_branchless_select): the translator's predicate updates and kill tests were a branch
     each on the Switch GPU; now selects.
  3. Bone fetches (banjo_gpu_vfetch_block): skinned meshes read their bone matrices without a branch per word: the
     skinning vertex shaders about halve.
  4. Light switches per pipeline (banjo_gpu_bool_spec): the material shaders test up to 14 light / path switches at run
     time, so every light's code was in every shader. Now each pipeline is built for the switches that are on.
  5. Unread vertex outputs (masseffect_native_vs_pruned_outputs): vertex shaders no longer compute outputs the pixel
     shader never reads (the setting existed but our shaders ignored it).
  On the PC's NVIDIA compiler the main material shader (PS536) went from 32.8 KB to 11.9 KB of machine code, and the
  typical pixel shader is now in UnleashedRecomp's size range; the console's own numbers decide.

  GPU flushes:
  6. Merged depth clears (banjo_gpu_edram4_clear_merge): the ~400 full GPU flushes a frame around the redirected depth
     clears became ~50, and ~180 small clear passes about 60 (PC counts). Clears into a depth buffer that can have
     hierarchical culling (ZCULL) keep the old way, so the driver's culling data stays right.

  CPU (Peggle 2's audited codegen proposals, implemented here):
  7. Scratch registers as locals (non_argument_as_local): the registers the game's compiler uses as scratch (r11, r12,
     f0, ...) are no longer written to memory before and read back after every call. About a million such accesses in
     the game code are gone (1,023,537 -> 21,259). Checked with Peggle's audit (1,225 functions that need the old way keep it), a PC build in
     which every such register starts as garbage (the game ran normally through the intro into the level), and a
     verify run.
  8. Direct calls: 208 calls the code generator wrongly treated as hooked (because a comment named them) are direct
     calls now.
  9. A correctness fix: 77 loads of kernel variable addresses were baked in as constants from the PC's addresses; they
     are real loads again (they could have been wrong on the console).

  For your runs (each a few minutes through the intro cutscene into the first level; send the logs folder after each):
    a. renut.toml - normal play, twice (the first compiles). Is it faster?
    b. configs/banjo_diagnostics.toml - the driver's numbers for the new shaders, barriers and passes per frame.
    c. if you have time: configs/banjo_shaders_old.toml (the shader and clear changes off) for a direct comparison
       with (a), and configs/banjo_stencil_fetch_3d_probe.toml (an EDRAM experiment: the stencil copies through the
       3D engine; the same picture).

Changes in the nineteenth package (2026-10-15 midday: GPU diagnostics)
----------------------------------------------------------------------
  COPY: banjo-nx.nro and the configs folder. renut.toml and the shader package are the eighteenth package's.

  Same game code and renderer as the eighteenth package; what is new is measurement, all OFF in renut.toml (so
  normal play is as before). It answers why the GPU takes 2-4x what the Xbox 360 needed.

  For your runs, in this order (each a few minutes through the intro cutscene and into the first level; send the
  logs folder after each):
    a. configs/banjo_diagnostics.toml - everything at once. It stutters at first: the driver compiles every shader a
       second time to report its statistics (registers, threads per core, instructions, spills), and one frame in
       ten seconds is measured draw by draw. Not for judging the frame rate.
    b. configs/banjo_fragments.toml - how many pixels each pass shades, every frame, at nearly normal speed.
    c. if you have time: configs/banjo_index_persistent_ab.toml (does the persistent index cache help on the
       console?).

  What the logs will then have (new lines):
    - "[native] shader statistics" for every shader: the driver's own numbers.
    - "[native] Vulkan commands per Swap", "barriers per Swap", "render passes per Swap" and their call sites.
      On the PC these already show 420 full-flush barriers and 258 render passes a frame, nearly all around the
      depth clears: the console will say what they cost there.
    - "[native] targets: depth target ... ZCULL plane possible: yes / no (why)" for every depth buffer.
    - "[native] texture uploads by host format": which textures stay compressed on the GPU.
    - fragments per pass ("Mfrag" in the GPU per Swap lines) and per shader ("per-draw GPU time").

Changes in the eighteenth package (2026-10-15 morning: guest register writes nothing reads)
--------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro AND the shader package (banjo_shaders.mesp + .idx: NEW, replace the old ones). renut.toml and
  the configs folder are unchanged from the seventeenth package (copying them again does no harm).

  Checked on the PC with your console renut.toml, intro route (movies, title, menus, the intro cutscene, Spiral
  Mountain), 300 s each: normal run, full verify run (0 differences in 89 million checks) and a "poison" build that
  turns every removed write into garbage so a mistake would crash or show: all clean, pictures normal.
  Not run on a console yet.

  1. Guest register writes nothing reads are left out (CPU, all game threads). The recompiled game code stored every
     register the 360 code set, even when nothing ever read it: about 34,000 such writes in 13,800 functions are now
     gone (the commonest are bit tests and jump-table dispatch leftovers). This step crashed at start when first
     tried; the cause was found this morning: the analysis did not see the game's mid-asm hooks (the particle
     switches), so it removed the arguments of the calls behind them. Fixed in the analysis, which the two codegen
     steps of the sixteenth package also use (it changed one float instruction there).
     IF THE GAME CRASHES OR MISBEHAVES somewhere the seventeenth package did not, tell me where: the PC route only
     reaches Spiral Mountain, and this step touches all game code.
  2. Early alpha test (from the seventeenth package): the margin by which a pixel must fail is wider, so a multiply-add
     the GPU driver fuses differently in its copy can never make the early test disagree with the real one. Needs the
     new shader package.
  3. The startup "[enhancements]" line now also lists the four hand-written natives' keys (banjo_hand_*).

  For your runs (send the logs folder after each):
    a. the normal renut.toml: through the intro cutscene into Spiral Mountain, then play a few minutes, go
       somewhere new if you can (this build's change affects all game code);
    b. configs/banjo_cpu_verify.toml (slower; the "[verify] ... differences" line must say 0);
    c. if you have time, the seventeenth package's list still applies: configs/banjo_deferred_ab.toml and
       configs/banjo_early_alpha_ab.toml.

Changes in the seventeenth package (2026-10-15 night: heavy scene shaders, step 4 of the API layer)
-------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro, renut.toml, the configs folder AND the shader package (banjo_shaders.mesp + .idx: NEW, replace
  the old ones). Prewarm list unchanged (the renderer adapts its entries to the new shader package by itself).

  Checked on the PC with your console renut.toml, intro route: 0 differences in a full verify run, no crash, the
  recorded A/B shows no picture change. Not run on a console yet.

  1. Early alpha test (GPU). Your heaviest scene shaders (PS536 and its family, about 30 ms a frame at stock in the
     intro cutscene) test their alpha only at the very end, after three shadow cascades and the lights. A tool now
     copies each shader's alpha computation to the point where it is known, so blocks of 2x2 pixels that fail the
     test stop there. 464 shaders got the copy; every copy was checked against the original by simulation. PC: the
     alpha-tested PS536 / PS447 draws take 9-10 % less GPU time; your console's share will show in the log.
     A/B: configs/banjo_early_alpha_ab.toml.
  2. Faster vertex checks on the ring thread (CPU). Proving that vertex data is unchanged was 8.8 % of the ring
     thread in your last log (about 1.35 GB hashed every 10 s). The hash now uses the A57's carry-less multiply:
     half the instructions per byte, the same guarantees, checked against a bit-by-bit reference while it runs.
     Off: configs/banjo_pmull_off.toml.
  3. Step 4 of the API layer (CPU): a resolve now clears only the texture cache entries it can touch (cache misses
     33 % -> 11-14 % on the PC), and 16-bit index ranges stay converted across frames (about 800 MB less to convert
     and copy every 10 s on the PC). Sonic All-Stars Racing Transformed's port found an ordering slip in the second
     one (the first draw after a wait could reuse an index range proven before it); fixed here.

  4. Diagnostic for the deferred recording trial (configs/banjo_deferred_ab.toml): its log now names what makes the
     ring thread wait for the recording worker ("drains by cause"). On the PC it is the end of each frame's command
     buffer (about 600 waits per 10 s); the console's numbers decide whether submitting from the worker is worth it.

  For your runs, in order of value (each a few minutes through the intro cutscene; send the logs folder after each):
    a. the normal renut.toml;
    b. configs/banjo_deferred_ab.toml (the ring thread is your limit; this moves the driver's recording to another
       core every other 4 s, and the "ring thread A/B" line gives both);
    c. configs/banjo_early_alpha_ab.toml (the GPU A/B line and the per-shader table);
    d. configs/banjo_cpu_verify.toml (slower; the "[verify] ... differences" line must say 0).

Changes in the sixteenth package (2026-10-14 evening, sixteenth package: the API layer's Clear and Resolve, the SASRT round)
-----------------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro, renut.toml and the configs folder. Shader package and prewarm list unchanged.

  Checked on the PC WITH THE CONSOLE'S renut.toml (earlier packages were checked with the PC's own settings, which
  render the scene without 2x MSAA and never showed your console's depth switches): the intro route (title, menus,
  the intro cutscene unskipped, Spiral Mountain), 0 differences in a full verify run, no crash. Not run on a console yet.

  1. EDRAM transfers in your intro cutscene: about 87 tiles a frame -> 0 on the PC with your settings. In your last
     log these were 23 ms of GPU time a frame at stock clocks, the largest single item after the scene's own shaders.
     Two operations of the API layer (docs/d3d-native.md, section 9) do it: Direct3D's Clear and Resolve now tell the
     renderer when they cover a whole surface, so the surface's partly covered edge tiles are taken whole instead of
     being copied in from whatever used that EDRAM before (the shadow cascades, the small 4x target). Off:
     configs/banjo_api_ops_off.toml.
  2. Draws that write nothing (no colour, depth, stencil or memory) are no longer drawn: about 850 a frame in the
     intro. No gain on the PC; on the console each one costs the GPU and the driver. A/B: configs/banjo_dead_draws_ab.toml.
  3. Sonic All-Stars Racing Transformed's codegen round, all six changes (each re-tested here: hundreds of millions
     of comparisons, 0 differences): unaligned vector stores, one-register vperm, inline small memcpy / memset (with a
     fix that SASRT then took too), float sign operations (the game never uses round-up / round-down: measured),
     single-precision arithmetic in float, and register write-backs no caller reads left out (poison-tested).
  4. The GCC vector-register fix of peggle2 / SASRT (vector locals in the generated code).
  5. The "GPU A/B" log line: every A/B config now also gives the GPU time of both halves.
  Tried and switched OFF: trusting Direct3D's vertex buffer Locks instead of re-reading vertex memory. Loading writes
  vertex memory without a Lock, and the renderer's own check caught it within 30 s.

  Steps 3 and 4 of the API layer (surfaces instead of EDRAM tiles; draw state from Direct3D's objects) are not done.

  For your run: the normal renut.toml through the intro cutscene for a few minutes, and if you can, a second run with
  configs/banjo_cpu_verify.toml (slower; the "[verify] ... differences" line must say 0). Send the logs folder.

Changes in the fifteenth package (2026-10-14 15:50: split tiles for the MSAA views)
-------------------------------------------------------------------------------
  COPY TWO FILES: banjo-nx.nro and renut.toml. Shader package and prewarm list unchanged. Contains everything of the
  fourteenth package (the native Direct3D draw path).

  Checked on the PC (gameplay run 0 differences; picture compared with the change off three ways). Not run on a
  console yet.

  The split tiles of the thirteenth package now also work for the MSAA depth views. Those views cannot receive copies
  (they are made that way so the GPU can cull with them), so they were left out; now the rest of such a tile is
  brought over by an import clipped to it, and only if something needs it. This is the expensive one in your level:
  the small 4x-MSAA target's clear edge went through the nine-pass import every frame (most of the 2.2 ms of
  "import9" in the level's opening scene of your third run). On the PC the transfers left are 23 tiles a frame (176
  before split tiles).

  For your next run: configs/banjo_gpu_test.toml (ZCULL in the reversed direction + the GPU time by shader), a few
  minutes inside a level, and send the logs folder.

Changes in the fourteenth package (2026-10-14 14:46: native Direct3D draw path)
-------------------------------------------------------------------------------
  COPY TWO FILES: banjo-nx.nro and renut.toml (the same NRO as the test zip sent in chat). Shader package and prewarm
  list unchanged.

  Checked on the PC (verify run: 649,563 native draw calls compared with Direct3D's own code, 0 differences; gameplay
  run: 9.2 million native draw calls, 0 differences). Not run on a console yet.

  1. Direct3D's two draw functions (DrawIndexedVertices, which writes about 85 % of all the GPU commands the game
     makes, and DrawVertices) now run as native code that writes the same command bytes as Direct3D. It makes the
     game's drawing threads about 20 % cheaper per draw. The render thread and the GPU are where the time goes, so no
     frame-rate change is expected from this. Off: configs/banjo_d3d_native_off.toml.
  2. configs/banjo_gpu_test.toml: the two GPU questions in one run, as you asked (ZCULL in the reversed-depth
     direction + the GPU time split by shader). This is the run I need most: a few minutes inside a level.
  3. Deferred recording stays off (your test).

Changes in the thirteenth package (2026-10-14 14:34: split tiles)
-----------------------------------------------------------------
  COPY TWO FILES: banjo-nx.nro and renut.toml. The shader package and the prewarm list are unchanged (if you never
  copied the eleventh package, copy all four files of this folder).

  Checked on the PC only (gameplay run 0 differences, verify run 0 differences, picture compared with the change off
  three ways). Not run on a console yet.

  1. Deferred recording is OFF again in renut.toml: your test found it slightly worse, and so did the PC. It stays
     available as configs/banjo_deferred_recording.toml (whole run) and configs/banjo_deferred_ab.toml (switched every
     4 s; the log's "ring thread A/B" line has both). If you still have the logs of your deferred run, they would show
     why it loses on Banjo.
  2. Split tiles (key banjo_gpu_edram4_split_tiles, on). The render-memory transfers that were left came from the
     edges of surfaces whose size is not a whole number of the Xbox's 80x16 tiles (the shadow maps, the small
     targets): every clear of such an edge moved the whole edge tiles between two layouts and back, for padding that
     nothing reads. Those tiles are now taken as they are, and their rest is brought over only if something reads
     it before the tile is cleared again (it almost never is). On the PC: 176 tiles moved a frame -> 31. On your
     console those transfers cost about 3.7 ms of GPU time a frame in the level's opening scene; expect 1-3 ms of
     it to go (not measured there yet).

  How much faster, honestly: in a level, roughly 0-5 % from this package. The level's own scene pass (the game's
  shaders at 1280x720) is still most of the GPU time, and at stock clocks this is still far from 30 fps.

  The native Direct3D draw path is finished in a separate copy (Direct3D's two draw functions as native code, the
  same command bytes, checked against the original: 0 differences) and goes into the next package. It makes the
  game's drawing threads about 20 % cheaper per draw, which is not where the time goes, so expect no frame-rate
  change from it.

  From your next run I would most like: the logs folder from a few minutes INSIDE a level with this renut.toml;
  then the same place with configs/banjo_gpu_groups.toml (it splits the scene's GPU time by shader).

Changes in the twelfth package (2026-10-14 12:46: the loading transition)
-------------------------------------------------------------------------
  COPY TWO FILES: banjo-nx.nro and renut.toml. The shader package and the prewarm list are those of the eleventh
  package (if you never copied the eleventh, copy all four files of this folder: banjo-nx.nro, renut.toml,
  banjo_shaders.mesp and banjo_shaders.mesp.idx; this build contains the crash fixes too).

  Checked on the PC only (gameplay run 0 differences, verify run 0 differences, the transition compared frame by
  frame with the change off). Not run on a console yet.

  1. The jigsaw transition and the loading screens. Your logs showed 16 ms of GPU time a frame there doing nothing
     useful: the game clears the depth buffer in a way the renderer did not recognise as a clear (the alpha test was
     left switched on, which cannot matter for a depth-only clear), so the whole depth buffer was converted to
     another layout and back twice a frame. It is recognised now (key banjo_gpu_proof_depth_only, on). On the PC
     the tiles moved in those stretches fell to about a sixth. This smooths the transitions; it does not make a
     level faster.
  2. A second way to test deferred recording: configs/banjo_deferred_ab.toml switches it off and on every 4 s in
     one run and the log's "ring thread A/B" line gives the render thread's time for both. It is still OFF in
     renut.toml: on the PC the picture is the same with it, but it is slower there; Futari measured it about
     three times cheaper per draw on the console. Only your console can say which holds for Banjo.

  A correction. In our chat I said the renderer shades every MSAA sample and that real hardware MSAA would halve the
  scene's cost. That was wrong, and you agreed to hardware MSAA because of it. The scene is already drawn with one
  sample per pixel (no anti-aliasing at all); hardware MSAA would make the picture smoother at the edges and COST
  GPU time. It is not built. Say so if you want it anyway, for the picture.

  Where the time still goes (your run at 2397/1305/2733, level's opening scene): GPU 38-56 ms a frame, 23-36 of it
  the scene itself (the game's own shaders at 1280x720, about 850 draws); render thread 34-38 ms. At stock clocks
  about 2.5 times that. This build does not change those numbers.

  What would help me most from your next run, in this order (each is a renut.toml to copy from configs/):
    a. the shipped renut.toml, a few minutes INSIDE a level (not only its opening scene);
    b. configs/banjo_gpu_groups.toml in the same place: it splits the scene's GPU time by shader, which is what I
       need to attack the 23-36 ms;
    c. configs/banjo_deferred_ab.toml in the same place (if the game hangs with it, say so and skip it);
    d. configs/banjo_zcull_greater.toml in the same place.
  Send the logs folder each time, and shader_misses/ if anything is invisible.

  Still being worked on, not in this build: Direct3D as native code (its first part, the draw calls, is being
  built and checked against the original in a separate copy), and the last of the render-memory transfers.

Changes in the eleventh package (2026-10-14 11:15: the three crashes of your third run)
----------------------------------------------------------------------------------------
  COPY FOUR FILES: banjo-nx.nro, renut.toml, banjo_shaders.mesp and banjo_shaders.mesp.idx (the shader package
  changed: one shader your run reported missing was added). The prewarm list (cache/) is unchanged.

  Checked on the PC only (gameplay run and verify run: 0 differences). Not run on a console yet.

  What your logs showed, and what was done:
  1. The two crashes at start-up (low clocks): the game shuts its sound output down and opens it again about ten
     seconds after starting. A sound callback that was still running at that moment used the output that had just
     been closed. At low clocks the callback takes longer, so it lost that race both times. Fixed: closing now
     waits for the callback, and a late sound frame is dropped instead of crashing.
  2. The crash in the cutscene (high clocks): the console ran out of memory. The renderer kept an 86 MB copy of the
     pipeline cache for the whole session and needed 86 MB more every time it saved it; the level filled the
     texture cache and the next save found no room. Fixed: the copy is no longer kept, a save that finds no memory
     is skipped instead of crashing, and textures not used for a few seconds are now released when the graphics
     memory gets low (new key banjo_native_textures_reserve_mb = 192), not only at the texture cache's own limit.
  3. One shader missing from the package (its draws were skipped in the level's opening scene): added.
     If something is invisible in a new area, send the files of sdmc:/switch/banjo-nx/shader_misses/.

  Performance, honestly (your run at 2397/1305/2733, docked): intro and menus 28-30 fps, the level's opening scene
  22-27 fps with one dip to 12. In the level the GPU is the limit: the scene alone takes 23-36 ms. The render
  memory transfers that cost 5.5 ms are gone (0.4 ms now). At stock clocks everything is about 2.5 times slower,
  so this build is nowhere near 30 fps there. Nothing in this package changes speed.

  Being worked on while you are away (not in this build): Direct3D as native code and the rest of the emulation
  that can go, as you asked.

Changes in the tenth package (2026-10-14: hand-written natives)
---------------------------------------------------------------
  COPY banjo-nx.nro AND renut.toml (new keys; your old toml works too, they default to on).
  Same shader package and prewarm list. Everything of the ninth package (below) is in this build too: if you have
  not run the ninth yet, run this one instead.

  Checked on the PC only (verify run: differences 0; gameplay run: no DIFFERENCE). Not run on a console yet.

  New: six of the game's functions run as native code written by hand (the kind Sonic Adventure 2 has):
     the C runtime's memcpy and memset           (banjo_hand_memcpy, banjo_hand_memset)
     Direct3D's two shader-constant writers      (banjo_hand_constants)
     Direct3D's two state-flush writers          (banjo_hand_state_flush)
  Each checks itself against the original and turns itself off if they differ.

  Honest size: about 2 to 3 % of the game threads' time, counted in A57 instructions (memcpy about half, memset
  about a seventh of what they were; the Direct3D writers a quarter to a half less). You will not see it in the
  frame rate: in your last run the renderer thread was the limit, not the game threads.

  "Optimised for ARM" was checked the only way it can be without a console: the Switch compiler's output for each
  native was counted against the original's. That caught one of them (the state flush) being no faster than the
  original in its first version, which a PC run would never have shown; it was rewritten.

  NOT in this build: Direct3D's draw path as native code, the part of Sonic Adventure 2's work that first made it
  slower on the console. In Banjo that is a project of its own (see docs/enhancements.md, "Hand-written natives").

  For a run: nothing new to do beyond the ninth package's list below. If you want the A/B of just these natives:
  configs/banjo_hand_natives_off.toml. Lines that must not appear: "[hot] DIFFERENCE".

Changes in the ninth package (2026-10-14: after your second console run)
------------------------------------------------------------------------
  COPY banjo-nx.nro AND renut.toml (the toml gained keys; your old one works too, the new keys default to on).
  Same shader package and prewarm list. Not built with LTO or PGO (held for the release build).
  No MSAA or resolution change, as you asked.

  Checked on the PC only (gameplay and verify runs: 0 differences). Nothing of this has run on a console yet.

  Your second run (26-30 fps at 2397/1305/2733): the renderer thread was the limit (95.6 % of a core, 31-35 ms a
  frame), the GPU was busy 24 ms and idle 13 ms a frame, one core of three idle.

  1. A hole in "the scene is drawn once" is closed. Build 2032 switched that item off for the rest of the session
     when a scene began with a copy instead of a draw (it happened at a scene change). If your log had
     "DIFFERENCE: tiles once", this was it.
  2. Renderer thread (the limit in your run), six small items:
     banjo_ring_shader_load_memo   the game loads a shader program about a thousand times a frame; each load copied
                                   it three times and hashed it. Now one compare for a program loaded from the
                                   same place with the same bytes.
     banjo_ring_vertex_epoch       small vertex buffers are no longer sampled on every draw.
     banjo_ring_constants_adaptive the compare of shader constants pauses while it never finds an equal block.
     banjo_ring_identity_once      a draw asks once, not four times, whether its vertex shader is the loaded one.
     banjo_ring_sampler_fast       the sampler cache compares in line and no longer divides on every draw.
     banjo_ring_vertex_proven_first  vertex buffers already proven unchanged skip two lookups.
  3. GPU, render memory transfers again: 1,042 tiles a frame -> 171 in gameplay (PC count).
     banjo_gpu_dead_raster         the game runs its particle simulation as draws that write memory; what they draw
                                   into the render target is thrown away by the next draw. They pulled 832 tiles
                                   into place first, every frame, for nothing. Checked on every use.
     banjo_gpu_rect_split          a Clear's leftover (an L of two rectangles) pulled the whole cleared square: 42
                                   tiles, of which it touches 16.
     In your run the transfers were about 5.5 ms of the GPU's 24 ms.
  4. Game threads: 18 of the game's hot functions run as native copies (the method Sonic Adventure 2 used; keys
     banjo_cpu_native_*). Honest result: about 1 % of the game threads' time, nothing you will see. Banjo's
     generated code already has what those copies gave SA2. 77 were built and measured; 59 were not shorter on the
     A57 and were left out. Each copy checks itself against the original and turns itself off if they differ.

  What to expect at the clocks you tested: a few percent on the renderer thread (my estimate: 1 to 3 fps), and a
  GPU that is idle a little longer (which only shows at lower GPU clocks). Not 30 fps at 1020/460/1600 yet.

  What would help most from a run:
  - The normal renut.toml first, a minute or two of GAMEPLAY this time if you can (the last logs ended in the
    intro; everything above was tuned on Spiral Mountain / Showdown Town on the PC).
  - Then configs/banjo_gpu_groups.toml for a minute of gameplay: it makes the log say which draw groups and pixel
    shaders the scene's GPU time goes to. The PC cannot tell me that (its GPU spends its time elsewhere).
  - If you have time: configs/banjo_zcull_greater.toml (a driver option that may make the scene cheaper; check that
    the picture is right) and configs/banjo_transfers_off.toml (item 3 off, to compare the "imports/exports"
    figure of the "GPU per Swap" lines).
  - configs/banjo_deferred_recording.toml is for the timing only: another port (futari-nx) found that method not
    pixel-identical, so do not judge the picture with it.
  - Lines worth sending if they appear: anything with DIFFERENCE or MISMATCH; "tiles once: ... goes back";
    "[hot] DIFFERENCE"; "is not the code the copy was made from".

  One thing I saw on the PC and could not place: about a second after a jump the picture is washed out for two
  frames. It is there with every optimisation switched off too, so it is not from this package; I do not know
  whether the game does it on a real console. If you notice it (or know it from the 360), tell me.

Changes in the eighth package (2026-10-13 evening: the four items after your first console run)
-----------------------------------------------------------------------------------------------
  COPY banjo-nx.nro AND renut.toml (the toml gained keys; your old one works too, the new keys default to on).
  Same shader package and prewarm list. Not built with LTO or PGO (held for the release build).

  Checked on the PC only. Nothing of this has run on a console yet, so the frame rate below is an estimate.

  1. EDRAM transfers nobody needed. The renderer imported whole render targets from the emulated Xbox render
     memory five times a frame (20.6 of your 56 ms of GPU time, plus 9.8 ms of smaller imports). The cause was
     found in a traced frame: 16 clears a frame were not recognised as "this target is about to be overwritten"
     because of a tessellation setting the game leaves switched on, and the full-screen passes drawn as one big
     triangle were not recognised either. PC, gameplay, per 10 s: 1,394,632 tiles transferred -> 311,400; of them in
     the expensive nine-pass imports 546,600 -> 4,800.
     Keys: banjo_gpu_proof_tess_leftover, banjo_gpu_proof_triangle, banjo_gpu_edram4_color_consumer,
     banjo_gpu_edram4_alias_by_shape, banjo_gpu_edram4_copy_batch.
  2. The scene is drawn once instead of twice (banjo_gpu_tiles_once). The Xbox 360 renders 720p with 2x MSAA in two
     halves that share its small render memory, and the game sends every draw of the scene once per half. The native
     renderer has room for both halves, so each draw is now made once for both, and the second pass is only compared
     with the first (every draw, field by field) and dropped. PC gameplay: every frame drawn that way, about 510
     fewer draws a frame, every one matched. A frame the renderer does not recognise is drawn in two passes as
     before and the log says why ("tiles once" lines).
  3. Vertex data that does not change is no longer copied every frame (banjo_ring_vertex_persistent). Your console
     copied 345 MB a second. All of it turned out to be the same bytes frame after frame. It now stays in a buffer of
     its own (64 MB); each frame the game's bytes are read once to prove they are still the same, instead of being
     copied. PC gameplay: 14 MB copied a frame -> 2 MB, with 12 MB read. On the PC that is a large gain in the intro
     and none in Spiral Mountain (a PC reads as fast as it copies); the console should gain, but only a run can say.
  4. Banjo's fur is fixed (banjo_native_stacked_baseless). The dark patches were a layered texture of the fur that
     the game loads small-version-first; the renderer built it from the wrong place while the big version was not
     loaded. It was not vertex data, as the last README guessed.

  What to expect: on the GPU, most of the 20.6 ms and part of the 9.8 ms should be gone (your run: 56 ms a frame).
  On the renderer thread (50 ms a frame in your run), about a fifth fewer draws plus the vertex copies. So the build
  should be clearly faster at the clocks you tested, but it will not reach 30 fps at 1020/460/1600 yet: the scene
  itself is 16.7 ms of GPU time at 1305 MHz and the renderer thread still spends about 18 us per draw.

  What would help most from a run:
  - The normal renut.toml first (logs as last time).
  - Then configs/banjo_vertex_persistent_ab.toml and configs/banjo_tiles_once_ab.toml (copy over renut.toml, play
    the same place for a minute or two each). They switch one item off and on every 4 seconds; the log's
    "ring thread A/B" line then gives the renderer thread's time with and without it from a single run. You may see
    the frame rate pulse every 4 seconds: that is the test.
  - If anything looks wrong (a seam across the middle of the screen, missing or stretched geometry):
    configs/banjo_emulation_items_off.toml is this build with the four items off (the fur fix stays).
  - Lines worth sending if they appear: anything with DIFFERENCE or MISMATCH; "tiles once: ... goes back";
    "persistent vertices: the arena is full".

Changes in the seventh package (2026-10-13 afternoon: after your first console run)
------------------------------------------------------------------------------------
  COPY banjo-nx.nro AND renut.toml. Same shader package and prewarm list.

  Your run (15-18 fps at 2397/1305/2733): thank you, the logs were what was needed. What they showed and what is
  in this build:
  - Everything new ran correctly on the console: the boot check passed (248 of 248 on the AArch64 forms), the
    driver's per-draw paths were used and checked themselves, no error line.
  - Where the time goes: the GPU spends about 30 of its 56 ms a frame on emulated EDRAM transfers, not on drawing;
    the renderer thread draws everything twice (the game's two screen tiles) and copies 345 MB of vertex data a
    second. Those are the next pieces of work; this package does NOT contain them yet, so expect the same frame rate.
  - A compiler bug the Peggle 2 port found on its console (GCC's ARM register allocation pass dropping instructions)
    is switched off for the whole build. Banjo's binary showed no sign of it, but the trigger was there.
  - Two texture corrections: samplers now take the filter the shader's own fetch instruction asks for
    (banjo_native_tfetch_filters), and the Xbox-only CTX1 normal maps are decoded exactly instead of being squeezed
    into BC1 (more texture memory: 56 MB instead of 14 MB in the main menu).
  - Banjo's fur: the dark rectangles are a real renderer bug (the original emulated renderer draws it smooth), on
    PC too and since the first native builds. (Fixed in the eighth package, above.)

Changes in the sixth package (2026-10-13 morning: the SA2 port's three CPU rounds)
-----------------------------------------------------------------------------------
  COPY banjo-nx.nro AND renut.toml (the toml only gained keys; your old one works too, the new keys default to on).
  Same shader package and prewarm list as the fourth package. Not built with LTO or PGO: those are for the release
  build, as you said.

  What is in it (checked on PC only; nothing of it has run on a console yet):
  - The SA2 port's three CPU rounds, which its 1.0.1 shipped with ("better by about 15 %" there, together with a
    new PGO profile). Almost all of it is how the game code is built, so it is in the NRO, not in the toml:
      * The game switches the FPU between two modes 868,000 times a second on PC (SA2: 80,000). The checks the code
        flow makes unnecessary are gone and many conversions no longer need a mode: 540,000 switches a second.
      * 10,180 loops and 1,406 small functions keep their registers in CPU registers instead of memory.
      * 18,156 loads of constants from the game's read-only data are constants now; 24,329 float loads take a
        shorter path; 53,881 vector byte swaps no longer need a table.
      * 187 of the game's busiest indirect calls go straight to their usual target (58 % of those calls).
      * No frame pointer in the game code (frees a register in 27,000 functions); divisions as one instruction.
      * Runtime: no barrier after every lock operation, no thread lookup per critical section (24,000 a second),
        no wake-up system call for an event nobody waits on.
  - The graphics driver's own per-draw fast paths are switched on for the first time (banjo_gpu_nvk_draw_contract).
    Earlier builds asked for them under a name this driver does not export, so they were never used. Each one
    checks itself inside the driver and turns itself off if it ever differs. Look for "C6 NVK per draw:
    improvements requested" in the log; a line with MISMATCH there is worth sending.
  - A renderer fix from SA2's 1.0.1 (black speckles there): a texture the game allocates just above a freed
    render-target copy gets its own image (banjo_native_partial_resolve_inside).
  - Long sessions (added to this package later the same day; found by the SA2 port from reports of SA2 and NFSMW
    going wrong after one to two hours: shadows glitch, then black textures and missing objects until a restart).
    The renderer kept every render-target copy the game ever made; they are now released two minutes after the
    game stops using them (banjo_native_resolved_retire_idle). Banjo keeps what it must: its shadow atlas and
    things it draws once and keeps showing stay, up to 128 MB in all. This has not been seen in Banjo because
    nobody has played it that long on a console yet. In a long session's log, the line to read is
      [native] targets: N resolved textures and N parked hold N MB
    (it should stay around 30-130 MB instead of growing all session).
  - The boot check is wider now: look for
      [enhancements] instruction forms self-check: 248 of 248 results as expected (the AArch64 forms)
    If it says DIFFERENCE, send the log (it also leaves banjo_selfcheck_failed.txt next to the NRO).
  - The profiler log now measures what one FPU mode switch costs on the console ("FPCR on this CPU"): one number
    I need from your logs, it decides one of the settings above.
  A/B: configs/banjo_cpu_rounds_off.toml turns the three run-time keys of the rounds off; banjo_noncpu_off.toml
  turns the driver paths off with the other GPU-side items.

Changes in the fifth package (2026-10-12: a rounding correction)
----------------------------------------------------------------
  Only banjo-nx.nro changed against the fourth package (same shader package, same prewarm list).
  If you have not copied the fourth package yet, copy everything as its section below says.

  - Correction: the game's "convert a float to an integer, rounding" instruction (fctid; 425 places in the game code,
    310 of them "round this float to a whole number") rounded exact halves the wrong way on the Switch: 0.5 -> 1 and
    2.5 -> 3, where the Xbox 360 and the PC build give 0 and 2 (halves to even). Every Switch build before this one
    had it; the PC build never did, which is why PC tests could not show it. Found by the SA2 port's audit
    (fcti_inline in app/renut_config.toml). Not a speed change: the same number of instructions per conversion.
    The boot log proves it on the console (the sixth package's wider check, above, includes it).
  - The game code is no longer compiled with -fno-trapping-math (only -fno-math-errno). The SA2 port's audit: that
    flag allows the compiler to move a float operation across one of the recompiled code's FPU mode switches. No wrong
    result was ever seen from it; it is dropped because it could not be shown exact, and it was worth under 0.5 %.

Changes in the fourth package (2026-10-07: "all of the SA2 port's optimisations")
---------------------------------------------------------------------------------
  COPY ALL THREE: banjo-nx.nro, banjo_shaders.mesp + .mesp.idx (the shader package changed) and
  cache/masseffect_native_pipelines.bin (the new first-boot list). The first time you reach each area past the intro,
  its loading screens can take longer while the console builds that area's pipelines once (the new shaders have new
  pipeline keys); after that it is cached.

  From the SA2 port, each with its own key in renut.toml (all on; checked on PC: verify run with 0 differences, every
  new path in use, 0 rejected draws, and videos of the movies, title, menus, intro cutscenes and Spiral Mountain):
  - GPU: resolves (Banjo makes ~30 a frame) are drawn on the 3D engine instead of the copy engine, which on the
    Switch's driver costs an engine switch and a wait every time (banjo_gpu_resolve_draw).
  - GPU: Direct3D's "clear depth and color" through a 4x view is done straight in the 1x targets instead of drawn and
    converted (banjo_gpu_edram4_clear_color0).
  - GPU: the EDRAM conversion budget is 8x bigger, so the renderer no longer forces a full CPU<->GPU drain about once
    a frame (31 times per 10 s on PC before, 0 now).
  - GPU: push constants once per pass instead of per draw, narrower barriers at the end of each render pass, the
    handheld GPU profile (460.8 MHz, as Unleashed/Marathon/Mass Effect use; nothing changes when docked).
  - GPU, new shader package: transparent pixels that would not change the image are skipped before blending; vertex
    and pixel shaders stop testing things the renderer knows per pipeline (texture signs, texcoord swaps, input
    remaps, the vertex index for 301 vertex shaders that never read it).
  - CPU: vertex buffers bound once per command buffer instead of once per draw (0.99 -> 0.05 binds per draw), the
    EDRAM plan of a draw reused when it repeats, fewer string allocations on the ring thread.
  - Switch runtime (from the SA2 port): finished threads are cleaned up and their 16 MB stacks reused (SA2 ran out of
    threads at a loading screen without this, and lost up to a second per frame to heap fragmentation).
  - The log file is written by a thread of its own (from the Futari port): the ring thread no longer waits for the SD
    card (Futari's console stutters of 250-1000 ms came from that).
  A/B configs as before (configs/banjo_noncpu_off.toml turns the GPU ones off, banjo_cpu_off.toml the CPU ones), plus
  configs/banjo_logwriter_off.toml.

Changes in the third package (2026-10-07)
-----------------------------------------
  - Game code, two codegen changes from the other ports (one NRO, as always):
      * the game's "callee-saved" CPU registers are kept in the host CPU's registers instead of memory, and the
        register save/restore helper calls (35,300 of them in the code) are gone (the "diet" the Mass Effect, SA2 and
        Sonic Transformed ports use; Mass Effect measured -10 % main thread). The game code is 5 % smaller (NRO 66.1 -> 62.8 MB).
      * floating-point loads/stores no longer switch the CPU's floating-point mode: 9,132 of 40,120 mode switches
        gone (each is a slow instruction on the Switch's CPU).
    Banjo needed extra care: the game keeps vector constants in registers 64-77 for its whole run and passes one
    value in register 92, outside the normal rules; those registers stay as before (without that, every video was
    garbage). Checked on PC: verify run with 0 differences (the native reverb bit-exact over 63,000 calls), videos of
    the startup movies, title, menus, the intro cutscenes and the first minutes of Spiral Mountain.
    Please compare fps / the profile with the second package if you can (its NRO is
    D:\Banjo kazooie port\builds\2026-10-07_0535_optimised\renut.nro), and watch the intro movies: if a movie or a
    character looks wrong, that is this change.

Changes in the second package (2026-10-07)
------------------------------------------
  - OPTIMISED CODEGEN (the whole game's recompiled code, from the SA2 / Mass Effect ports): guest flags kept in CPU
    registers, guest memory accessed without "volatile" (Mass Effect measured +15-20 % fps on the console), and
    129,000 calls between game functions made direct so the compiler can inline them. The startup log's
    "[enhancements]" line now starts with "optimised codegen". Same game behaviour: checked on PC (video of the route,
    verify run with 0 differences); one function that needed special handling was found and marked.
  - Ring thread (same image, each with its own key): shader-object bookkeeping without per-thread lookups
    (banjo_note_shader_fast), render-target lookups from a memo (banjo_ring_target_memo), and the frame pacing wait
    sleeps until the next vblank or submission instead of waking every 50 us (banjo_ring_wait_vblank).
  - Vertex copies: a draw whose vertices lie inside a bigger copy of the same buffer made earlier in the frame reuses
    it (banjo_ring_vertex_containment, from the Sonic Transformed port): ~25-30 % fewer vertex bytes copied on PC.
  - Index cache: draws matched by address within a synchronization epoch instead of hashing every draw's indices
    (banjo_ring_index_epoch; checked against the hash on 382,754 hits on PC, 0 differences).
  - Settings from the SA2 port's console tests: the upload buffer in CPU-cached memory (masseffect_native_upload_memory
    = 1, about twice the write speed there) and hardware-CRC fingerprints (masseffect_native_fingerprint_mode = 2).
  - The game code is compiled with -fno-math-errno (as Mass Effect ships; same results). -fno-trapping-math was
    also on until the fifth package.
  - Correctness fix in the renderer's same-frame reuse of vertex/index copies (banjo_ring_sync_fences): it never knew
    when the game synchronized with the GPU (the shared renderer never counted those points), so a buffer the game
    rewrote mid-frame could have been reused stale. Now exact; less reuse than the unsafe version, still a saving.
  - The log has a new "[native] Draw stages" line (where a draw's time goes) and a new "[native] WAIT_REG_MEM waits" line (what the ring waits for) and a "vertex containment" line.

Earlier changes (2026-10-07, first package)
-------------------------------------------
  - Ring thread (the GPU command thread), same image, each with its own key in renut.toml:
      banjo_no_draw_pairing = true   the per-draw "pairing" search inherited from Mass Effect never matched a draw in
                                     Banjo; it is skipped (less work on the game threads and on the ring).
      banjo_ring_edram_memo = true   a draw with the same render targets as the draw before reuses its EDRAM
                                     preparation (from the SA2 port; ~95 % of draws).
      banjo_direct_state = false     the "direct renderer" step 1 (draw state handed to the ring without PM4 packets).
                                     Same image (checked on 3.3 million draws), but not faster on PC, so it is off
                                     until the console measures it (below).
  - The log now says how long the ring thread works per frame: "[native] ring thread: X ms of work per Swap (Y us per
    draw ...; Draw/Copy ..., the rest ...)".
  - Two new configs measure an enhancement on and off inside ONE run (it switches every 4 s); the log's
    "[native] ring thread A/B" line has both numbers:
      configs/banjo_direct_ab.toml   banjo_direct_state
      configs/banjo_memo_ab.toml     banjo_ring_edram_memo

Known issue: on PC this build crashed once at boot (about 4 s in) in ~57 starts; it did not happen again in 56 tries.
If the game closes by itself while booting, just start it again, and please send that run's logs/ folder.

Suggested test for this build: one run with the normal renut.toml (boot to the first level, play a minute), then the
same route with configs/banjo_direct_ab.toml and with configs/banjo_memo_ab.toml (rename logs/ between runs).

Earlier changes (2026-10-06 build)
----------------------------------
  - GPU DRIVER: earlier builds accidentally linked an older generic NVK driver (Mesa 26.2.1) instead of the
    nfsmw one the port is set up for (Mesa 26.2.2). This build links the nfsmw driver, rebuilt with one extra
    switch: per-stage shader caching (a shader shared by several pipelines is compiled once, not once per
    pipeline). masseffect_nvk_cache_per_stage = true in renut.toml turns it on.
  - Compiled pipelines are saved to cache/ every 20-45 s (the last test saved nothing: it was closed before the
    first save). The second boot of the same scenes starts from that cache.
  - One background pipeline compile thread instead of two: on the console two compile threads mostly waited on
    each other (one shared memory-allocator lock) and slowed the game's own loading.

What changed since the first test (the "jiggy screen" hang)
------------------------------------------------------------
The jiggy screen is the game's loading screen. It stayed up because the GPU ring thread was compiling a new
pipeline every ~0.5 s and the game waits for the ring. On PC the same thing happens with an empty pipeline cache
(~150 ms per pipeline there, the jiggy screen for ~50 s). Fixes, all with the same image:
  - Shader library rebuilt with compile-time constants: pixel shaders are about half the size for the driver to
    compile (62 MB -> 35 MB library). Rendering checked identical on PC.
  - Pipeline prewarm: cache/masseffect_native_pipelines.bin now holds the list of ~1,300 pipelines the PC runs
    met (title, menus, intro). Two lowest-priority threads compile them from boot in idle CPU time, so the ring
    finds them ready. The file grows with the console's own compiled pipelines after the first run.
  - Mesa's own SD-card shader cache is off (it held a lock the ring thread waited on mid-compile).
  - The game threads that spun waiting for the GPU (three at ~55 % CPU each) now sleep until the ring advances
    (all six of the game's GPU wait loops).
  - The ring thread's fast paths from Mass Effect's tuned config are on (each checks itself against the slow path
    and logs DIFFERENCE if they ever disagree), and the ring records Vulkan commands on a worker thread.
  - The jiggy loading screen's own shaders were missing from the library (it never showed on fast PC runs): added.
  - XAudio's reverb (a quarter of a core all the time) runs as native ARM code, bit-identical to the recompiled
    one (36,000 calls compared). banjo_cpu_verify.toml also checks it against the original on every call.
  - The game code is now compiled without FMA fusion, as the SDK intended: the console's floating point matches
    the PC build and the Xbox 360 (before, GCC could fuse separate multiplies and adds).
FIRST BOOT: the GPU pipelines are compiled on the console the first time each scene needs them (hundreds for the
title, intro and first level; a few hundred ms each). The jiggy loading screen stays up while that happens: please
let it run (several minutes the first time). Progress is now saved to cache/ every 20-45 s, so even if you close it,
the next boot starts where it stopped; the second boot of the same scenes should be fast. Do the first boot with the
normal renut.toml or banjo_cpu_on.toml, not banjo_cpu_verify.toml (verify runs every check twice and is slower).

Every enhancement can be switched off in renut.toml (banjo_cpu_enhancements / banjo_noncpu_enhancements are
the master switches); the log has an "[enhancements]" line saying which ones ran.

configs/ has ready-made variants of renut.toml (copy one over sdmc:/switch/banjo-nx/renut.toml to use it):
  banjo_cpu_on.toml      = the normal renut.toml
  banjo_cpu_off.toml     = every CPU enhancement off (A/B comparison)
  banjo_noncpu_off.toml  = every non-CPU enhancement off (prewarm, Mesa cache, per-stage cache)
  banjo_all_off.toml     = both groups off
  banjo_profile.toml     = normal + the app's own sampler as well (logs/rex/banjo_profiler.log)
  banjo_cpu_verify.toml  = every CPU enhancement checks itself against the original code on every use (slower).
                           The log's "[verify] ... differences: N" lines must say 0. For one test session.
  banjo_direct_ab.toml   = normal + banjo_direct_state switched off/on every 4 s ("ring thread A/B" line)
  banjo_memo_ab.toml     = normal + banjo_ring_edram_memo switched off/on every 4 s
  banjo_upload_uncached.toml = normal but the upload buffer without CPU cache (the A/B of the SA2 setting)
The CPU and GPU profilers are ON in renut.toml and in every config except banjo_cpu_verify.toml: each run writes
logs/rex/rex_profile.log (CPU samples) and the per-category GPU times in the renut log.
If the SD card still has logs/rex/stacks_profile.flag from an earlier package, delete it: that file forces CPU
sampling even in the verify config (profiling is now switched by renut.toml).

Other configs: banjo_cpu_off.toml (every CPU enhancement off) for an A/B of all of them, banjo_cpu_verify.toml (every
check on every use; the "[verify] ... differences" line must say 0). Rename or copy logs/ between runs.

After each run, copy back:
  sdmc:/switch/banjo-nx/logs/            (the whole folder: renut log and logs/rex/rex_profile.log)
  sdmc:/switch/banjo-nx/shader_misses/   (if it exists: shaders the library does not have yet)
