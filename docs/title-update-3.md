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

## The function table under the base image (found 2026-10-29, fixed in the SDK)

The base `default.xex` has an image of 0x10A0000 bytes, the update's 0xF20000. The SDK puts the indirect-call table
at the image's end by the generated `REX_IMAGE_SIZE` (the patched size: 0x82F20000) before the XEX is loaded; the
loader then loads the base image (to 0x830A0000) over the table's first 1.5 MB, and `XexModule::ApplyPatch` zeroed
and decommitted everything past the new image size. Every slot for the code at 0x821B0000-0x82270000 was empty, so
each indirect call there went through `ResolveIndirectFunction` and `FunctionDispatcher::GetFunction` (a recursive
mutex and a map lookup): 2.7 % of the main thread in the first console run of the update. It also made the PC's
promoted-call self-check report those targets as "not the function the generated code calls" (77 targets, all below
0x8226A000), which is why the first promotion list was cut to 57.

Fix (`sdk/src/system`): `ApplyPatch` no longer decommits a function table, and `UserModule` writes every registered
function into the table again after a successful patch (`FunctionDispatcher::RewriteFunctionTable`; the log says
"XEX patch: N function table entries written again"). Any port that runs a title update whose image is smaller than
the base game's needs the same.

## Indirect promotions again (2026-10-29)

`app/indirect_promotions.txt` made again: 287 sites. A PC census of the update's code (`banjo_diag_indirect_census`,
the scripted route then the sticks wandering, 15 minutes: 3,168 sites, 345 million calls) merged with the 57 sites
mapped from the base, without targets whose table entry is another function by design (native and localized copies
of `config/hot_functions.txt` and the copy lists, app hooks); then a second census on that tree (the first census's
tree still had the first list's promotions, whose calls a census does not see: a list change needs a fresh codegen,
`post_codegen.py` never undoes a promotion) added 17 sites. PC check (the route, 9 minutes, the native copies under a
heavy guard): 188 million promoted calls and 0 wrong, 502 native copies 0 differences.

The promoted-call self-check (`RexPromotedMismatch`, `app/src/cpu/banjo_codegen_checks.cpp`) now names each target
once; it named the first 16 calls before, which were the same few targets, so a list made from them left the rest in.

## PGO with a level (2026-10-29)

The first profile on the update's code (the 0654 package) had boot and the menus only: its emulator session ran in
an old Ryujinx data folder whose shader files did not match (0.3-3 fps, it never left the menus). Taken again with the
287-site tree: the instrumented build (`app/out/switch-pgo-gen`), Ryujinx in the current data folder, the scripted
route (`route_script.txt`) into the game and then `input_script_sticks` wandering, 15 minutes (13 captures: Showdown
Town on foot and in a vehicle from about 2 minutes in), merged with `tools/pgo_merge.py` into `pgo/` (315 files),
then a clean `switch-dist` build (ninja does not see a changed `.gcda`). The menus-only profile is kept in
`../pgo_profile_tu_menus`.

## 1.0.0's font and dialogue boxes (2026-10-30, the user's request)

The update is Rare's December 2008 patch for small text on SD TVs. It changed three things about text:

- **Text size.** The game decides between big and normal text in `sub_823B42D8` / `sub_823B43D0`:
  - It uses the profile byte at +131 (bit 0x2 = set, bit 0x4 = big) when the profile has one.
  - Otherwise it uses the word at 0x82E0BD58, where 0 means big.
  - Big text takes the stylesheet's `speechbig_prefix` (`sub_8255CDA0` loads the four styles).
  - The options menu has a "Use big font" tick box (`useBigFontButton`, `sub_82560958`).
- **The font.** `sub_822F4EE8` picks the font cache's ini files by language:
  - 1.0.0 sent English (0) to `metafont_euro.ini` and `euro.sbp`, which is Andy Bold.
  - The update sends every Latin and Cyrillic language to the russian files, a bold Comic Sans. Both inis claim to
    be "MS Comic Sans".
  - Both .sbp files hold the same 33,974 glyphs (Latin, Cyrillic and CJK) in the same 48x48 cells.
  - The format: a 6-byte header with the count at +4; then 10-byte entries (u16 char, u32 file offset, s8 ?, s8 left
    bearing, u8 width, s8 right bearing); then one zlib stream per glyph, 4 bits a pixel.
- **The dialogue boxes.**
  - XUI scenes are named `<type>_<project>_<name>.<ext>` (`sub_823B7950`; the tables are at 0x82DA29B4).
  - Each scene has a descriptor: +0 type, +4 package (the table at 0x82D90C50), +8 name, +268 project.
  - The update moved `ingame_dialogdisplay` (0x82D91D68) and `frontend_dialogdisplay` (0x82D979A8) from
    `BanjoX_Ingame` (2) and `BanjoX_CoreMP` (4) into its own always-loaded `BanjoX_TU15` (11), as banjoxdlc scenes.
    Their box is `aid_texture_banjoxdlc_ui_menu_dialogueboxnew`, with no cogs.
  - 1.0.0's scenes are still in the disc's bundles. The in-game one is in `Bundle/4f/685374`: the
    `aid_texture_banjox_ui_menu_dialoguebox` nine-grid and two cog images.
  - 1.0.0's scenes have every element the update's code binds except `scrollUpArrow` and `scrollDownArrow`. An
    element that is not found is a null handle, and every XUI call refuses one without harm.
  - The update added `background2`, the box for a speaker on the right (descriptor word +0 of the scene's start data,
    which also picks `dialogModelLeft` or `dialogModelRight`). 1.0.0's scene has a `background2` too.

Two keys, both off by default, applied at boot (`title_update.cpp`, `ApplyStyleOptions`, from
`RenutApp::OnPostSetup`). Each one checks the bytes it replaces first and logs a warning instead if they differ:

- `banjo_font_andy_bold`: the two russian ini paths in the image (0x82112AF4, 0x82112B20) are rewritten to the euro
  ones, so every Latin and Cyrillic language gets Andy Bold at the update's size.
- `banjo_dialogue_boxes_original`: the two scene descriptors get their 1.0.0 package and the banjox project back.
- Text sizes (`banjo_text_style_log_s`, a PC diagnostic: it logs the inputs and the four style prefixes):
  - The speech styles are `<FONT SIZE="29">` (big) and `"18"` (normal); info text is 24 and 18.
  - The default word at 0x82E0BD58 is 0 in the image, but the game sets it to 1 itself during boot. So on an HD screen
    the text is normal-size (18) unless the profile ticks "Use big font". All the PC checks above were at 18.
  - A boot-time write of the word does nothing, because the game overwrites it.
  - Big text (29) in 1.0.0's box is checked on the console with the tick box (2026-10-30).
- Switch build in Ryujinx (NRO 2eaaf3175534, 2026-10-30 22:25): both keys log "2 of 2" (the page unprotect and
  restore work there), no crash, title screen right.

The code and read-only data pages are read-only on the host, the Switch included (`XexModule` page descriptors), so
the writes unprotect the page and then restore it. The big-text choice is not changed: the update's tick box still
works.

Tools:
- Scratchpad `sbp_text.py` renders a line of text from an .sbp file.
- Scratchpad `xdec/xdec.exe` is reNut's `mods/xcompress.cpp` built for the host; it decompresses a bundle.

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
  0.34 similarity) needs checking by hand first. reNut upstream itself replaced its pause tab with a GRAPHICS row in
  Display Settings (`src/renut_engine/game/graphics_menu.cpp` at 0a00f06, on the update's addresses: the pause's and
  the main menu's Display Settings scene, Game Information's list as the options page); taking it is the simpler way
  back to an in-game settings menu, with banjo-nx's own option list (its options are the `renut_*` keys of its Vulkan
  renderer) and a check through the main menu's Display Settings.
- The native renderer (`app/src/me`, `app/src/native`) reads the base's CDevice layout: Renut's renderer runs whatever
  `banjo_renderer` says.
- PGO: the profile's generated-code functions have new names; the renderer and SDK parts still match. (Done
  2026-10-29: see "PGO with a level" below.)

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
  the base's 187; the census in `banjo_codegen_checks.cpp` makes a fresh list from a PC run). Later that day: the
  "seven others" were empty table slots ("The function table under the base image"), and the list is 287 sites.
- `limiter_lazy_log10.py` matches the update's limiter again (one label: `loc_82B6A758`).
- Found on the way, the PC build had been broken since the deko3d-only render-thread rounds (helpers inside
  `#if defined(PLUME_DEKO3D)`, `far` as a variable name, two functions named `RepeatCensusReport`, the PGO hooks):
  fixed.
- Switch build in the emulator, title screen (intro movies, title).
- The PGO build (profile taken on the update's code, docs/pgo-lto.md) froze once in Ryujinx at the second intro
  movie, as one run of the build before it had at the title: every guest thread stopped together (the hot-copy
  reports' call counts flat), the host threads (uam compiles, reports) went on. In both, the stall watchdog
  (`banjo_stall_watch_seconds`, `profiler/stall_watch_switch.cpp`: it pauses each thread with
  `svcSetThreadActivity` to read its registers and stack) went silent at that moment: its next dump never came.
  Ryujinx's `KThread.SetActivity(pause)` waits in a loop that takes and drops the kernel's global critical section
  until the paused thread leaves its core; on a loaded host the thread it waits for can lose that lock to it every
  time, so the watchdog hangs with a thread left paused, and every guest thread then blocks on what that thread
  holds. The console's kernel takes the thread off its core at once, and the watchdog has run in every console
  package since it was ported from UnleashedRecomp-NX. With `banjo_stall_watch_seconds = 0` the same NRO ran clean twice
  (the movies, the title loop, 5,400 and 7,500 game frames in two 9-minute runs, no missing shader, no
  `[hot] DIFFERENCE`): emulator runs set it to 0 from now on; console packages keep it on. Packaged as
  `builds/2026-10-29_0654_tu3_dlc_pgo` (NRO 07a4a47f22b6).
