# banjo-nx on the ultimate base: progress log

Times AEST. One line per step; the details are in docs/banjo-ultimate-notes.md, docs/banjo-near-exact.md and the commits.

## 2026-11-19

- 09:40 Read the base's README, docs (building, changelog, ultimate-fixes, off-by-default, the porting prompt, xdk-notes,
  katamari-notes' layout sections). The base fetched into banjo-nx's object store as `refs/ultbase/ultimate` (no remote,
  master untouched).
- 10:05 Inventory: the base's first commit is banjo-nx 894d3256 plus 13 files banjo-nx committed later in 1.0.0; the
  Katamari era ends at 701c9d32 (katamari-nx 1.0.1), the ultimate ports start at 6929e720. Classes of every file
  (tools/ult_rebase_classify.py). Worktree `D:\Banjo kazooie port\banjo-nx-ult`, branch `ultimate` from master.
- 10:10 Commit 2c99d6de: the base's tree at 6cd02ae7.
- 10:12-10:35 tools/ult_rebase_apply.py: Katamari's layer out (6f00644a), Banjo's game layer back (fc79fa18), Renut at
  Banjo's XDK (7cb428f4: 6 conflict blocks; port 87's Viva Pinata offsets, port 169's scaler copy, the viewport fixed),
  Banjo's later work in the shared files (77cceaf5: 78 conflict blocks in 20 files), docs (1f60a14c).
- 10:40 PC build: the first configure stopped at `generated/rexglue.cmake` (generator output, untracked in both trees:
  the base's copy, with port 69's PCH byproduct, put in app/generated) and at vcvars (vswhere off the PATH: the VS
  Installer folder added). The shader dump and glslang copied from banjo-nx's out/ (game-derived, not committed).
- 11:05 FFmpeg's configure output, DXC's binaries and a few ignored SDK files copied from the base's working tree.
- 11:25 Code generation ran (407 files, 39,926 functions); post_codegen stopped at indirect_calls (19 sites not found):
  the site key fixed (named functions by their own address), the list re-keyed (b84f4e37).
- 11:35-11:50 Build fixes (a duplicate A/B case, a duplicate FetchSpecialised, a configure after the code generation);
  renut.exe linked. cc_entry_census: v0 read by sub_82402910 (kept live at indirect calls); cc_exit_census: every
  read value-independent (5510bbe5).
- 12:09 PC boot (boot1): title update 3, the save, Showdown Town in the vehicle at ~140 s. run_pc.ps1's arguments quoted.
- 12:19 PC verify (verify1, 480 s): 0 differences but one bulk copy (sub_82C41318, f3, a single denormal against -0.0):
  `off` in the list until explained.
- 12:42-13:15 CG-13 / near-exact censuses (cg13_town1 460 s, cg13_town2 620 s with the vehicle left): CG-13 harmless
  (audio and VMX-consumed sites), ON; #1 #2 #17 #5 #19 cleared, #41 (12 in the boot) and #42 count. The denormal site
  report widened to 256. uam built from the base's sources (ports 162/163).
- 13:20 r1_preserve.py / lr_preserve.py imported banjo-nx master's leaf_dead_stores.py (a hard-coded path): fixed.
- 13:25 The user: rumble stays on by default; the game menu (A+B+X+Y+L+R) to show every key of 1.0.2's renut.toml.
  app/src/banjo_menu.cpp through a hook in the base's menu (SetGameMenuRows): 25 rows in the toml's order, in the
  menu's twelve languages.
- 13:47 The Switch shader package: 4,916 shaders translated and compiled with uam (0 failed), 332 MB of GLSL -> 29 MB.
- 13:48 dist/switch/renut-nx: renut.toml (shipped + logs), renut_play.toml (shipped), renut_ab.toml (A/B 83, 150, 86,
  control 1; 30 s halves).
- 14:10-15:20 The Switch NRO built and put on the user's card (sys-ftpd; 1.0.2's files renamed *-1.0.2); the DKSH
  cache for the 4,916 shaders started (tools/deko3d_variant_cache.py, 53,113 variants). Menu verified by its own
  check (banjo_menu_check: 32 rows, 25 of 25 keys, 12 languages).
- 15:30-16:00 The method restated (a full rebase: the base's form wherever it has one). Shared files audited: logging
  (banjo-nx's logging_off), input_script (banjo-nx's stick moves), the GLSL optimiser (a duplicate fetchspec pass)
  back to the base's; banjo_renut_diag_skip_ps left behind (f21ed4b8). Plan lists (a) / (b) / (c). The handoff patch
  carries list (c)'s grafts (the GLSL loader, draw variants, rex_profile.log only with logs): 21 files (48a5d4bc).
  The GLSL optimiser's change gives the same GLSL for N&B (the pass at the same place; it changes no pixel shader), so
  the cache being built stays valid.
- 16:00 Not-exact censuses without a run: #9 r1 (8,143 functions assumed beyond the proof, REX_R1_CHECK to test them),
  no trapping math (the 5 CRT FPSCR writers keep -ftrapping-math: blur-nx's CMake form grafted; the base put the option
  on every file). The tree-wide frame elisions need a frame-exact test that N&B does not have: off.
