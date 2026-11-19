banjo-nx Switch test build (Renut's renderer on deko3d; no Vulkan driver since package 36m)
=========================================================================================

SD card layout:
  sdmc:/switch/banjo-nx/banjo-nx.nro
  sdmc:/switch/banjo-nx/renut.toml
  (banjo_shaders.mesp, banjo_shaders.mesp.idx and cache/masseffect_native_pipelines.bin were the native
   renderer's; since 36m it cannot run, so they are not needed any more. Leaving them there is harmless.)
  sdmc:/switch/banjo-nx/pipeline_list.bin         <- NEW (package 26): the pipelines Renut's renderer builds ahead
  sdmc:/switch/banjo-nx/deko3d_shaders.bin        <- NEW (package 29, replaced in 33): compiled shaders of the deko3d
                                                     renderer (next to the zip in the package folder, not inside it:
                                                     game data)
  sdmc:/switch/banjo-nx/game_root/     <- copy everything from "D:\Banjo kazooie port\game\" here
                                          (Bundle, Debug, loctext, default.xex, ...; $SystemUpdate is not needed)

Launch with a title takeover (hold R while starting a game, then open the Homebrew Menu), or a forwarder
with a 39-bit address space. From the Album it will not have enough memory.

Latest changes (this build, 2026-10-22, package 36z7: textures released when memory runs short)
-----------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (NRO_SIZE bytes, md5 NRO_MD5...), renut.toml and renut_auto.toml (the configs folder is unchanged
  from 36z6 but has the new key too: replace it).
  Your 36z6 crash in the town (std::bad_alloc): the memory ran out (2,903 of 3,090 MB in use, 744 MB of it textures the
  renderer keeps for the GPU, climbing by ~870 MB in the last 90 s while the town loaded). The renderer is meant to
  release textures that have not been used for a while when memory runs short, but on the Switch it never did: the
  rule compared against a "GPU memory budget", and on deko3d that budget was reported as 0, so the rule never fired.
  Now the budget is the heap's real room (on the Switch textures, buffers, the game's memory and everything else share
  one heap), and:
  - with less than banjo_renut_textures_reserve_mb = 512 MB of room left, textures unused for 120 frames are released
    (checked every 32 frames);
  - with less than half of that, textures unused for 8 frames go, checked every second whatever the frame rate (your
    town load ran at 0.5-4 fps, where 32 frames were up to a minute).
  A released texture is made again from the game's memory when it is drawn (a short upload, the same picture).
  Emulator: forced to release all the time (reserve 16 GB), the route ran at 26-30 fps without a problem.
  Your crashed run also had the CPU profile on: the main game thread was 41 % in the new copies and 33 % in plain
  generated code (it was about 70 % generated code before 36z6).

Package 36z6 (2026-10-22: the Bejeweled 3 port's CPU method)
------------------------------------------------------------
  (36z6 files: banjo-nx.nro 88,623,301 bytes, md5 401455fd050f...; cache 60.)
  Your request: the four steps of the Bejeweled 3 port's method, from your console profiles (the seven runs of
  2026-10-21, packages 36u to 36z3).
  1. Write back only the registers a function changed: Banjo's copies already did this (nothing to add).
  2. Bulk copies: 360 more game functions run as "localized copies" (the game's own code with its registers kept in
     the processor instead of memory), every function your profiles found hot that still ran as plain generated code
     (at least 8 samples over the seven runs; they hold 91 % of the samples a lower bar would take). Left out: tiny
     functions (already inlined), forms the copy tool cannot make, the five script interpreters (already optimised a
     different way).
  3. Small functions copied into the functions that call them: Banjo's copies already did this; inside the new copies
     2,273 calls go to such inlined copies.
  4. No self-check on the copies in play: every copy is called directly (before, each call counted down to its next
     check). configs/banjo_hot_guard_on.toml puts the self-check back.
  Checked in the emulator, with the copies compared against the original code: 22,975,090 calls compared, no
  difference (verify mode), and with the self-check on the whole route, none either. On the way the comparison found
  a bug in the self-check itself (it did not record one kind of store, the copies were fine): fixed.
  A/B: configs/banjo_cpu_native_bulk_off.toml (the 360 back to the generated code). The PGO profile was taken again
  in the emulator for this code (5 minutes, every profiler off, as you asked). Please send the logs of a run through the busy town: rex_profile.log shows how much
  of each thread is in the copies now.

Package 36z5 (2026-10-21: controls from SA2, two players, the Switch keyboard)
-------------------------------------------------------------------------------
  (36z5 files: banjo-nx.nro 80,644,293 bytes, md5 b21aa97b71b4...; cache 60.)
  deko3d_shaders.bin is unchanged (36z3's cache 60). Your renut.toml must be the new one: the controls are in it.
  1. Controls (sa2-nx's controller code, as you asked):
     - Xbox layout: the face buttons go by position, as on the 360 pad (input_xbox_layout = true): the bottom button
       (B on Switch controllers) is the Xbox A, the right one (A) is B, the left one (Y) is X, the top one (X) is Y.
       The settings menus follow it. false = by letter.
     - Two players (up to four): the handheld and every connected controller are separate players, in the order they
       connect (before, the handheld and the first controller were both player 1). L+R+ZR opens the system's
       controller screen: connect a second controller, change the order, or split a Joy-Con pair into two players.
       Single Joy-Cons are held sideways (input_joycon_sideways = true). The log says "controller: Pro Controller
       (No1) is player 1" and so on whenever this changes.
     - Button maps for player 1 and player 2: the [input_p1] and [input_p2] sections at the END of renut.toml. Each
       line names the Switch buttons for one Xbox button, "" = the default, e.g. a = "B, ZR". The log lists the ones
       set ("input_p1_a = B, ZR") and warns about a name it does not know (that button keeps its default). Keep the
       sections last: every line after [input_p1] belongs to it.
  2. The game's keyboard (naming things, e.g. a vehicle) is now the Switch's own software keyboard: type with the
     touch screen or the controller, + or OK accepts, its Cancel leaves the name as it was. It shows the game's
     title and description above the field when the name is 32 characters or less, otherwise the title inside the
     empty field. switch_software_keyboard = false puts back the old box (which cannot be typed into on the console).
     Checked in the emulator: the game's own call path, an accepted name (with accented letters, cut to the game's
     15 characters) and a cancel, the game running on afterwards. Not yet seen on the console: please try one name
     and send the log (lines "XamShowKeyboardUI: Switch keyboard ..." and "Switch keyboard: accepted/cancelled").

Package 36z4 (2026-10-21: the background shader compiles fixed for memory)
--------------------------------------------------------------------------
  (36z4 files: banjo-nx.nro, md5 6cfbf4b125a4...; cache 60.)
  deko3d_shaders.bin is 36z3's (cache 60: put it back if you are still on the generic-only test cache).
  Your call: the profiler window and the FPS counter are off in renut.toml, renut_auto.toml and every config
  (banjo_show_profiler = false, banjo_show_fps = false: the window cost ~7 % of a core). The logs are written as before.
  Your 36z3 crash (with the generic-only cache): the memory ran out while loading an area (2,980 of 3,098 MB). The
  pipeline list's prebuild asked for nearly every shader variant at once and each waiting compile kept its own copy of
  the shader's text (~50 KB): thousands of them. Now:
  1. a waiting compile keeps a few hundred bytes (the shader's text is shared, made into the variant only when it is
     compiled);
  2. the variants your draws need are compiled first; the prebuild's own requests wait behind them, at most 256 at a time
     (the rest when a draw needs them).
  Emulator, with the generic-only cache: the whole route, no crash, the memory as with the full cache (2.3-2.5 GB), and
  more variants swapped in than before (712 against 390). With cache 60 nothing compiles in play.
  The normal footprint (your 36z2 log): about 2.4 GB of the 3.1 GB heap: the 360's 512 MB, the GPU copies of textures and
  targets (~870 MB), buffers (160 MB), code and shaders (~120 MB), and the heap's free pieces.

Package 36z3 (2026-10-21: no draw waits for a shader compile)
-------------------------------------------------------------
  (36z3 files: banjo-nx.nro 80,521,413 bytes, md5 99055a30ea72...; cache 60, 21,961 variants, 158 MB.)
  Your 36z2 freeze: loading the next area, a draw waited for a shader the cache did not have, and the console's shader
  compiler never finished it (two minutes in its optimiser, 100 % of a core): the frame never came. Your session also
  compiled 375 shaders while playing (a stutter each), in the areas past the test route.
  1. A draw never waits for a compile now (banjo_deko3d_async_variants = true): a shader variant missing from the cache
     is compiled in the background while its draws use that shader's generic variant (the same picture, a little
     slower for those draws), and they switch to their own variant once it is ready. A compile that never ends only
     leaves its draws on the generic variant.
  2. deko3d_shaders.bin has the generic variant of every shader of the game (4,881 shaders, all areas) plus every
     variant your console compiled and every one of your pipeline list: new areas run without a single wait.
  3. configs/banjo_deko3d_async_variants_off.toml: the old way (draws wait), for comparison.
  The [renut] "deko3d:" line every 10 s counts it: "N stages given the generic variant meanwhile, M compiled in the
  background, K swapped in".

Package 36z2 (2026-10-21: 864p docked, any height in the config, docked white line fixed)
------------------------------------------------------------------------------------------
  (36z2 files: banjo-nx.nro 80,509,125 bytes, md5 d761a2f30408...; cache 58, then 59.)
  Your 36z run: docked 24 fps, GPU 41.3 ms a frame where handheld held 30 (33.0 ms). Docked the GPU clock is 768 MHz
  but the memory stays at 1600 MHz, so the memory-heavy passes (resolves, SMAA, upscale, present) grow with the pixels;
  and Renut's extra mip level for every texture drawn above 1x cost 4 ms a docked frame (handheld never has it).
  1. Docked 3D image at 1536x864 (banjo_renut_render_height_docked = 864), as you asked.
  2. Any height for either mode in renut.toml: banjo_renut_render_height_handheld / _docked, 360 to 1080 (rounded to
     a multiple of 9: 648, 702, 756, 810, 864, 900, ...). Read at start.
  3. The white line at the top (and left) of the docked picture: fixed (the game's full-screen draws missed the first
     row and column above 1x; emulator check: the edge rows match their neighbours now).
  4. A small 128x128 render target (ten draws a frame in your log) was cut to 115x115 in handheld: fixed.
  5. Your call: no extra mip levels above 1x in renut.toml, renut_auto.toml and every config
     (banjo_renut_host_mips = false; about -4 ms a docked frame; NOT exact docked: the blur passes read the full-size
     image only, as at 1x and in handheld; no effect in handheld). configs/banjo_renut_host_mips_on.toml puts Renut's
     mip levels back.

Package 36z (2026-10-21: the 3D image at 648p handheld / 900p docked, the HUD at full res)
------------------------------------------------------------------------------------------
  (36z files: banjo-nx.nro 80,582,853 bytes, md5 415e94c201f8...; cache 57.)
  Not in this package: deko3d_shaders_f447.bin (it was made from the old shaders; only
  configs/banjo_deko3d_uam_wide_rows_off.toml used it: with 36z that config compiles its shaders while playing).
  Your request, NOT exact (renut.toml, renut_auto.toml and every config but one):
  1. The game's 3D image is drawn at 1152x648 in handheld mode (0.81 of the pixels) and 1600x900 docked
     (banjo_renut_render_height_handheld = 648, banjo_renut_render_height_docked = 900).
  2. The HUD, menus and text stay at the display's resolution: 1280x720 handheld, 1920x1080 docked (the swapchain is
     made at 1920x1080 when docked, as sa2-nx does: banjo_renut_display_height_docked = 1080). Right after the game's
     final composite and SMAA the image is scaled up once, bilinear as you asked (banjo_renut_upscale_filter = 0 in
     every config: one sample a pixel, the cheapest form; 1 = bicubic, sharper), and the HUD is drawn over the
     full-size copy. Menus and loading screens (no
     3D composite) are drawn at the display's resolution whole. Shadow maps and the blur buffers keep the console's
     sizes. Docking and undocking switch at the next frame.
  3. The menus' 3D icons (the item bar of the single-player menu, drawn after the composite with a depth test) get a
     depth buffer at full resolution too, filled from the scene's in those frames only ("UI depth upscale" in the GPU
     profile); gameplay frames never need it.
  4. configs/banjo_renut_native_720p.toml is the game's own 1280x720 everywhere (as 36y), to compare. The GPU
     profile has a line "UI upscale"; the [renut] line "internal resolution" every 10 s counts the frames.
  Emulator checks: handheld (1152x648 under a 1280x720 HUD) and docked (1600x900 under 1920x1080, the swapchain made
  1920x1080) through the menus, the intro and the first dialogues; HUD text sharp, nothing cut at the edges.
  Runs: renut_auto.toml on the scripted route, handheld (fps and GPU ms against your 36y run), and a look docked.

Package 36y (2026-10-21: SMAA back to 36v's form; the dialogue portrait lighter)
--------------------------------------------------------------------------------
  (36y files: banjo-nx.nro 80,451,781 bytes, md5 87ceedbfd9ba...; cache 56.)
  Your 36w run: SMAA 3.12 ms a frame on average, more than 36v's 2.79. Its new per-pass lines: edges 1.46, weights
  0.88, blend 0.36, copy 0.27. The edge pass costs the same whether it writes every pixel or a sixth of them (it is
  bound by its five texture reads a pixel), so 36w's extra copy pass was a loss:
  1. SMAA is 36v's three passes again, with 36w's mask direction and per-pass lines ("SMAA edges", "SMAA weights",
     "SMAA blend"). The same pixels as SMAA itself (emulator check).
  2. 75 % fur shells as 36x (your call).
  3. Your call, NOT exact: the dialogue portrait (the 96 x 96 box) with half its fur shells (11 of 21) and 2x MSAA
     instead of the game's 4x (banjo_renut_portrait_msaa_samples = 2; 1 = no MSAA). configs/banjo_renut_portrait_full
     .toml puts both back; banjo_renut_fur_shells_all.toml draws every shell of the main fur again.
  Runs: renut_auto.toml on the scripted route: the heavy dialogue windows' fps and GPU ms against your 36w run.

Package 36x (2026-10-21: 75 % fur shells; SMAA checked against SMAA itself)
----------------------------------------------------------------------------
  (36x files: banjo-nx.nro 80,447,685 bytes, md5 1c5f227dd9a1...; cache 56.)
  1. Your call, NOT exact: banjo_renut_fur_shells_percent = 75 in renut.toml, renut_auto.toml and every config: three
     of every four fur shells are drawn (Banjo: 16 of 21, his outermost shell drawn, so the fur keeps its length;
     the dialogue portrait's too). The fur family was about 18 ms of the heaviest frames. The [renut] line "fur
     shells ... N drawn, M not drawn" every 10 s counts it. configs/banjo_renut_fur_shells_all.toml draws every
     shell again (with SMAA, as 36w).
  2. SMAA is 36w's. The comparison frames (configs/banjo_renut_smaa_compare.toml) now run SMAA's own unmodified
     shaders in the same frame and count the pixels that differ: emulator, 24 frames, 0. A further idea, sharing
     lumas in each 2x2 pixel quad (3 texture reads a pixel instead of 5), was tried and dropped: it differed.
  Runs: renut_auto.toml on the scripted route (fps, GPU, the four "SMAA ..." lines and the fur family's lines
  against your 36v run).

Package 36w (2026-10-21: SMAA cheaper again, the same pixels)
-------------------------------------------------------------
  (36w files: banjo-nx.nro 80,435,397 bytes, md5 9bf79bf2107c...; the rest as 36u.)
  Your 36v run: SMAA 2.47 ms a frame on average (36u 3.77). The edge pass still wrote three things for every pixel;
  now only the pixels SMAA can change write anything (about a sixth of the screen in gameplay):
  1. the edge pass writes edges and the mask only there (the rest cleared, which is cheap on the Switch's GPU);
  2. no full copy of the picture any more: the blending reads the game's image and a small masked pass copies the
     changed pixels back;
  3. the mask set up so the GPU's coarse depth culling can skip whole untouched tiles in the later passes.
  The same pixels as 36u/36v (emulator check: the full passes run in the same frames, 0 pixels differ).
  The GPU profile now has a line per pass: "SMAA edges", "SMAA weights", "SMAA blend", "SMAA copy".
  Runs: renut_auto.toml on the scripted route; compare the four SMAA lines' sum with 36v's "SMAA".

Package 36v (2026-10-21: SMAA cheaper, the same pixels)
-------------------------------------------------------
  (36v files: banjo-nx.nro 80,427,205 bytes, md5 5d4257c9f511...; the rest as 36u.)
  Your 36u run: SMAA cost 3.3-5.3 ms of GPU a frame (the t220 scene 30.0 -> 25.9 fps). Now:
  1. the expensive passes only where they matter: the edge pass writes a mask, the blending-weights pass runs only
     on pixels with an edge and the blending pass only on pixels it can change (about 12 % of the screen in
     gameplay); the rest are skipped by the GPU before their shader runs;
  2. one pass and one GPU barrier less: the copy of the picture is written by the edge pass itself;
  3. the edge detection without its clamping arithmetic (the GPU sampler clamps).
  The same pixels as 36u: emulator run with configs/banjo_renut_smaa_compare.toml (it now also runs 36u's full
  passes in its frames and compares): 23 frames, 0 pixels differ.
  Runs: renut_auto.toml on the scripted route (the GPU profile's "SMAA" line and the fps of the heavy windows
  against your 36u run), then configs/banjo_renut_smaa_ab.toml for SMAA's exact cost.

Package 36u (2026-10-21: SMAA anti-aliasing before the HUD)
-----------------------------------------------------------
  (36u files: banjo-nx.nro 80,423,109 bytes, md5 93ff1515c5d0...; cache 56.)
  Your request: SMAA 1x (Jimenez et al.'s SMAA, MIT) instead of MSAA. It runs on the 3D image right after the
  game's final composite (bloom and depth of field done) and before the HUD is drawn, so the HUD and its text stay
  sharp. Every cutscene and gameplay frame; the title and menus have no such composite and stay as they are.
  banjo_renut_smaa = true in renut.toml, renut_auto.toml and every config (SMAA's medium preset; set it to false in
  the toml for 36t's picture). Configs:
    banjo_renut_smaa_high.toml     SMAA's high preset (smoother diagonals, more GPU time)
    banjo_renut_smaa_ab.toml       SMAA off and on every 4 s: its GPU ms a frame in the "[renut A/B]" lines
    banjo_renut_smaa_compare.toml  every 15 s the same frame before and after SMAA as capturas/smaa_NNN_off.tga
                                   and _on.tga (full size, no HUD)
  Emulator comparisons: builds/smaa_comparison_2026-10-21. The [renut] line "SMAA ... N frames" every 10 s says
  how many frames had it. Runs: renut_auto.toml, then configs/banjo_renut_smaa_ab.toml on the scripted route.

Package 36t (2026-10-21: the NRO built with PGO and LTO)
--------------------------------------------------------
  (36t files: banjo-nx.nro 80,087,237 bytes, md5 39b4c08ff8f8...; the rest as 36s.)
  Your request: the same sources as 36s, compiled with profile-guided optimisation (PGO) and link-time
  optimisation (LTO), sa2-nx's method and settings (docs/pgo-lto.md):
  - The profile: an instrumented build counted every branch of the game code, the app, Renut's renderer and plume
    during a 30-minute Ryujinx run of the scripted route (boot, menus, the pizza cutscene and its dialogue, then
    15 minutes of play in Spiral Mountain with A pressed every 6 s). The optimised build lays out and inlines by
    those counts; code the run never reached (other levels, vehicles) is optimised as before.
  - LTO: the game code, the app, the renderer and plume optimised as one program at the link.
  - The NRO is about 5.5 MB larger (inlining). The pictures are meant to be the same as 36s (same float rules:
    -ffp-contract=off, no fast-math); the emulator check showed them right.
  Runs: renut_auto.toml on the scripted route, as 36r/36s: compare the render thread's and the game thread's
  ms a frame and the fps of the heavy windows with your 36r run. If anything looks or plays differently from
  36s, use 36s's NRO and tell me.

Package 36s (2026-10-20 evening: the late input's GPU queue part removed)
-------------------------------------------------------------------------
  (36s files: banjo-nx.nro 74,541,253 bytes, md5 38b023548875...; deko3d_shaders.bin cache 56.)
  Your call: banjo_late_input_gpu and banjo_late_input_gpu_margin_us are removed from the code, the tomls and the
  configs (banjo_late_input_gpu_on/_ab/_margin_4/_8/_10). It was off by default in 36r already, so 36s runs like
  36r. What your runs showed (controller read -> GPU done, 36k/36r without it, 36m/36q with it): nothing at the
  30 fps cap; in scenes just able to hold 30 about 6 fps lost; a lower lag only where the frame rate was already
  far below 30. Plain late input (banjo_late_input) stays on. docs/deko3d-renderer.md section 25.
  Runs: as 36r (renut_auto.toml, then configs/banjo_gpu_ab_pack.toml on the scripted route).

Package 36r (2026-10-20 evening: the frame rate 36q lost, back)
---------------------------------------------------------------
  (36r files: banjo-nx.nro 74,545,349 bytes, md5 c8373cf39bdf...; deko3d_shaders.bin cache 56.)
  Your 36q run, against 36j/36k on the same route:
  1. The late input's GPU queue part (banjo_late_input_gpu, since 36l) is OFF again. In the scene where 36j/36k ran
     29.6-29.9 fps with 33.4 ms of GPU a frame, 36q ran 24.3 fps with the same GPU work: the controller read was
     held back 29 ms a frame (36j/36k: 8-11 ms), so each frame started too late and the GPU idled about 20 % (what
     you saw: not 30 fps, the GPU not busy, the CPU not either). It did nothing measurable at 30 fps.
     configs/banjo_late_input_gpu_on.toml puts it back. Plain late input stays on.
  2. Two of SA2's render-thread items are OFF: they cost Banjo's render thread more than they saved (your 36q
     profile: render thread 0.5-1.5 ms a frame slower than 36m; DrawStateLocked 9.5 -> 12.6 % and the shared
     constants' push 2.7 -> 4.9 % of it). banjo_renut_chain_reuse (with the replayed draws) and
     banjo_renut_inline_line_compare; configs/banjo_renut_chain_reuse_on.toml and _inline_compare_on.toml for A/Bs.
     The rest of SA2's work stays on (the game thread's device copies did get cheaper).
  Expected: 36j/36k's frame rate in the GPU-bound scenes, the render thread at 36m's or a little under.
  Runs: renut_auto.toml, then configs/banjo_gpu_ab_pack.toml (36q's notes below) on the scripted route.

Package 36q (2026-10-20 evening: the GPU A/B pack, on top of 36p)
-----------------------------------------------------------------
  (36q files: banjo-nx.nro 74,545,349 bytes, md5 38d7a82d7c97...; deko3d_shaders.bin cache 56.)
  0. IMPORTANT, two fixes: 36o and 36p can hang. In the emulator two of today's CPU items hung the game:
     - SA2's core layout (banjo_renut_core_layout: the game's drawing thread alone on core 0) hung the first frame in
       4 of 12 runs with it on (36o and 36p have it on);
     - the Futari port's Begin*Vertices change in 36p (banjo_renut_deferred_chain) hung it within the first minute:
       N&B draws on two Direct3D devices and that change only works with one, so a draw could get the other's state.
     Both are off in 36q (0 hangs in the runs since). The rest of SA2's CPU work stays on. If you want to try the core
     layout on the console anyway: configs/banjo_renut_core_layout_on.toml.
  The GPU work of this evening: the heaviest scene's shaders were read instruction by instruction and three
  bigger ideas were tried in the emulator; none of them pays in N&B (docs/deko3d-renderer.md section 24 says why),
  so they are not in the build. What is in it:
  1. One run measures two GPU options that were waiting for the console: configs/banjo_gpu_ab_pack.toml turns on
     banjo_deko3d_light_branch and banjo_deko3d_gpr32 (the fur family with 32 registers: more threads in flight
     for some extra memory traffic) and switches each off in turn every 4 s. Its log has a "[renut A/B]" line
     for each (target 15 light branch, 16 gpr32): GPU ms a frame on and off. Run it on the scripted route like
     renut_auto.toml (it has the same route); the heavy scenes are where it counts. The new cache 56 has the
     shaders with both on, so nothing compiles during the run.
  2. A question for you (not done, it changes pixels): the dialogue portrait's box is drawn with the game's 4x
     MSAA (disable_msaa only covers the main scene). Without it, 5 resolves a frame and their full GPU stalls go,
     and the portrait's fur is shaded once a pixel instead of up to four times. Say if you want a key for it.
  Runs: renut_auto.toml, then configs/banjo_gpu_ab_pack.toml, on the scripted route.

Package 36p (2026-10-20 afternoon: 36o plus Begin*Vertices draws in the chain)
------------------------------------------------------------------------------
  (36p files: banjo-nx.nro 74,537,157 bytes, md5 ef22c358aad7...; deko3d_shaders.bin was 36j's.)
  1. From the Futari port (your request there): the draws whose vertices the game writes after the call
     (Begin*Vertices) keep only what changed in the device too, instead of a whole 12.7 KB copy each. About 1,500
     per 10 s in N&B's play, so a small part; the emulator's check: 0 of them whole copies, every draw compared,
     0 differences. configs/banjo_renut_sa2_cpu_off.toml switches it off with the rest.
  Runs: renut_auto.toml and configs/banjo_renut_sa2_cpu_off.toml on the scripted route.

Package 36o (2026-10-20 afternoon: SA2's Renut CPU work, on top of 36n)
------------------------------------------------------------------------
  (36o files: banjo-nx.nro 74,528,965 bytes, md5 512fdcaf3706...; superseded by 36p, test 36p instead.)
  Your request: the SA2 port's work on Renut's CPU side, taken into banjo-nx (docs/renut-cpu-work-from-sa2.md).
  On SA2's console it took its render thread from 17.6 to 11.1 ms a frame and its game thread from 19.2 to 15.5.
  Same pictures: every item does the same work fewer times. Checked in the emulator with both verify keys:
  thousands of draws compared, 0 differences (one race found and fixed).
  1. The render thread's queue: woken every 16 draws instead of every draw, done signals only when someone waits,
     one clock read per batch, the next draw's data fetched ahead.
  2. A draw takes the previous draw's pipeline, targets, viewport, texture words and framebuffer when nothing they
     come from changed. SA2 did this for its immediate draws; banjo-nx also does it for the draws replayed from the
     game's recorded command buffers, which are most of N&B's draws in play.
  3. The shared constants compared with NEON 64 bytes at a time; fewer of them cleared and re-sent.
  4. Immediate draws keep only what changed in the device (about 5 KB instead of 12.7 KB).
  5. The game's drawing thread alone on core 0, the other game threads on cores 1-2 (they used to share cores
     in 10 ms slices). Its log line: "[renut] banjo_renut_core_layout: ...".
  What to look at: the "[renut] ... render thread busy X ms a frame" figure against 36n's (your 36m run: about
  17.5 ms at the 30 fps cap, 22.8 ms in the heavy scenes) and "input -> swap" in the [latency] lines.
  configs/banjo_renut_sa2_cpu_off.toml switches all of it off (the A/B); _chain_reuse_replays_off.toml and
  _core_layout_off.toml switch off one part; banjo_renut_cpu_verify.toml checks every draw (slower, for tests).
  Runs: renut_auto.toml and configs/banjo_renut_sa2_cpu_off.toml on the scripted route.

Package 36n (2026-10-20 afternoon: late input margin 6 ms, stacked textures, on top of 36m)
-------------------------------------------------------------------------------------------
  (36n files: banjo-nx.nro 74,455,237 bytes, md5 4ec415aa8393...; deko3d_shaders.bin was 36j's.)
  Your 36m run (same route as 36j, which had banjo_late_input_gpu off):
    at the 30 fps cap: controller -> screen 56 ms either way (the GPU is never behind there, so there is nothing to
    take); in the GPU-bound scenes: 113 -> 91 ms, but about 1 fps lower (22.1 -> 21.1; the GPU idle 4 % more).
  1. Your call: banjo_late_input_gpu_margin_us = 6000 (new key; 36m used 4000). The controller read is held back
     2 ms less, so the GPU runs dry less often: expected most of the lost frame back for a few ms of the 21 ms
     gain. configs/banjo_late_input_gpu_margin_4.toml is 36m's, _8.toml and _10.toml give more frame rate back.
  2. From the SASRT port: a stacked texture (several layers sampled as one) was seen by deko3d as its first layer
     only; plume's Vulkan side always read every layer. Fixed. The log says "[textures] first stacked (2D array)
     texture" / "first volume (3D) texture" if the game uses either (if neither line appears, nothing changes).
  Runs: renut_auto.toml on the scripted route.

Package 36m (2026-10-20 afternoon: no Vulkan driver, on top of 36l)
-------------------------------------------------------------------
  (36m files: banjo-nx.nro 74,447,045 bytes, md5 417e4bd713b2...; deko3d_shaders.bin was 36j's.)
  1. Your request: Mesa's Vulkan driver (NVK) is not linked into the NRO any more: 14.4 MB smaller (88.9 ->
     74.4 MB), with nothing of it left inside (checked symbol by symbol). The Vulkan entry points the code
     still names answer "no driver". Same pictures and speed: the deko3d renderer never used it.
  2. The NRO now runs only Renut's renderer on deko3d. A config asking for banjo-nx's native renderer or for
     Renut on Vulkan boots deko3d anyway and says so in the log ("needs Vulkan, which this build does not
     have"). Those 59 configs (banjo_renderer_native, banjo_renut_vulkan, the native renderer's A/B ones) and
     renut_normal.toml, banjo_diagnostics.toml and banjo_fragments.toml are left out of the package. Old copies
     on the SD card do no harm, but they would only run deko3d now.
  Runs: renut_auto.toml on the scripted route as before (36l's late input check still applies).

Package 36l (2026-10-20 afternoon: late input from the GPU queue, on top of 36k)
--------------------------------------------------------------------------------
  (36l files: banjo-nx.nro 88,881,349 bytes, md5 0bf92a198198...; deko3d_shaders.bin was 36j's.)
  Your 36k run: the latency trim held back 91 frames and every one of them ran the GPU dry, so it backed off and
  changed nothing (with eager kicks a frame's last part is a small list: holding until only it is left is too
  late). It is off again. At 30 fps a frame's first GPU work waits about 14 ms for the GPU to finish the frame
  before; the controller could be read that much later without the GPU starting the frame any later.
  1. banjo_late_input_gpu (ON): late input also measures that wait (each frame's first GPU work against the
     GPU finishing the frame before) and delays the controller read by it, less 4 ms. It corrects itself: a
     later read leaves less wait to measure. Expected: "input -> GPU done" at 30 fps about 10 ms lower; the
     [latency] line's late input part says "... ms of it from the GPU queue". Same pictures, same frame rate.
     configs/banjo_late_input_gpu_off.toml is the old way; configs/banjo_late_input_gpu_ab.toml switches it
     every 4 s (the "[renut A/B] banjo_late_input_gpu" line).
  Runs: renut_auto.toml and configs/banjo_late_input_gpu_off.toml on the scripted route.

Package 36k (2026-10-20 afternoon: the latency trim on, on top of 36j)
----------------------------------------------------------------------
  COPY: renut.toml, renut_auto.toml and the configs folder. The NRO and deko3d_shaders.bin are 36j's (the same
  files: banjo-nx.nro 88,881,349 bytes, md5 efddb226b227...).
  Your 36j run: display sync runs on the console now (refresh 16.666 ms; its automatic phase stays at 0, a few late
  frames every 10 s step it back), no shader compiled at all (the new cache covers your route), the same speed.
  At 30 fps "input -> GPU done" is the same as 36g's (52-55 ms in play): the GPU, about 80 % busy, is still
  finishing the frame before when a frame's work arrives, so sending it sooner (eager kicks) does not finish it
  sooner. The controller is read about a frame before the GPU gets to that frame.
  1. banjo_deko3d_latency_trim is ON (package 36e, tested in the emulator): after each present the render thread
     holds the next frame until the GPU has only the current frame's last part left, so the game's controller read
     (late input) comes that much later and the frame waits less in the GPU's queue. If the GPU ever runs dry
     because of it, it stops holding for 60 frames by itself. Expected: "input -> GPU done" down by up to a frame
     where the GPU is the busier part; the [latency] "latency trim" line counts the frames held and how long.
     configs/banjo_deko3d_latency_trim_off.toml is the old way. Eager kicks and display sync stay on.
  Runs: renut_auto.toml (everything on) and configs/banjo_deko3d_latency_trim_off.toml on the scripted route.

Package 36j (2026-10-20 midday: display sync made to start, a probe fix, on top of 36i)
---------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (88,881,349 bytes, md5 efddb226b227...) and the NEW deko3d_shaders.bin from the package folder. The
  tomls and configs are 36i's (display sync, eager kicks and skin once on).
  Your 36i runs: the same speed as 36g (dialogue 42-44 ms of GPU, 22-23 fps; the heaviest 57 ms). Three findings:
  1. Display sync never ran: "[display] cannot open the display". The game's window (libnx's default window, which
     the deko3d swapchain uses) has the display open already and the console refuses a second open. The NRO now
     makes that window itself (the same system calls in the same order) and display sync uses its display.
     NOT yet run in the emulator (it was busy): this changes how the window is made at start-up; if the game does
     not start, put 36i's NRO back and tell me. When it works the log says "[display] the frame limiter follows the
     display's refresh" and every 10 s a [display] line (present -> display, input -> display).
  2. Eager kicks looked a little slower (input -> GPU done +1.5..+4.8 ms) but that was the probe: it noticed the
     GPU finishing only at some kicks, and with eager kicks fewer of those were left. It now looks at every kick in
     both modes, so the A/B compares like with like.
  3. The 13 shaders your first run compiled are in the new deko3d_shaders.bin (your log named them).
  Runs: renut_auto.toml (everything on), configs/banjo_display_sync_off.toml, configs/banjo_deko3d_eager_kicks_off.toml.

Package 36i (2026-10-20 midday: input lag at the frame cap, on top of 36h)
-------------------------------------------------------------------------
  COPY: banjo-nx.nro (88,881,349 bytes, md5 8a54ce27c322...), renut.toml and renut_auto.toml (CHANGED: 1 new key ON,
  banjo_display_sync ON) and the configs folder (regenerated: 4 new). deko3d_shaders.bin is 36g's (the same file).
  1. Your question: less input lag when the game already hits 30 fps. Your 36g logs at 30 fps: controller read ->
     the game's frame done 12.5 ms, -> the render thread done replaying it 14 ms more, -> the GPU done 28 ms more
     (54.6 ms in all), then the wait for the display's next refresh. The GPU gets most of the frame only at the
     end: a finished part goes to it at the game's next kick, not its own. banjo_deko3d_eager_kicks sends the frame
     in more, smaller parts (100 draws, up to 6 a frame), each at its own kick, so the GPU works while the render
     thread is still replaying. Expected 8-14 ms less controller -> frame done at 30 fps; the same pictures. ON by
     default (your request). configs/banjo_deko3d_eager_kicks_off.toml is the old way (its [latency] line's
     "input -> GPU done" against renut_auto.toml's, on the scripted route, shows what it saves);
     configs/banjo_deko3d_eager_kicks_ab.toml switches it every 4 s (its "[renut A/B]" line: fps and render thread
     time with and without it).
  2. For the wait for the display's refresh (about 8 ms on average): banjo_display_sync (package 35b) releases
     frames on the display's refresh with a measured phase. ON in these tomls at your request: only the emulator
     choked on it (if the console stalls or hangs, use configs/banjo_display_sync_off.toml and tell me). Its
     [display] line every 10 s gives present -> display and input -> display.
  3. Every millisecond of GPU or render-thread time is also a millisecond of lag at 30 fps (the three steps run
     one after another), so the GPU A/Bs of 36g help here too.
  4. The log now warns if a texture uses an "exponent adjust" (another port found Renut ignores it; Banjo's
     emulator route showed none).
  Runs: (a) renut_auto.toml (eager kicks and display sync on), (i) configs/banjo_deko3d_eager_kicks_off.toml, (j)
  configs/banjo_display_sync_off.toml, each on the scripted route; the logs as usual.

Package 36h (2026-10-20 midday: the same pictures and speed as 36g, better logs)
--------------------------------------------------------------------------------
  COPY: banjo-nx.nro (88,873,157 bytes, md5 83cdfcd83b4c...). Everything else is 36g's (tomls, configs,
  deko3d_shaders.bin): if you have 36g on the card, only the NRO changes.
  Your 36g runs (thank you): against package 35 on the same route the dialogue scene went from 52.7-55.0 to
  42.4-43.9 ms of GPU a frame (18-19 -> 22-23 fps) and the heaviest moment from 67.3 to 55.8 ms (14.8 -> 17.7
  fps); the dialogue portrait's fur costs 40-59 us a draw instead of 93. The first run compiled 13 shaders the
  cache lacked (kept on the card: the second compiled none).
  1. The log names each shader compiled in play (its size and a hash), so your logs alone let me add them to the
     next cache (before, that needed pipeline_list.bin).
  2. The "[renut] skin once" line counts the fur and other skinned draws apart. Skin once for the shadow maps was
     tried in the emulator and left out (most shadow draws had nothing to reuse).
  3. The package folder keeps the build's symbol file (renut_<md5>.elf, not in the zip), so the CPU profile in
     logs/rex can be named function by function.
  Runs: the A/Bs if you have time, each on the scripted route (renut_auto.toml with the config's keys, or the
  config itself): configs/banjo_deko3d_skin_once_ab.toml, configs/banjo_deko3d_light_branch_ab.toml,
  configs/banjo_deko3d_gpr32_ab.toml. Their "[renut A/B]" lines give each one's GPU time with and without it.

Package 36g (2026-10-20 late morning: the fur skinned once a frame, on top of 36f)
---------------------------------------------------------------------------------
  COPY: banjo-nx.nro (88,873,157 bytes, md5 3e2ed7be469c...), renut.toml and renut_auto.toml (CHANGED: 1 new key, ON),
  the configs folder (regenerated: 3 new) and the NEW deko3d_shaders.bin from the package folder (the four fur
  vertex shaders changed; everything else is 36f's cache). Everything in 36f is in 36g: if you have time for one
  package, run 36g.
  1. Exact, GPU (on by default, your choice): Banjo's fur draws every body part once a shell, shell by shell, and every
     one of those draws skins the same vertices again (12 bone rows loaded and blended a vertex, about half of the
     fur vertex shader). With banjo_deko3d_skin_once on, a part's first draw of the frame skins and writes each
     vertex's three skinned rows to a buffer, and the part's other shells read them there: their vertex shader
     475 -> 231 instructions and 50 -> 30 registers (the other three fur shaders alike). A shell reads the rows only
     when everything the skinning reads is the same as for the draw that wrote them (vertex and index data, bone
     data, constants); otherwise it skins as before. The same pixels: the rows are stored and read as the exact
     bits the writing draw computed. Checked by running the compiled shaders (old, writing, reading) on random
     inputs: every output equal, bit for bit, in all four shaders; a deliberately broken copy is caught.
     Your package-35 profile: a fur draw into the 96x96 portrait costs ~93 us of which the pixels are perhaps
     30-50; if the rest is vertex work, the fur's ~110 draws a frame spend ~4 ms on it and this saves 2-3 ms of
     the heaviest frames. configs/banjo_deko3d_skin_once_off.toml is the old way; configs/banjo_deko3d_skin_once_ab.toml switches it off and on every 4 s
     (the "[renut A/B] banjo_deko3d_skin_once (target 17)" line every 10 s), to measure what it saves. Its log line "[renut] skin once ...: W ... wrote their rows, R read them, P skinned as before"
     should show R about 10-20 times W.
  Runs: as 36f's (a) renut_auto.toml first (skin once on), then (g) configs/banjo_deko3d_skin_once_ab.toml on the
  scripted route (renut_auto.toml with banjo_renut_ab_target = 17 and renut_gpu_profile = 10.0 is the same thing for
  the scripted route). tools/summarise_console_log.py reads all of it from the logs.

Package 36f (2026-10-20 morning: no shader compiles in play, on top of 36e)
---------------------------------------------------------------------------
  COPY: banjo-nx.nro (88,803,525 bytes, md5 d25c70dca105...) and the NEW deko3d_shaders.bin from the package folder
  (deko3d_shaders_f447.bin is 36d's). The tomls and configs are 36e's.
  Your question from last night, whether all the shaders can be compiled ahead so no thread compiles them in play:
  they are, except for what the cache does not know yet, and tonight's emulator runs showed why the same few (4-7)
  were compiled again at every start:
  1. What the console compiles and learns in a session (shaders into deko3d_shaders.bin, pipelines into
     pipeline_list.bin) was written but not committed to the card: closing the game with HOME lost it, so the next
     start compiled the same shaders again. Both files are now committed (shaders at each compile, the pipeline list
     once a second).
  2. Old pipeline list records from before package 30 made the renderer build three pipelines with a setting no
     longer used, compiling their shaders at every boot for nothing. Loaded records now follow the current settings.
  3. The shaders the game asked for that the cache lacked (new combinations of the game's shader switches) are in
     the new deko3d_shaders.bin.
  The log names every shader compiled in play: "[deko3d] uam: run-time variant (stage ..., ... bytes): ...". On the
  second run of the scripted route that should say nothing; send the logs with pipeline_list.bin as usual and the
  next cache covers whatever your console found.

Package 36e (2026-10-20 morning: input lag, a compiler fix, on top of 36d)
--------------------------------------------------------------------------
  COPY: banjo-nx.nro (88,803,525 bytes, md5 4b58b30807a3...), renut.toml and renut_auto.toml (CHANGED: 2 new keys, both
  off) and the configs folder (regenerated: 2 new). deko3d_shaders.bin and deko3d_shaders_f447.bin are 36d's (the same files: keep them if you copied 36d; the compiler fix changes none of Banjo's cached shaders, checked byte for byte).
  Run 36e rather than 36d: 36d's shader compiler can crash on a skinned vertex shader whose bone row has an unused
  middle word (found by another port; none of Banjo's cached shaders has one, but the console compiles a few
  variants of its own on a first run).
  1. Input lag without vsync (off; configs/banjo_deko3d_latency_trim_on.toml): where the GPU is the limit (the
     heavy scenes), a finished frame used to wait in the GPU's queue for most of a GPU frame (your package 35 logs:
     swap -> present 66-95 ms at 18 fps), and the game had read your controller that much earlier than needed. On:
     after each frame the renderer waits until the GPU has only that frame's last part left before it records the
     next one and lets the game start it, so the controller is read later; the GPU still has work queued. If the
     GPU ever runs dry because of it, it stops for 60 frames. Expected: about 30-40 ms less lag in the heaviest
     scenes, nothing in the 30 fps ones. The [latency] line now ends with "input -> GPU done" (the earliest the
     screen can show the frame), and a "latency trim" line says how often it held back and whether the GPU ran
     dry. Please run it on the scripted route and send the log: its [latency] and fps lines against run (a)'s.
  2. NOT exact, your call (off): configs/banjo_renut_portrait_fur_shells_3.toml draws 7 of the 21 fur shells in
     the 96x96 dialogue portrait only (~2.5 ms of the heaviest frames for a few thousand pixels).
  Runs: as 36d's, plus (h) configs/banjo_deko3d_latency_trim_on.toml on the scripted route.

Package 36d (2026-10-20 morning: 128-bit bone rows, on top of 36c)
-----------------------------------------------------------------
  COPY: banjo-nx.nro (88,787,141 bytes, md5 f9e885bf4813...), renut.toml and renut_auto.toml (CHANGED: 1 new key, on), the
  configs folder (regenerated: 1 new) and BOTH caches from the package folder: the NEW deko3d_shaders.bin (every
  skinned vertex shader changed; the old one is refused as another compiler's and everything would compile on the
  console) and deko3d_shaders_f447.bin (only for the off config). Run 36d rather than 36c: it also fixes a crash
  in the profiler's stack sampler (on in every profiling config) that another port hit in the same code.
  1. Exact, GPU: your package-35 profile shows the fur and shadow-map vertex shaders bound by memory, not by
     arithmetic: the fur drawn into the 96x96 dialogue portrait costs 1.88 ms for 21 draws (89 us each, almost no
     pixels), and the two shadow-map passes 2.1 ms with no pixel shader. Each skinned vertex reads its bone rows
     as 48 separate 32-bit loads; the shader compiler now reads each row with one 128-bit load: 12 loads instead of
     48 in the fur vertex shader (596 -> 560 instructions, 2 more registers), and the same in every skinned vertex
     shader (the characters, and their copies in the shadow maps). The same words are read: every shader without
     bone rows compiles byte for byte as before. Estimated 1-2 ms off the heaviest frames, but not certain: the wider
     loads cost about 2 registers in most skinned shaders (the fur's 48 -> 50 means fewer of them resident at once),
     so please run (g) below: its GPU lines against run (a)'s decide whether this stays on.
     configs/banjo_deko3d_uam_wide_rows_off.toml is the A/B (the 32-bit loads, with deko3d_shaders_f447.bin).
  Runs: as 36c's, plus (g) configs/banjo_deko3d_uam_wide_rows_off.toml on the scripted route (its GPU lines
  against run (a)'s).

Package 36c (2026-10-20 morning: the yellow flash, on top of 36b)
----------------------------------------------------------------
  COPY: banjo-nx.nro (88,787,141 bytes, md5 47cd06522220...), renut.toml and renut_auto.toml (CHANGED: 2 new keys) and
  the configs folder (regenerated: 4 new). deko3d_shaders.bin is 36b's (the same file: keep it if you copied 36b).
  Everything in 36b and 36 below is in 36c: if you have time for one package, run 36c.
  1. The colour flash, a second cause (caught in the emulator with RenderDoc): a whole frame washed yellow. The bloom
     chain read one of its 80x45 levels through the swizzle 1,1,0,0 (solid yellow) instead of RGBA, because the
     renderer took the swizzle from the texture's header in game memory, a temporary the game had already
     rewritten for its next pass, not from the copy the GPU uses (the device's fetch constant). Now a header that
     differs from the device's copy in anything SetTexture copies unchanged (swizzle, signs, pitch, tiling, number
     format, mip address) is treated as stale and the device's copy is used. In the emulator the header's yellow
     swizzle showed up again on that bloom level and was not used. Not rare: the main menu's transition hit it in
     a run of frames (a texture read with blue and alpha forced to 1), so this is likely also the "menu glow cuts
     out at 30 fps" and the cyan flashes you reported in package 29/30: please look at the menu and the sky. The
     log's periodic report has "[flash] device swizzle: N binds in M frames" and "[flash] header vs device (same
     data): ... by dword" lines (dword 4, the mip levels, differs all the time by design: the sampler's mip limits),
     and the first 16 stale headers one by one ("its header (same data) is stale").
     configs/banjo_renut_device_swizzle_off.toml is the old behaviour (A/B; it still counts them).
  2. Banjo's nose as a flat black block in close-ups (the pizza cutscene): the emulated renderer draws it the same way
     in frame-matched video, so it is not a deko3d bug. If a 360 shows it rounder or shiny, tell me.
  3. The two GPU A/Bs in ONE run each: configs/banjo_deko3d_light_branch_ab.toml and configs/banjo_deko3d_gpr32_ab.toml
     switch their key off and on every 4 s, so the "[renut A/B]" line every 10 s gives the GPU time of the same
     scenes with and without it (two separate runs differ by more than these keys do). Use them instead of 36b's
     (d) light_branch_on and 36's gpr32_on runs. Both halves' shaders are in the cache (no compiles); the first
     switches build the other half's pipelines from it (a short hitch the first time). Run one A/B config at a time:
     the two keys together have no cached variants.
  4. NOT exact, your call (off: banjo_renut_fur_shells = 1): configs/banjo_renut_fur_shells_2.toml draws every 2nd
     fur shell of each mesh (11 of Banjo's 21, the dialogue portrait's too): thinner fur, the same length. The fur is
     about 18 ms of the heaviest frame, almost all of it pixel work (21 layers of Banjo's pixels through the lit fur
     shader; re-skinning ~5,000 vertices a shell is well under 1 ms in all), so half the shells should save 6-9 ms:
     that scene from ~48 ms to ~39-42 ms, about 24-25 fps at the stock clock. Its GPU lines will say. Have a look at Banjo up close and say whether the look is acceptable; its log has
     "[renut] fur shells ... drawn, ... not drawn".
  Runs: (a) renut_auto.toml (the scripted route) as before; (b) configs/banjo_deko3d_light_branch_ab.toml on the same
  route; (c) configs/banjo_deko3d_gpr32_ab.toml; if there is time (d) configs/banjo_renut_diag_fur_vertex_only.toml
  (only its GPU lines matter), (e) configs/banjo_renut_fur_shells_2.toml, or renut_auto.toml with banjo_renut_fur_shells = 2 for the
  scripted route (look and GPU lines)
  and (f) configs/banjo_renut_game_gamma_on.toml to look at.

Package 36b (2026-10-20 morning: skinning and lighting shaders, on top of 36)
---------------------------------------------------------------------------
  COPY: banjo-nx.nro (88,778,949 bytes, md5 a1ebb8894ff3...), renut.toml and renut_auto.toml (CHANGED: 2 new keys, both off),
  the configs folder (regenerated: 3 new) and the NEW deko3d_shaders.bin from the package folder (the skinned
  characters' vertex shaders and every lit pixel shader changed). Everything in 36 below is in 36b too: if you have
  time for one package, run 36b.
  1. Skinned characters' vertex shaders, the same pixels: Banjo's fur runs its vertex shader once a shell (21 shells a
     frame, and 21 more for the 96x96 portrait of a speaking character in dialogues, whose draws your logs show cost 89 us each for a few thousand
     pixels: vertex work). A bone's data select that only its own sum read is gone, and each bone row's address
     arithmetic is done once instead of four times: the fur vertex shader 594 -> 509 instructions and 56 -> 48
     registers; in the shadow maps 473 -> 388 and 43 -> 35. 324 vertex shaders.
  2. Lit pixel shaders, the same pixels: the lights' selects whose other value nothing reads are gone (79 in each lit
     shader): the fur shader in your heaviest scene 775 -> 714 instructions (-8 %), the other lit materials alike, and
     the depth-of-field composite A5E2 153 -> 134. Each change was checked by running the old and new compiled shader
     on hundreds of random inputs (bit-identical outputs; a deliberately broken copy is caught).
  3. The early depth test also for draws whose stencil test cannot write (7 more pipelines; small).
  4. New warning, expected never: "[renut] fetch data: ... off 16 bytes". If you see it, tell me (item 1 relies on it).
  5. A/B, off: configs/banjo_deko3d_light_branch_on.toml puts each point light's work in 14 of the heaviest lit shaders
     in a branch on whether the light reaches the pixels, so groups of 32 pixels all out of its reach skip it (about 35
     instructions a light); where every light reaches (characters, as the game picks lights per object) the branches
     cost a few instructions. The same pixels either way; only the console can say which is faster.
  6. A measurement, not a picture: configs/banjo_renut_diag_fur_vertex_only.toml makes the fur family's pixel shaders
     discard at once (the fur vanishes), so the GPU profile shows what its draws cost without the pixel work: how much
     of the fur's ~18 ms is vertex shading (my estimate from your logs: ~5 ms). Its shaders compile on the console the
     first time (a few seconds of stutter).
  Runs: as 36's (a), (b), (c) below, then (d) configs/banjo_deko3d_light_branch_on.toml on the scripted route, and if
  there is time (e) configs/banjo_renut_diag_fur_vertex_only.toml (only its GPU lines matter).
  One question for you (default unchanged): the game loads a display gamma ramp (about x^1.17: darker midtones), which
  the Xbox 360 and banjo-nx's own renderer apply and Renut's renderer never has, so the deko3d builds have been a
  little brighter in the midtones than a 360 since package 26 (three other ports measured the same in theirs).
  configs/banjo_renut_game_gamma_on.toml applies it exactly (the game's own 256-entry ramp, looked up per 8-bit value
  in the present pass, as the 360's display does; the log says "[renut] game gamma ramp applied" with its entries:
  [64]=192 [128]=459 [255]=1019, the same your native renderer logged). Checked in the emulator. Have a look; if you
  like it, it can become the default (banjo_renut_game_gamma = true).

Package 36 (2026-10-20 night: GPU for the heaviest scene, the colour flash)
-------------------------------------------------------------------------
  COPY: banjo-nx.nro (89,372,869 bytes, md5 7395b07e886a...), renut.toml and renut_auto.toml (CHANGED: 4 new keys), the
  configs folder (regenerated: 5 new) and the NEW deko3d_shaders.bin from the package folder (every pixel shader's
  text changed again). Keep your pipeline_list.bin, and please copy it into the logs folder you send: the next shader
  cache built from your console's own list means no shader compiles even on a package's first run (your second run of
  package 35 compiled none; the first compiled 12 the cache had not predicted).
  1. GPU, the same pixels (docs/deko3d-renderer.md section 19). Your heaviest scene was 51.6 ms of GPU a frame:
     - texture size queries read from a buffer instead of a texture instruction per pixel: A5E2 (a full-screen pass,
       the second heaviest pixel shader of the whole route, 1.9 ms) does 4 texture operations a pixel instead of 8,
       and the fur shells one fewer (configs/banjo_deko3d_txq_off.toml is the A/B);
     - draws that write no depth or stencil (particles and other blended effects; not the fur shells, which write
       depth) test depth before their pixel shader runs, so hidden pixels no longer run it
       (configs/banjo_deko3d_early_z_off.toml is the A/B);
     - an A/B only, off: the fur shaders compiled with 32 registers so the GPU can keep more of them in flight, at the
       price of a few spills (configs/banjo_deko3d_gpr32_on.toml).
  2. The colour flash: while the game stalls (loading), the renderer re-presented its last frame every 16 ms; if the
     game had freed or remade that texture meanwhile, the screen showed new memory nobody had cleared (deko3d does
     not clear it, Vulkan's came cleared), a full-screen colourful frame. Now nothing is re-presented during a stall
     unless a menu is up (the screen keeps the last frame, as the console does), and a frame no resolve wrote is never
     shown. If you still see a flash, tell me where (scene / moment) and send that log: a [flash] line names any texture
     sampled with nothing in it. configs/banjo_renut_stall_present_on.toml brings the old behaviour back.
  Runs: (a) renut_auto.toml's scripted route: the heavy scenes' GPU line and fps against package 35b. (b) the same with
     configs/banjo_deko3d_early_z_off.toml, then (c) configs/banjo_deko3d_gpr32_on.toml: these tell which of the two
     help on the console. Your GPU runs at 460.8 MHz handheld; docked the console's stock GPU clock is 768 MHz.

Package 35b (2026-10-19 night: the fur fix, the limiter on the display's refresh)
---------------------------------------------------------------------------------
  COPY: banjo-nx.nro (89,192,645 bytes, md5 98694401fef8...), renut.toml and renut_auto.toml (CHANGED: 3 new keys), the
  configs folder (regenerated: banjo_display_sync_on.toml is new) and the NEW deko3d_shaders.bin from the package
  folder (next to the zip; package 35's is not used: the shaders changed again). Keep your pipeline_list.bin.
  1. The fur regression of package 35, fixed. Your package 35 logs: the heavy scenes' GPU went 48-52 -> 52-55 ms a
     frame, all of it in the blended fur shells (the fur shader 9.0 -> 12.1 ms, three others about twice as slow). The
     Jinjo Bingo fix added a value the shaders read that the early-alpha tool did not know, so 3,604 shaders lost
     Renut's early alpha test (a quad whose pixels all fail is discarded before the shader body); the pictures were
     identical, only slower. The tool knows it now: 3,663 shaders have their early test again, and the heavy scenes
     should come back to package 34's GPU time less the 1 ms the depth hand-over saves.
  2. Latency batch 3, OFF by default in this package: in the emulator every run with it on stalled or hung, and the
     runs without it did not; I have not found why yet. Try it on the console only with
     configs/banjo_display_sync_on.toml, after the main runs. What it does: the frame limiter (frame_cap = "30")
     follows the display's refresh. It released frames on its own
     33.3 ms timer, whose phase to the display slid: an image finished just after a refresh waited almost 16.7 ms more,
     and now and then one frame was shown for three refreshes and the next for one. Now each frame is released at a
     refresh plus a phase the game measures (as late as the frames still reach the display at the same refresh);
     a frame that ends late still goes at once (no 20 fps snapping). Late input's ceiling is now 80 % of the frame
     interval instead of 30 ms (it was at its ceiling in your 18 fps scenes; this part is on).
     With the config, a [display] line every 10 s: present -> display and input -> display (when the image is really
     on the screen, not just handed over), the phase and its misses, and diagnostics for the emulator's stalls.
  Runs: (a) renut_auto.toml's scripted route, as for package 35: the fur scenes' GPU time is the check of item 1.
  (b) only if (a) went well: the same route with configs/banjo_display_sync_on.toml. If the game stalls or hangs,
  send that log (it tells me where); if it runs, the [display] lines give the latency it gains.

Package 35 (2026-10-19 evening: GPU, render thread, Jinjo Bingo, input latency)
-------------------------------------------------------------------------------
  COPY: banjo-nx.nro (package 35's), renut.toml and renut_auto.toml (CHANGED: 8 new keys, all on),
  the configs folder (regenerated, 7 new configs) and the NEW deko3d_shaders.bin from the package folder (next to the
  zip): every shader changed (the Jinjo Bingo fix below), so the old cache is not used and without the new one the
  console compiles every shader again during play. Keep your pipeline_list.bin (and send it with the logs if you can).
  1. GPU (docs/deko3d-renderer.md, section 16), from your package 34 profile (depth resolves 3.3 ms a frame):
     - shadow maps are no longer copied: the scene samples them straight from their depth buffers, and the shadow pass
       draws into a second buffer while the first is read (2 of the 5 depth copies a frame gone; "depth hand-over" line);
     - shadow maps (and the other D24S8 depth buffers) in the Xbox 360's own 24-bit depth format, 4 bytes a pixel
       instead of 8 (their depth is the console's 24-bit value now, within one 24-bit step of before);
     - fewer GPU pipeline drains (barriers merged, no cache flush where only writes follow, none between two reads);
       the deko3d line counts the barriers and now the copy-engine work by kind;
     - the black clear of the whole screen before the frame is skipped (the frame covers it).
  2. Render thread (docs/render-thread-proposals-2026-10-19.md, the report from the agent; most of it taken):
     - texture checks proven by the store cards like the vertex copies (the "store cards" line counts textures too;
       on the emulator over 99 % of the checks, 0 changed under clean cards);
     - float constants and the shared constants pushed to the GPU by what changed (pipelined constant-buffer updates)
       instead of a 5.7 KB upload per draw, and no cache clean of them;
     - the texture binding memo by texture (it almost never hit: every upload and resolve reset it), the pipeline memo
       8 times larger, framebuffer / surface / viewport memos, per-shader summaries, the memexport bookkeeping once
       per draw instead of four times, the upload ring cleaned only where written, the ring clean on core 2, no heap
       allocation per draw, the GPU profile's labels only on the frames it times, and smaller items;
     - one proposal was taken out again: holding the renderer's lock across render items hung the emulator at the first
       loading screen.
  3. The Jinjo Bingo fix from Renut upstream (Beta-Nut 4, commit 574b488): "jinjo bingo would show a black square
     instead of the silhouette of the item". A draw the game confines to one tile of the screen gets its pixel
     position relative to that tile, as on the Xbox 360 (the shaders read it: every shader rebuilt, hence the new
     deko3d_shaders.bin).
  4. Input latency:
     - the GPU gets each frame's work in pieces at the game's kicks (up to 4 a frame) instead of all at the present:
       it starts sooner, and the frame is shown sooner;
     - when the render thread is the slower one, the game's frame used to wait at its Swap; the game now reads the
       controller later by about that wait (less 2 ms), so the frame shows newer input without coming any later;
     - the [latency] line every 10 s: input -> swap -> present (the means and maxima), and the late input delay.
     configs/banjo_deko3d_kick_submit_off.toml and banjo_late_input_off.toml are the A/Bs.
  On the emulator (Ryujinx, the scripted route) the pictures are as before with all of it on.
  Runs: (a) renut.toml: the intro cutscene, the title screen a minute, the menu, a minute of Spiral Mountain, and if
  you can a Jinjo Bingo board with an unknown item (it should show the item's silhouette, not a black square); send
  logs/rex and deko3d.log. The [latency] line gives input -> present every 10 s; tell me how the controls feel against
  package 34. (b) renut_auto.toml's scripted route (it plays itself), then the same route with
  configs/banjo_late_input_off.toml and configs/banjo_deko3d_kick_submit_off.toml: the [latency] lines of the three
  logs compare the latency work, the fps and the GPU profile its cost. (c) If you have time:
  configs/banjo_deko3d_push_constants_off.toml (the pushed constants' A/B of the render thread) and
  banjo_deko3d_depth_handover_off.toml (the shadow-map copies back, the GPU's A/B), on the scripted route.

Package 34 (2026-10-19: CPU work from the codegen report, more GPU and shader work)
-----------------------------------------------------------------------------------
  COPY: banjo-nx.nro (package 34's), renut.toml (CHANGED: new keys written out, all on), the configs
  folder (regenerated, 6 new configs) and the NEW deko3d_shaders.bin from the package folder (next to the zip). The
  shader compiler changed again, so the old deko3d_shaders.bin is not used: without the new one the console compiles
  every shader again during play. Keep your pipeline_list.bin (and if you can, send it to me with the logs: the next
  shader cache can then hold every pipeline you have played, not only the emulator's).
  1. CPU (as asked): the shader compile thread runs on core 2 below the game's threads (a thread waiting for a compile
     lends it its priority); a draw waiting for its specialised pipeline no longer locks the build queue every time;
     the alpha-test variants (no test, >= and >) are in the shader cache for every pipeline of an old list, so a first
     run does not compile them during play. Package 33's run compiled 231 shaders during play; the emulator found that
     183 of its own were missing too, 103 of them the ">" alpha test, now in the cache.
  2. A package 33 bug: the alpha-test folding did not reach the console. Draws without the alpha test still had a
     discard, so the GPU could not test depth before shading them. Fixed (the opaque main pass should be cheaper).
  3. GPU: Zcull is now told when a copy, resolve or blit writes a depth buffer; depth tests that cannot fail and write
     nothing are turned off (the GPU no longer reads depth for those full-screen passes).
  4. Shaders, the compiler (uam) and the GLSL fed to it (docs/deko3d-shader-optimisations.md, items 2.16 to 2.28):
     - storage-buffer offsets folded into the loads (skinned vertex shaders 846 -> 774), bone rows past the end read a
       zero register, signed fields extracted in one instruction, inputs the vertex shader writes as zero are
       constants in the pixel shader;
     - an input is interpolated once where it was interpolated again in every block, and for each pixel shader uam now
       compiles a few ways and keeps the one with fewer registers, then less code;
     - value numbering: what a shader computes again (Renut's early alpha test repeats the main body's texture fetches
       and arithmetic) is reused, including the same if / else computed twice; pixel shaders about 2.5 % fewer
       instructions weighted by your GPU profile;
     - no SSY / SYNC pair (divergence bookkeeping, two instructions each) around branches on constants, which cannot
       diverge: about 20 instructions fewer in each heavy shader;
     - the fur shader (2F36, the top of your GPU profile, 6.2 ms) no longer fetches its textures twice: 4 texture fetches
       fewer and about 40 instructions. configs/banjo_deko3d_early_hoist_off.toml is the A/B for that one.
     - ifs in a row on the same per-pixel condition are one if (the fur shader had 8 of them for its shadows): another
       3.3 % fewer instructions, and most heavy pixel shaders now need 31-32 registers instead of 40 (the fur shader 39
       instead of 44), so the GPU keeps more pixels in flight to hide texture latency.
  5. Depth resolves: the "words" copy of each depth resolve (depth and stencil as bytes) is written only for the
     resolves something reads that way (the depth of field's); the shadow maps' and the others' are not: about 14 MB a
     frame less to write. configs/banjo_renut_words_always.toml writes them every time, for the A/B.
  6. CPU, the codegen report's proposals (docs/cpu-codegen-proposals-2026-10-19.md, section 0 lists what was taken):
     - the render thread no longer hashes every cached vertex and index copy after each kick (16 MB a frame in
       gameplay, about a quarter of the render thread in your profile): every game store outside the stack now marks
       its 4 KB page, and a copy is hashed again only when one of its pages was written. The "store cards" line every
       10 s counts both. configs/banjo_renut_store_cards_off.toml hashes as before (the A/B of the render thread; the
       marks themselves cost the game threads a little and stay);
     - texture bindings, render targets and pipelines looked up once per change instead of every draw; the recording
       threads take no lock per recorded draw; clears and resolves no longer allocate; the GPU state of a pipeline is
       sent only in the parts that differ from what is bound. configs/banjo_renut_cpu_memos_off.toml and
       banjo_renut_pipeline_memo_off.toml are the A/Bs;
     - the recompiled game code: single floats copied through a register (also negated or made absolute on the way)
       without two conversions (15,700 places, the culling loop of the drawing threads among them); the hottest calls
       between generated files inlined; 24 functions that a comment in the codegen config made look "hooked" are
       inlinable again; the four hottest functions that had their registers kept in memory now use locals; the
       kernel's waits and exports cheaper.
  Runs: (a) renut.toml: the intro cutscene, the title screen a minute, the menu, a minute of Spiral Mountain; send
  logs/rex and deko3d.log (the GPU profile by shader is on). (b) configs/banjo_deko3d_early_hoist_off.toml, the same
  route: the fur shader's line in the GPU profile tells whether 4 fetches fewer beat 2 registers more.
  (c) the CPU A/Bs if you have time, each on renut_auto.toml's scripted route (it plays itself):
  configs/banjo_renut_store_cards_off.toml, then banjo_renut_cpu_memos_off.toml. Send logs/rex after each.
  Optional: configs/banjo_deko3d_uam_gvn_off.toml and _uam_uniform_branches_off turn the compiler's two new passes off
  (they compile their shaders during play: the CPU numbers of those runs do not count, the GPU profile does).

Earlier changes (2026-10-18 11:05, package 33: GPU time on deko3d)
-------------------------------------------------------------------
  COPY: banjo-nx.nro (86,087,877 bytes, md5 62472cfb3394...), renut.toml (CHANGED: new keys written out, all on), the configs
  folder (regenerated, 9 new deko3d configs) and the NEW deko3d_shaders.bin from the package folder (next to the zip; the
  shader compiler changed, so the old file is not used: without the new one the console compiles every shader again,
  slowly, the first time). deko3d_shaders_noprmt.bin (next to it) is only for configs/banjo_deko3d_uam_prmt_off.toml.
  Keep your pipeline_list.bin: this build converts it at the first start (the pipeline key grew) and keeps your
  pipelines.
  Everything here is about the GPU (docs/deko3d-renderer.md section 13, docs/deko3d-shader-optimisations.md):
  1. Resolves are drawn instead of copied. Each copy ran on the copy engine and stopped the whole GPU before and after
     it (about 17 a frame). The depth of field reads the depth it needs where the resolve put it, instead of a copy every
     frame. In the emulator the picture is the same as package 32's, frame for frame.
  2. The depth buffers are compressed by the GPU, and Zcull (the GPU's coarse depth test that skips hidden tiles) is on.
     The emulator does not emulate either, so only the console shows what they save.
  3. The shaders, rewritten for the deko3d shader compiler (uam), which makes much worse code than NVK's compiler from
     the same source: every ?: became branches, the alpha test was a chain of 7 comparisons with a discard in every
     pixel shader (so the GPU could never test depth before shading), and every vertex shader wrote all 25 outputs.
     Now: the alpha test's function, the tessellation kind and the outputs the pixel shader reads are part of each
     pipeline, so the shaders drop what a draw does not need (a draw without the alpha test has no discard and the GPU
     tests depth first; a shadow-map vertex shader computes only the position); selects instead of branches; the
     resolution-scale code (deko3d draws at 1280x720 only) is gone. Measured by uam's own output, weighted by package
     32's GPU time per shader: pixel shaders 878 -> about 630 instructions on average, vertex shaders 1209 -> about 800.
  4. uam itself: byte swaps (all the big-endian data the vertex shaders read from memory: bones, skinned vertices, the
     memory-export shaders' four-part swaps) are one PRMT instruction instead of five to eleven, and the per-word choice
     of swap is one too. The skinned character vertex shaders shrink to about 40 % (2689 -> 1043 instructions).
  5. Two small ones, as asked: while a draw's own pipeline is still being built it uses a generic one, whose alpha test
     is now a bit test instead of seven comparisons; and uam shares the address shift between neighbouring words a
     shader reads from memory ((i + 1) << 2 as (i << 2) + 4).
     PLEASE NOTE: the emulator does not implement PRMT at all, so in it the characters vanish; the console is the first
     test of this part. If Banjo, Kazooie or other characters are missing, stretched or "exploded" with renut.toml, run
     configs/banjo_deko3d_uam_prmt_off.toml (copy deko3d_shaders_noprmt.bin too) and tell me: that is the old code for
     the byte swaps, everything else the same.
  Runs: (a) renut.toml: the intro cutscene, the title screen a minute, the menu, a minute of Spiral Mountain; send
  logs/rex and deko3d.log (the GPU profile by shader is on, as in package 32, so the two compare directly). (b) the same
  route with configs/banjo_deko3d_gpu_before.toml (items 1 and 2 off) for the GPU A/B. (c) only if characters are wrong:
  configs/banjo_deko3d_uam_prmt_off.toml. Other new configs turn one thing off each: banjo_deko3d_copied_resolves,
  _words_copied, _depth_uncompressed, _zcull_off, _alpha_spec_off, _vs_outputs_off, _tess_spec_off.

Earlier changes (2026-10-18 03:00, package 32: the visual bugs, found with RenderDoc)
-------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (86,497,477 bytes, md5 033108595280...), renut.toml (CHANGED: four new keys written out, all on),
  the configs folder (regenerated, three new configs) and the NEW deko3d_shaders.bin (from the package folder, next to
  the zip: 49 new compiled shaders for the fixed water shaders). Keep your pipeline_list.bin. Still deko3d.
  Found with RenderDoc captures of the Ryujinx emulator running this NRO (every draw the deko3d renderer made) against
  the PC and the original emulated renderer (docs/deko3d-renderer.md section 12). Each one checked in the emulator:
  1. Depth of field and the blur around characters: Renut's renderer drew everything half a pixel up and left of where
     the Xbox draws it (the Xbox puts pixel centres on whole numbers; the renderer never added the half pixel). The depth
     of field's small blur turned that into a 4-pixel orange/brown halo around Kazooie's crest, Banjo and the radio. Now
     placed as the Xbox does (banjo_renut_half_pixel_offset = true): in the emulator Kazooie and Banjo are as sharp as on
     the original renderer, and the whole picture lines up with it twice as closely. configs/banjo_renut_half_pixel_off.toml
     has the old placement, to compare.
  2. Water (the flooded valley, the empty pond under the rock, and the cyan "sky flash", which was the misplaced water):
     the deko3d shader compiler reads a constant's component chosen at run time wrongly (always the first). The
     tessellated water fetched its corners from the wrong place. The shaders are now written so it cannot happen. In the
     emulator the pond and the moat are back and the valley is dry.
  3. The "non-square pixels" (grass, fur and leaf edges as a checker pattern): with MSAA off the GPU dithers alpha to
     coverage. Now an alpha test, as our native renderer does (banjo_renut_alpha_to_mask_test = true;
     configs/banjo_renut_a2c_dither.toml has the old way).
  4. The full-screen colour flash: a texture header the game rewrites while the renderer reads it (on the PC every frame
     for one of the glow/bloom textures) threw away the glow texture's content. The renderer now uses the GPU's own copy
     of the description. This should also be Banjo's menu glow dropping out at 30 fps (the faster the game runs, the more
     it happened); please check, the emulator cannot reach 30 fps.
  5. Smaller: the depth the game resolves for the depth of field carries the stencil now (the game reads it to tell the
     characters from the scenery; banjo_renut_depth_words_stencil), and point sprites' size limits are read as the
     hardware reads them (banjo_renut_point_minmax_radius).
  6. Please check: the intro cutscene ("Breaking news", "MMM... we may have put on a couple of pounds": Kazooie and Banjo
     sharp, no halo), the pond and river water on the title screen and in "Once upon a time...", grass and fur edges,
     flashes (full screen or the sky), Banjo's glow on the menu at 30 fps.
  Runs: (a) renut.toml: the intro cutscene, the title screen a minute, the menu, a minute of Spiral Mountain; send
  logs/rex and deko3d.log. (b) optional: configs/banjo_renut_half_pixel_off.toml over renut.toml in the intro (a photo of
  the "Breaking news" shot in each).

Earlier changes (2026-10-17 23:45, package 30: depth of field and water fixes)
------------------------------------------------------------------------------
  COPY: banjo-nx.nro (86,464,709 bytes, md5 65d8d49e60f2...), the configs folder (regenerated) and the NEW deko3d_shaders.bin
  from the package folder (it has the shaders this build needs; yours keeps working too, it would just compile them
  once), and renut.toml (CHANGED: banjo_renut_blend_skip = false written out, and the GPU profile split by pixel shader on,
  banjo_renut_diag_gpu_shaders = true; the same settings as package 29 otherwise, deko3d).
  1. Depth of field: the blend skip (banjo_gpu_blend_skip, a small GPU saving from package 27) made Renut's renderer blur
     things the game keeps sharp (Kazooie, the radio, Banjo's fur edges in the intro cutscene), on Vulkan as well as
     deko3d. Found by recording the same frames with the original emulated renderer on the PC and comparing them one by
     one. Renut's renderer no longer uses it (banjo_renut_blend_skip = false; our native renderer keeps it).
  2. Water: the river water missing on deko3d came back in the emulator with the same change.
  3. Please check: the depth of field and the blocky pixels around characters in the intro cutscene ("MMM... we may
     have put on a couple of pounds"), the river and pond water on the title screen, Banjo's glow on the menu, and
     whether the green full-screen flash or the cyan sky flash still happen.
  Runs: (a) renut.toml: the intro cutscene, the title screen a minute, the menu; send logs/rex and deko3d.log.

Earlier changes (2026-10-17 21:59, package 29: Renut's renderer on deko3d, first console run)
---------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (86,460,613 bytes, md5 8154904fa1d2...), renut.toml (CHANGED), the configs folder (all
  regenerated) and NEW deko3d_shaders.bin (from the package folder, next to the zip). Keep your pipeline_list.bin.
  1. renut.toml runs Renut's renderer on deko3d, the Switch's own GPU library, instead of Vulkan/NVK
     (renut_gpu_backend = "deko3d"; docs/deko3d-renderer.md). Same renderer, same optimisations, the same shaders
     converted for it. In the Ryujinx emulator it boots to gameplay (the intro, then Spiral Mountain). This is its first
     run on a console.
  2. The colour check: in the emulator some textures have red and blue exchanged. Banjo's shorts are teal instead of
     yellow, his backpack orange instead of blue, and the minimap's browns are blue; the level itself looks right. I do
     not know yet whether that is the emulator or the renderer. Please look at Banjo (shorts, backpack) and the minimap
     in Spiral Mountain, then put configs/banjo_renut_vulkan.toml over renut.toml (the same settings on Vulkan, as in
     package 28b) and look at the same place. A photo of each helps.
  3. deko3d_shaders.bin holds the 2,632 compiled shader variants the pipeline list needs (compiled on the PC). A shader
     that is not in it is compiled on the console the first time it is drawn (a stall of about a second) and added to
     the file, so the next time is smooth. Without the file every shader compiles on the console: very long stalls.
  4. New log next to the NRO: deko3d.log (the deko3d back end's own messages). Please send it back with logs/rex. The
     "[renut] deko3d:" lines every 10 s in the log count draws, shaders compiled on the console and cache hits.
  5. If it crashes or freezes, configs/banjo_renut_vulkan.toml over renut.toml gives you package 28b's renderer back.
     Please send the logs and deko3d.log of that run first.
  6. Under deko3d the record worker (banjo_renut_record_worker) is not used: deko3d commands are recorded directly, so
     configs/banjo_renut_record_ab.toml compares nothing in this build. Every other config runs on deko3d now (they
     take renut.toml's back end) except configs/banjo_renut_vulkan.toml.
  Runs: (a) renut.toml: boot to Spiral Mountain, the colours (photo), play a minute; send logs/rex and deko3d.log.
  (b) configs/banjo_renut_vulkan.toml at the same place: photo, logs.

Earlier changes (2026-10-17 14:04, package 28b: MSAA off, recording on two threads, the check worker gone)
--------------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (68,470,981 bytes, md5 2a0bfa9df796...), renut.toml (CHANGED), pipeline_list.bin (CHANGED) and the
  configs folder (all regenerated).
  1. The game's 2x MSAA is off (your call): renut.toml has disable_msaa = true (one sample per pixel, aliased edges).
     configs/banjo_renut_msaa.toml puts it back for a comparison.
  2. Command recording on two threads (banjo_renut_record_worker): the render thread records into a copy of each
     Vulkan call and a worker on core 0 plays them to the driver (plume + NVK were about a quarter of the render
     thread in your last logs). Same commands in the same order. configs/banjo_renut_record_ab.toml switches it every
     4 s (A/B target 14); "[renut] record worker" lines show how long the render thread waited for it.
  3. The check worker is removed: on your console it answered none of its checks (0 of about 300,000 every 10 s)
     while hashing 3.4 GB on core 0. Core 0 is the record worker's now.
  4. The depth-words view (package 28) is off: it is the main suspect for the broken depth of field. If the depth of
     field is still wrong, add banjo_gpu_output_trim = false to renut.toml and tell me whether that fixes it.
  5. The GPU profile no longer writes ~2,000 filler timestamps a frame (it reads only the ones a frame wrote).
  6. For the main pass breakdown you asked for: configs/banjo_renut_gpu_shaders.toml splits every pass by pixel
     shader ("GPU profile" lines with draws a frame). On the PC the main 1280x720 pass is mostly a few full-screen
     draws (2.4 of its 4.5 ms in groups of 1-3 draws), then blended draws (1.7 ms); the console decides.
  Runs: (a) renut.toml in a level, stock clocks: the logs (logs/rex too); is the depth of field right? (b)
  configs/banjo_renut_gpu_shaders.toml a minute in the same place. (c) configs/banjo_renut_record_ab.toml a minute.

Earlier changes (2026-10-17 12:54, package 28: Renut's renderer by default, fewer resolve copies, smaller draws)
-------------------------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (68,409,541 bytes, md5 c133c7a07b37...), renut.toml (CHANGED), pipeline_list.bin (CHANGED: the old one
  is of another format and would be thrown away) and the configs folder (all regenerated).
  First boot: the shader package changed, so the pipelines are compiled again (the driver's cache file starts over);
  the list above lets the workers build them ahead in the levels the PC played.
  1. renut.toml now boots Renut's renderer (banjo_renderer = "renut") with every one of its optimisations on, and the
     diagnostics I need: GPU time by pass every 10 s ("GPU profile" lines), one frame every 30 s of its resolves,
     clears and target changes ("[trace]" lines), the "[renut]" lines and the CPU sampler (logs/rex/rex_profile.log).
     configs/banjo_renderer_native.toml is the same with our own renderer; the configs of our renderer's own keys boot
     that one too.
  2. GPU times on the console were too small: NVK reports a 1 ns timer tick, the Tegra X1's is 1.6276 ns, so every GPU
     ms figure of Renut's renderer read 61 % of the real one (your last logs: the GPU, not only the render thread,
     limits many stock-clock windows). Fixed: the "GPU profile" and "[renut]" GPU figures are real now.
  3. Fewer resolve copies: the game reads nearly every depth resolve as an 8_8_8_8 texture (the depth as bytes). Renut
     copied the whole resolve into that texture each time; the texture now reads the resolve's own words
     (banjo_gpu_depth_words_view; configs/banjo_renut_words_ab.toml, A/B target 13). Same bytes. On the PC (a GPU with
     bandwidth to spare) the A/B is within noise; the console's memory is where the copy cost, so its A/B decides.
  4. Smaller draws: (a) the colour channels a draw never writes are left out of its pixel shader, so the driver drops
     the work that only made them (banjo_gpu_output_trim; configs/banjo_renut_trim_ab.toml, A/B 12; about 26,000-30,000
     draws per 10 s write part of their channels). PC A/B: no change (the PC's driver knows the write mask, NVK's
     shader compiler does not). The A/B config's off half compiles the untrimmed pipelines when first used: stalls in
     its first minutes, then judge the windows after. Checked: configs/banjo_renut_trim_poison_test.toml (trimmed
     channels set to 60000) looked the same on the PC. (b) depth-only draws without their pixel shader
     (banjo_gpu_depth_only, A/B 11): every such draw in the levels played uses a shadow-caster shader that cuts out
     with a kill, which must stay, so it applies to almost nothing in Banjo; it stays on for any draw that qualifies.
  5. The pipeline cache is saved once no pipeline was built for 15 s (as Unleashed saves at loading screens), not every
     minute while compiling: the save holds the driver's lock a compile waits for (banjo_renut_cache_save_quiet).
  6. Package 27c's flash fix is still in: please tell me if a white/teal flash still happens.
  Runs: (a) renut.toml as it is: title, then a level, stock clocks: send the logs folder (logs/rex too). (b) the same
  with configs/banjo_renut_words_ab.toml, then configs/banjo_renut_trim_ab.toml, a minute or two each in a level.
  (c) If anything looks wrong (colours, missing shadows): configs/banjo_renut_trim_poison_test.toml must look exactly
  like renut.toml; a screenshot of each in the same place. (d) A note on (b): run it after a level has been played
  once with renut.toml, so the trimmed pipelines are built.

Earlier changes (2026-10-17 11:34, package 27c: your two bugs, stream checks off the render thread)
------------------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (68,249,797 bytes, md5 d970d9a21b23...) and the configs folder (one new config). renut.toml,
  pipeline_list.bin and the shader package are as package 27b's.
  1. Status Monitor FPS stuck at 19.4-19.7 with Renut's renderer: fixed. Renut's renderer never counted its presented
     frames for the console's FPS (only our renderer did), so the overlay kept an old number.
  2. The one-frame white/teal flashes (your two videos: the scene behind the title logo replaced for one frame, the
     HUD fine): a likely cause found in Renut's code and fixed. At the end of each frame Renut told the game the frame
     was finished before it had read the frame's last vertex data (and at every kick, before reading CPU-written
     textures). If the game reused a buffer straight away, the old frame's last draws (the composite) took the new
     bytes. A shader compile holding the render thread at that moment makes it more likely. Renut now reads them
     first. The PC never showed the flash, even with compiles slowed to console speed, so please watch for it: if it
     still happens, a video and the logs again.
  3. Stream checks off the render thread (they were a quarter of its time on your console): a worker thread hashes
     them at each kick, when the game has issued every draw that reads them; the render thread only takes the
     results (77-90 % of them on the PC). Checked: a verify run compared all of them, 0 differences. Plus a faster
     hash loop (prefetch) and no re-reading of the same buffer within one kick.
     configs/banjo_renut_checkworker_ab.toml switches the worker every 4 s ("[renut A/B]" lines, target 10).
  4. The profiler window's GPU Frame and Present rows now work with Renut's renderer, and there is a new "Render thread
     (Renut)" row: that thread is the one limiting the frame rate most of the time.
  5. The pipeline workers now get their low priority (the console had refused 0x3F; 0x3B, as UnleashedRecomp-NX).
  Runs: (a) configs/banjo_renderer_renut.toml copied over renut.toml, the title screen for a few minutes then a
  level, stock clocks: any flash? In the log, "[renut] check worker" and the render thread ms in "[renut]".
  (b) configs/banjo_renut_checkworker_ab.toml in a level: what the worker gains. Then package 27's list below.

Earlier changes (2026-10-17 10:13, package 27b: Renut's renderer's boot crash fixed, profiler rows)
------------------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (68,102,341 bytes, md5 2206acd60fdd...). Everything else as package 27 (below): renut.toml, the
  configs folder, pipeline_list.bin; the shader package is unchanged.
  1. The crash at boot with Renut's renderer (your logs of 10:04: a memory access fault right after "Renderer: Vulkan
     on NVIDIA Tegra X1", in VmaDeviceMemoryBlock::Map): the NRO held two copies of the Vulkan memory allocator,
     plume's (Renut's renderer) and the SDK's, whose internal C++ classes had the same names. On the PC the SDK's copy
     is in a DLL, so they never met; on the Switch the linker kept one of each, and plume's first upload buffer went
     through the other copy's code. Plume's copy now has names of its own (tools/plume_vma_names.py; checked: no
     allocator symbol shared any more). It was there since package 26, so Renut's renderer has not run on a console
     before this build: if it stops somewhere else, send the logs the same way.
  2. The profiler window (banjo_show_profiler, on in every config) now fills "GPU Frame" and "Present" with Renut's
     renderer (both said n/a before: nothing fed them), and has a new row "Render thread (Renut)": that thread's busy
     ms a frame, the one to watch on the console.
  Checked on the PC: the intro route into Spiral Mountain with Renut's renderer, 30.00 fps, no crash.
  Runs: as package 27's list below, starting with (a) configs/banjo_renderer_renut.toml copied over renut.toml.

Earlier changes (2026-10-17 08:48, package 27: the rest of Renut's renderer, a codegen round)
------------------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (68,049,093 bytes, md5 0725e94a5f0a...), renut.toml, the configs folder (NEW configs) and
  pipeline_list.bin (CHANGED, next to the NRO). The shader package banjo_shaders.mesp + .idx and
  cache/masseffect_native_pipelines.bin are unchanged from package 26 (our renderer's shaders did not change).
  Renut's renderer's pipeline cache (cache/renut_pipelines.bin) starts over by itself (its shaders changed): the
  first boot with Renut's renderer compiles again, the second loads the cache.
  1. Renut's renderer has everything of ours that applies to it (docs/renut-renderer-port.md, section 7):
     - NVK's per-draw improvements and dynamic descriptors by differences (banjo_gpu_nvk_draw_contract and its
       part keys), pipelines without robustness (banjo_gpu_robustness_off): nobody asked NVK for them with
       Renut's renderer before. "[renut] NVK" lines every 10 s give NVK's own times and guard states;
     - its shader translator now has our items: bool constants as specialization constants (specialised
       pipelines built in the background, the generic one meanwhile), early alpha (3,663 pixel shaders),
       literal constants from the constant bank, bone rows in one load, bone fetches without their branch,
       blend skip, the tessellation grid (the water: on the PC route 3.0 million vertex shader runs instead of
       15.2 million every 10 s);
     - its memory sized for the console, not a PC (your "no texture eviction glitches like sa2"): only textures
       not drawn for 120 frames are released, only when the driver's own budget is short, never one the GPU wrote;
       the stream cache 128 MB, the upload ring trimmed sooner. A forced-eviction test on the PC (every 32 frames,
       released slots shown magenta): no magenta, the same pictures;
     - its cached vertex and index copies are checked at every GPU kick and converted again if the game rewrote
       them (before: checked every 4 frames, so a rewritten buffer could draw old data for a few frames). This
       costs render thread time: on the PC +1.5 ms a frame, on the console perhaps 6-8 ms. Run (b) says how much;
       if it is a lot, banjo_renut_exact_off.toml is Renut's own behaviour (no stale data was ever seen on the PC
       with either).
  2. Codegen round (both renderers: the game's own code), 15 ideas audited (docs/codegen-enhancements.md,
     "Package 27's codegen round"), 3 taken: the vector select as one NEON instruction (2,813 sites), the 1,273
     functions your console profile samples placed together in the ELF (they now sit in 584 4 KB pages instead of
     1,449: fewer instruction TLB and cache misses), tiny functions inlined into their callers' files (9,909 calls).
     The 12 not taken are listed there with why (two were localized copies: one measured 26 % dearer on the A57,
     the other its guard could not check; inline atomics turned out to change nothing).
  Checked on the PC (console settings, intro route into Spiral Mountain): Renut's renderer with everything above,
  30.00 fps, every picture right, no magenta, no crash; the verify mode with Renut's renderer: every generated-code
  self-check and the localized copies' guard (45 million calls compared) at 0 differences; the forced-eviction test:
  no magenta. Renut's renderer's report now prints those self-checks too ("[verify]" lines).
  Runs to make, in this order:
    a) configs/banjo_renderer_renut.toml: Renut's renderer, the usual route with a level, played twice (the second
       boot has the pipeline cache). In the log: "[renut]" (fps, render thread and GPU ms a frame; the exact
       checks and the textures released under the reserve), "[renut] NVK parts" / "NVK improvements", "Memory:
       heap", "[frames]". Any texture that looks wrong or flashes: note where.
    b) configs/banjo_renut_exact_ab.toml on a steady scene in a level: the "[renut A/B]" lines (target 6) give fps,
       render thread and GPU ms with and without the stream checks every 4 s. If "on" is much slower, also play a
       run of configs/banjo_renut_exact_off.toml.
    c) renut.toml as it is (our renderer), the same route: the codegen round with our renderer.
    d) Renut's other in-run A/Bs on a steady scene ("[renut A/B]" lines): configs/banjo_renut_boolspec_ab.toml (7),
       banjo_renut_blendskip_ab.toml (8), banjo_renut_tessgrid_ab.toml (9, near the water).
    e) configs/banjo_renut_eviction_test.toml (a test, not to play with: textures released every 32 frames and
       released slots magenta: any magenta on screen is a bug, send a photo and the log).
    f) If (a) is not smooth: configs/banjo_renut_memory_policy_off.toml (Renut's own memory limits), one run.

Earlier changes (2026-10-17 03:35, package 26: Renut's renderer in the NRO, a shader round, frame pacing)
------------------------------------------------------------------------------------------------------------------------
  COPY EVERYTHING: banjo-nx.nro (65,964,229 bytes, md5 af9aa1dd30d4...), renut.toml, banjo_diagnostics.toml,
  banjo_fragments.toml, the configs folder, the shader package banjo_shaders.mesp + .idx (CHANGED: replace the old
  one), cache/masseffect_native_pipelines.bin (unchanged prewarm list) and pipeline_list.bin (NEW, next to the NRO:
  the pipelines Renut's renderer builds ahead, from the PC).
  1. Frame pacing (your "it likes 20 if it cannot hold 30"): renut.toml now has target_refreshRate = 0 and
     frame_cap = "30". The game no longer waits for the display's vblanks itself (which rounded every frame to
     33 or 50 ms); the frame cap holds 30 and anything between 20 and 30 shows as it is. configs/banjo_pacing_game.toml
     puts the game's own pacing back (to compare).
  2. TWO RENDERERS IN ONE NRO, chosen at boot with banjo_renderer in renut.toml:
       banjo_renderer = "native"  banjo-nx's renderer, as before (the default)
       banjo_renderer = "renut"   Renut upstream's renderer (github.com/masterspike52/reNut): the game's Direct3D runs
                                  natively through plume/Vulkan, with no PM4 ring, no EDRAM and no tile emulation.
     To try Renut's renderer: copy configs/banjo_renderer_renut.toml over renut.toml (it is renut.toml with that
     key, its GPU report every 10 s, the same pacing, and its bloom/blur buffers in the console's 8-bit format;
     configs/banjo_renut_post_high.toml is Renut's own 16-bit default, more GPU bandwidth). On the PC (same route,
     both uncapped) it ran about twice as many frames as ours, and banjo-nx's tuning keys gave it 15-20 % of that. It is new on the console: it has never run on NVK
     before tonight, and the first boot compiles its pipelines (cache/renut_pipelines.bin keeps them for the next).
     If the log says "Shader ... is not in the shader cache", send the shader_capture folder next to the NRO: those
     shaders go into the next build's cache (the PC route only reached the intro and Spiral Mountain).
     banjo-nx's work in it (docs/renut-renderer-port.md): Switch support (libnx window, UnleashedRecomp-NX's plume
     Switch backend, the static NVK); in its shader translator: constants in uniform buffers (constant banks
     instead of memory loads), branchless select, clamp bounds from constants, predicated blocks without branches
     (heavy pixel shaders keep about a third of their branches) and a branchless bone fetch (skinning vertex
     shaders 178 -> 25 branches); its render thread pinned to core 1 and its background threads to core 2, a
     persistent pipeline cache, precise barriers, cached upload memory, CRC hashes with PMULL, the push constant
     only where it is read, draws queued without heap allocations or a wake-up call each, repeated constants not
     uploaded again, NEON byte swaps for the vertex conversion, shader stages compiled together so the driver drops
     the vertex outputs nobody reads. The game-code (CPU/codegen) work runs with either renderer.
  3. Our renderer: two more exact items, each with an in-run A/B:
     - literal shader constants read from the constant registers Direct3D loaded them into (107-117 moves fewer in
       the in-place material shaders), A/B 56: configs/banjo_literals_cbuf_ab.toml;
     - the Clear of a small stencil rectangle (once a frame in the levels, the 96-tile import9) fetches only the
       tiles it covers in part, A/B 57: configs/banjo_stencil_clear_partial_ab.toml.
     configs/banjo_import9_steps.toml times each step of the 9-pass import, to see where its time goes.
  Checked on the PC: both renderers over the intro route with the console's settings (no crash, no DIFFERENCE,
  pictures checked; Renut's into Spiral Mountain gameplay, 300 s at a steady 30 fps); our renderer in the verify mode
  with every check on: 0 differences; the pacing fix holds 30 with both; the Switch NRO links with both renderers.
  Runs to make, in this order:
    a) renut.toml as it is (our renderer, new pacing): the usual route with a level. Is 20-30 smooth now?
    b) configs/banjo_renderer_renut.toml: the same route with Renut's renderer. In the log: "[renut]" lines (fps,
       render thread and GPU ms a frame every 10 s), "[frames]", "GPU profile". Play twice: the second boot loads
       the pipeline cache. If it crashes or draws wrong, send the log (rex_stderr.log too).
    c) If (b) runs: configs/banjo_renut_nomsaa.toml (Renut's renderer with the game's 2x MSAA off: one sample per
       pixel, the image our renderer draws; Renut otherwise draws real 2x MSAA, more GPU work),
       configs/banjo_renut_tuning_off.toml (Renut's renderer without banjo-nx's tuning) and
       the in-run A/Bs configs/banjo_renut_barriers_ab.toml, _pin_ab, _dedupe_ab, _push_ab, _pool_ab ("[renut A/B]"
       lines: both halves' fps, render thread and GPU ms a frame; best on a steady scene, a level).
    d) configs/banjo_literals_cbuf_ab.toml and banjo_stencil_clear_partial_ab.toml (our renderer).
    e) banjo_diagnostics.toml and banjo_fragments.toml as before; configs/banjo_import9_steps.toml on the Banjo
       jumping screen.

Earlier changes (2026-10-16 21:40, package 25: prover removed, deferred recording on, a shader round)
------------------------------------------------------------------------------------------------------------------
  COPY EVERYTHING: banjo-nx.nro (61,257,925 bytes, md5 df738e623604...), renut.toml, banjo_diagnostics.toml,
  banjo_fragments.toml, the configs folder, the shader package banjo_shaders.mesp + .idx (CHANGED: replace the old
  one) and cache/masseffect_native_pipelines.bin (the prewarm list made from your own console's cache: your 1,721
  pipelines; the first boot compiles them again for the new shaders, in the background).
  1. The ring prover is gone (your prover-off run: the CPU at 202 % against 227-280 % with it, the same fps, the
     executor no slower per draw). Its configs are gone too.
  2. Deferred recording is ON by default, its worker on core 0 (24b's A/B: -12 % of the executor's work a draw,
     +5 % frames). configs/banjo_deferred_off.toml turns it off; banjo_deferred_ab.toml switches it every 4 s.
  3. Shader round (six exact changes, each with its own A/B target, docs/shader-translator-ideas.md):
     - the Xenos clamp bounds read from constants instead of moved immediates (12-27 instructions a heavy shader),
     - pipeline robustness off for the game's pipelines (2 fewer instructions per texture handle),
     - no sign mask on textures without signs (1 fewer per handle),
     - vertex outputs the pixel shader does not read are no longer written (16 of the skinning shader's 23 stores),
     - bone palettes padded with zeros (48 fewer selects a vertex in the skinning shaders),
     - the 9-pass stencil import (the screen after the title) no longer queries texture sizes in every pixel.
  Checked on the PC: 240 s with every check on (0 differences); the six switched every 4 s in two recordings (menus,
  then the intro and the level): the same picture at every switch.
  Runs to make, in this order:
    a) renut.toml as it is: the same route as before, including the Banjo jumping screen and a level (fps, CPU, GPU).
    b) configs/banjo_shaders25_ab.toml: the six switched every 4 s (the "GPU A/B" and "ring thread A/B" lines).
    c) configs/banjo_stencil_fetch_3d_ab.toml on the Banjo jumping screen (the 3D stencil passes against the copy
       engine; never measured on the console).
    d) banjo_diagnostics.toml and banjo_fragments.toml (the shaders' statistics and NAK assembly of this package).
    e) Optional, LAST and short: configs/banjo_zcull_greater.toml (the driver's ZCULL for the scene's reversed depth).
       If the picture freezes or the console hangs, stop: UnleashedRecomp lost the GPU with ZCULL changes.
  If a picture looks wrong: configs/banjo_shaders25_off.toml (the six off) and tell me which.

Earlier changes (2026-10-16 19:04, package 24b: package 24 with its two console faults fixed)
-------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (61,290,693 bytes, md5 e84ad1d97253...). Nothing else changed (same configs, same shader package).
  Use 24b, not 24.
  1. The crash at the title screen (std::terminate after "ring prover ... thread at priority 0x2C"): the ring
     prover's thread was detached, and the Switch's thread library has no detach (it throws). Packages 23 and 23b
     had the same fault. The thread is now made the way the ring's and the decoder's are, with a 512 KB stack, and
     kept for the whole run (as battd_nx and fruitninja_nx keep theirs).
  2. "DIFFERENCE: direct-state record 41 ..." in the log right after start (the records then switched themselves
     off): the record ring's lines are zeroed ahead with DC ZVA, which zeroes the whole aligned 64-byte block, and
     the ring was not 64-byte aligned, so each zeroing could wipe the end of the record just written. The ring is
     4 KB-aligned now (and the PC's tests zero the same blocks the console does). The check caught it at once and the
     game drew from the packets, so nothing was drawn wrong.
  Checked on the PC again (300 s with every record checked: 0 differences). The runs to make are package 24's,
  below.

Earlier changes (2026-10-16 18:32, package 24: no PM4 on the decoder, the prover's finer clearing, ARM fixes)
------------------------------------------------------------------------------------------------------------------------
  COPY: banjo-nx.nro (61,290,693 bytes, md5 fea9a2aae5c6...), banjo_diagnostics.toml, banjo_fragments.toml and the
  configs folder. The shader package is package 21's (unchanged: no cache rebuild). Everything of 23b is in it.

  1. Steps 4-6 of the Direct3D replacement (docs/d3d-full-replacement.md): every Direct3D call that writes commands
     has them turned into host records the decoder applies (no PM4 parsing for them: on the PC 97 % of the stream);
     the native draw path writes its records itself; the game's submissions reach the decoder through a host queue
     (the primary ring is no longer read). New 10 s report lines: "Direct3D records", "host submissions", "buffers
     kept as records", "PM4 parsed outside records".
  2. The ring prover clears a segment up to its first wait the game has not released (step 7b): on the PC it now
     fingerprints 92 % of the vertex windows the executor needs (was about a third). This is the part that should
     help the frame rate: the executor fingerprinted those itself before (11 % of the ring in package 21b).
  3. The executor no longer does the decoder's record bookkeeping at each Swap; shader loads are fingerprinted ahead
     too.
  4. ARM audit of the new code: thread-local reads (a function call on the Switch) cut from 2-4 to 1 per Direct3D
     call, an atomic loop removed from the most frequent hook, NEON byte swaps, the record ring's lines zeroed ahead
     (DC ZVA) so its writes do not read DRAM first.
  All checked on the PC: verify mode and a normal run with every record checked, 300 s each, 0 differences.

  Most useful runs (copy the file over renut.toml; send the logs folder after each):
    a) banjo_diagnostics.toml, 3-4 minutes of play: the profiler (where the executor's time goes now), the ring
       split line, the prover line ("the ring took N fingerprints ... read M windows itself").
    b) configs/banjo_ring_prover_fine_ab.toml (target 48: the finer clearing on and off every 4 s; the "ring thread
       A/B" line).
    c) configs/banjo_d3d_records_ab.toml (target 45: the records on and off; the decoder's and game threads' cost).
    d) configs/banjo_deferred_ab.toml and configs/banjo_deferred_ab_core0.toml (the driver calls on a worker: core 2
       next to the decoder, or core 0 next to the main thread).
  Also there: banjo_d3d_records_native_ab.toml (46), banjo_d3d_host_submit_ab.toml (47), banjo_cpu_verify.toml (every
  check on every use: slow, but it would show a console-only difference: look for "differences: 0").
  If something goes wrong (a hang, a crash, wrong pictures): banjo_d3d_records_off.toml (steps 4-6 off), then
  banjo_ring_decoder_off.toml; tell me which one fixes it.

Earlier changes (2026-10-16 15:45, package 23b: the ring split, the ring prover, the codegen agent's round)
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
