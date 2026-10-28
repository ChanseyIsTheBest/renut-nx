# Title update 3 and the DLC (2026-10-29)

banjo-nx is recompiled from Banjo-Kazooie: Nuts & Bolts **with title update 3** (title 4D5307ED, v0.0.3.1), as reNut
upstream is since its commit ab60251 ("updated to work with titleupdate and dlc"), and installs the **DLC** packages
(L.O.G.'s Lost Challenges) the way reNut does. This page is how the move was made and what to know when it is made
again (another update, another port of the family).

## What the player needs

| Where | What |
| --- | --- |
| game folder (`game_data_root`; on the Switch `sdmc:/switch/banjo-nx/game_root`) | the disc's files and `default.xex`, **plus the update's `default.xexp`** next to it |
| `update` folder (next to banjo-nx, or next to the game folder; `update_data_root` overrides) | the update's `db_index.txt` and its 12 bundle files |
| `dlc` folder next to banjo-nx (`renut_dlc_folder`) | the DLC package file(s) as they come (LIVE / PIRS / CON), e.g. `E41F20AAF7094B9AFCA8CC90489D689D72A5062B4D` |

On the Switch: `sdmc:/switch/banjo-nx/update/` and `sdmc:/switch/banjo-nx/dlc/`.

- The runtime applies `default.xexp` itself (`sdk/src/system/user_module.cpp`: a sibling `<xex>p`), and mounts the
  update folder as `update:` (`sdk/src/system/runtime.cpp`). `app/src/renut_engine/title_update.cpp` finds the update
  folder when `update_data_root` is not set, and stops with a message box when `default.xexp` is missing (the code is
  the update's: the base image would not match it).
- The update package itself (`TU_16L61VD_0000004000000.00000000000O1`, STFS LIVE) is unpacked with any STFS tool; the
  one used here is a Python port of the SDK's reader (STFS block chain, hash tables).
- DLC: `app/src/renut_engine/dlc_install.cpp` is reNut upstream's installer: at start every package in the `dlc` folder
  for this title and of content type 2 is installed once into the user data folder (`ContentManager::InstallContent`;
  `content/0000000000000000/4D5307ED/00000002/...`). Klungo's arcade 2 (the DLC's) is held at 30 fps like the first
  (`klungoDlcConstruct_hook` / `klungoDlcDestruct_hook`, `frameHooks.cpp`).
- Switch file reads go through a 1 MB host buffer (`sdk/src/core/filesystem_posix.cpp`, from peggle1-nx): a single
  large read straight into fresh guest memory froze the console in Peggle's DLC; this DLC's main file is 64 MB.

## What a title update changes

The update is a **recompile** of the game: almost every function moved, register allocation changed (hooks that name
registers), the XDK libraries were rebuilt (D3D, XAudio, XACT, the video codec: loop forms with `bdnz`, inlined
helpers), data moved (`.data` / `.bss` by about -0x190000), and some game structures changed size:

- **CDevice**: 4 bytes more from `0x2AC0`, 8 more from `0x2E30` (the base's `0x2E2C`), unchanged from `0x3390`. The
  device snapshot is 12,720 bytes (`kDeviceSnapshotSize 0x31B0`), its last chain chunk 48 bytes. Everything that knows
  the layout moved with it: `d3d.h` / `d3d_native.h` offsets (reNut upstream's values), `render/draw.h FieldOfOffset`
  (the record-time draw path's fields), `d3d_native.h kStateTagChunks` and the fetch tag table,
  `command_buffers.cpp TakeLastChunk`.
- Game classes: the player profile's unlock bytes (+24), the pause and display menus, co-op, part classes (reNut
  upstream's diff of ab60251 lists them). The scene table gained scenes (main menu 68 -> 76, start menu 70 -> 78).
- The section pools now take a lock of the game's own (`0x82E198E4`): banjo-nx's `banjo_pool_lock` (the base's race
  fix) is off by default, its host mutex would be the other lock order.

## How the addresses were moved

Tools (all in `tools/`, written for this; they work for any two executables of a game):

1. `func_signatures.py`: each generated function's signature (shape, text, skeleton, calls, mnemonic histogram), from
   the generator's `// mnemonic operands` comments.
2. `map_functions.py`: base -> update function map. Name anchors (reNut upstream's `renut_funcs.toml` and
   `renut_gpu_funcs.toml` of both versions), shape and skeleton anchors, longest increasing run of anchors, a
   similarity DP between anchors, a call-share check. `remap_addresses.py` adds two passes: a **name rescue** (a
   function both lists name the same, with counts within 15 % and histograms 60 % alike: the update reordered XACT) and
   hand-checked pairs (`tu_v3/map/manual.json`: reNut upstream's D3D name pairs and every constant upstream changed by
   hand in ab60251, the reverb's `.rdata` constants found in the patched image by their neighbours).
3. `remap_addresses.py`: an address inside a function is moved by aligning the two functions' instruction lists
   (`exact` / `replaced` / `unsure`); `--hooks` rewrites a hook file (shared hooks taken as upstream wrote them, ours
   mapped with their registers renamed by votes of the aligned operands); `--rewrite` rewrites sources, `--exclude`
   leaves values a file already has in their new form (upstream's own edits after a three-way merge).
4. `remap_lists.py`: the performance lists (hand natives and their `n_<id>` namespaces, localized and bulk copies, the
   hot layout, locals opt-outs, inline edges, indirect promotions, the `lr` / `r1` choice files): entries without a
   pair are dropped (each only loses its speed-up) and reported.
5. `tu_funcs_names.py`: the update's `renut_funcs.toml` = upstream's plus the base names our files use, and five
   corrections (upstream's TU list names the scalar resamplers `XAUDIO::SRC::Linear::Process<T,1>` as `LinearVector`).
6. `upstream_token_pairs.py`: the constants upstream changed in a commit, as old -> new pairs; the map agreed with 215
   of them and differs on 6 (two are upstream comments that are wrong).

Measured: 39,674 base functions, 39,926 in the update; 32,830 mapped (12,569 anchors); every hook (42) placed and
checked; 12 of the 13 hooks upstream moved by hand reproduced (the 13th, StopNSwap, upstream moved on purpose).

## What was derived again, not moved

The analysis-made lists of `renut_config.toml` are regenerated on the update's code (`tu_v3/g1`: our options, no
lists, `non_argument_as_local` off), with the base's own procedure:

- `non_argument_as_local_keep`: `non_argument_audit.py`'s ENTRY functions minus the save / restore helpers, plus
  `rex_CallSettingFrame` and `rex_C_ExecuteTerminationHandler` (905; the base's 1,225 had the same rule). RET:
  `rex_lock_fhandle`, the jump-table stub's two callers, and a new `f0` case whose callee is kept.
- `[functions] share_registers`: `read_before_write.py`'s reports triaged as the base's: a report whose base
  counterpart the base kept or dismissed gets the same decision; new ones by the base's rules (SEH funclets share; a
  function whose prologue saves the register owns it). 31 entries. The tool now reads `rex::ppc::CompareAnyMode(crN,
  ...)` as a write of `crN` (fp_compares_modeless; 450 false cr6 reports without it). The video codec's v14-v31 reads
  are on correlated branches (its wrappers set only v1): not arguments, not kept (check the intro movies).
- Indirect promotions: callers and targets mapped, a call kept only where the update's function has that many `bctrl`.

## What reNut upstream's commit gave directly

Three-way merge (`git merge-file`, base 99cb2f4, theirs ab60251) of the 12 renderer files upstream changed for the
update: 10 clean, `photo_capture.cpp` one conflict (the hook macro), `widescreen.cpp` taken as upstream's (it also
centres XUI differently on the update) with banjo-nx's hook macros. StopNSwap (the update's profile, seven new
unlocks), `Infinite_fuel_and_ammo` returning bool (`return_on_true`; the base's `return = true` returned whatever
the setting said), the parts-access hooks on r11, the debug menu's scene ids, the thread slot fix
(`thread_slots.cpp`) and the environment map pool (`env_map_pool.cpp`).

## Shaders the update brings

The update's XEX carries its own compile of the D3D, XUI and video shaders, so 23 shaders the game makes at runtime
have new hashes (the movies' and the title logo's among them: on the first Switch build the movies were black and the
logo missing, the log saying "Shader ... is not in the shader cache"). The PC renderer translates them on the fly; the
Switch build carries the runtime shaders in the NRO, so they go through the usual steps, on the NRO's own set (the 173
captures in `app/out/pc-me/shader_capture`):

1. Renut's XenosRecomp on those captures: the early-alpha dump (`BANJO_HLSL_DUMP_ONLY`), `tools/renut_early_alpha.py`,
   then the translation with `BANJO_HLSL_OVERRIDE` and `BANJO_DEKO3D_SPIRV_DIR` (SPIR-V per shader) into a
   `renut_gpu/shader_cache.cpp` of its own;
2. `tools/renut_switch_shader_cache.py --pc <that folder> --only <captures> --out-dir app/out/renut_switch_dist`;
3. `tools/deko3d_shaders.py`, `tools/deko3d_glsl_opt.py` (every pass, as the shipped GLSL), then
   `tools/deko3d_shader_package.py --only <captures> --raw --out app/out/renut_switch_dist`.

The 150 shaders of the October 22 package came out the same from the passes except 30, whose old GLSL predates the
dynamic resolution's scale codes (fixed at 1.0): the new package has all 173 at the current translator.

## Not carried over yet

- The reNut Settings pause tab (`cvar_menu.cpp`): the update inlined the section title setter (base `sub_825A7CF0`)
  into its two callers, and the pause object's offsets have not been checked on the update. The tab is not added;
  every other hook of the file passes through. Settings stay in `renut.toml`. For the port: the inlined title code is
  at `0x8258CF14-0x8258CF70` (in `sub_8258CEC8`, the base's `sub_825A7BD8`) and `0x8258C490-0x8258C4E0` (in
  `sub_8258C010`, the base's `sub_825A6DB8`); the section array is `0x82E1045C`, the title keys `0x82D8B7BC + 8 id`,
  and the update looks the text up with `sub_824FA180(key, buffer, 256, 1)` (the base used `sub_825EF418` with the
  string table first). A mid-asm hook on each `bl 0x824fa180` (id = `r10 >> 3` at `0x8258CF64`, `r11 >> 3` at
  `0x8258C4D4`) can write "reNut Settings" into `r4` and jump past the call. The input handler (`sub_8258C948`) reads
  the pause object at the base's offsets; the dispatch callback (base `sub_825A8D68`, mapped to `sub_8258E008` at
  0.34 similarity) needs checking by hand first.
- The native renderer (`app/src/me`, `app/src/native`) reads the base's CDevice layout: Renut's renderer runs whatever
  `banjo_renderer` says.
- PGO: the profile's generated-code functions have new names; the renderer and SDK parts still match.

- reNut upstream's `game/level_pools.cpp` (the section pools' host textures released when the game frees their memory,
  `ReleaseHostTexturesInMemoryLocked`, which banjo-nx's renderer has but nothing calls): a candidate for the next round;
  it hooks the pool reset `sub_823333F0`, which `banjo_pool_lock.cpp` hooks too (off by default now), and only a run
  through section changes shows its effect.
- reNut upstream's renderer work after 574b488 (99cb2f4, 0a00f06: ambient occlusion, its SMAA, parallel recording,
  video-memory pools for PCIe, the title logo and menu colour, bloom and depth-of-field strength) is for its Vulkan /
  D3D12 path, which banjo-nx's deko3d record-time path and its own SMAA replaced; online play (EOS) is not for the
  Switch. Its other game modules (controllers, mods, vehicle tweaks, graphics menu) are built on its newer game layer.

## Checks (2026-10-29)

- PC build, title screen with the update's data and the DLC installed (`tu_title2.log`): the patch applies
  ("new version: 0.0.3.1"), the game reads `UPDATE:\db_index.txt` and its bundles, the DLC installs (0.2 s), no crash.
- The native copies under a heavy guard (`banjo_hot_guard_calls = 4000`, `banjo_hot_guard_period = 16`): 477 copies
  reached, all on, 0 differences over 5.7 million calls (the first run found one: the constant writers' natives wrote
  the base compile's `std r31,-8(r1)` slot, which the update's compile does not have; removed). The reverb native
  under `banjo_native_reverb_verify`: 51,500 calls, 0 mismatching.
- Promoted indirect calls: the self-check (`REX_PROMOTED_CALL`) found targets whose dispatch entry is not the function
  the promoted call jumps to (localized copies, and seven others); every promotion naming one was dropped (57 left of
  the base's 187; the census in `banjo_codegen_checks.cpp` makes a fresh list from a PC run).
- `limiter_lazy_log10.py` matches the update's limiter again (one label: `loc_82B6A758`).
- Found on the way, the PC build had been broken since the deko3d-only render-thread rounds (helpers inside
  `#if defined(PLUME_DEKO3D)`, `far` as a variable name, two functions named `RepeatCensusReport`, the PGO hooks):
  fixed.
- Switch build in the emulator, title screen (intro movies, title).
