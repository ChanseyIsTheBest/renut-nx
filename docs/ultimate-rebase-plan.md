# banjo-nx on the ultimate base: the inventory and the method

Written 2026-11-19, before and while the code moved (branch `ultimate`, worktree `banjo-nx-ult`). The base is
renut_nx_ultimate_base (`D:\renut-ultimate\base`, branch `ultimate`) at **6cd02ae7** (ports 1-188); banjo-nx's
`master` (renut-nx 1.0.2 of 2026-11-05 and its docs commits, 8d61034d) stays as it is.

## What the base is, against banjo-nx

The base's first commit, 83b4ae77 ("Start katamari-nx from banjo-nx's structure", 2026-10-30), is banjo-nx's tree at
894d3256 (2026-10-29) plus 13 files of banjo-nx work that was then uncommitted and that banjo-nx committed later in
renut-nx 1.0.0 (the title update's function table fix in the SDK, the codegen checks, the first draw-variant and
deko3d tuning pieces), without banjo-nx's docs, game lists (emptied) and dist/switch. Its history then has two parts:

| Commits | What | Taken here |
|---|---|---|
| 83b4ae77..701c9d32 (2026-10-30 to 11-05) | katamari-nx: Beautiful Katamari's port up to its 1.0.1 | its universal parts (below); its game layer not |
| 6929e720..6cd02ae7 (2026-11-07 to 11-19) | the ultimate ports 1-188 from the other ports | all of them, with the per-XDK files at Banjo's XDK |

Comparisons (line ends ignored; `tools/ult_rebase_classify.py` writes `out/ult_rebase_classes.tsv`): 22,928 files the
same in the base's tip and banjo-nx's master; 226 the base changed and banjo-nx did not since the fork; 11 the other
way; 47 both; 403 new in the base, 303 new in banjo-nx (most of them dist/switch's tomls), 21 that the fork had and
banjo-nx never committed (tools it took from other ports).

## The inventory

### Banjo's own layer (what the base does not have): came back

| Piece | Files | How |
|---|---|---|
| The code generation lists and config | app/config/* (functions, hooks, the GPU functions, the CRT, hot functions and edges, the opt-outs, the r1 / lr choices), app/renut_config.toml, app/indirect_promotions.txt | banjo-nx's, as they are (no port changed the base's copies) |
| Hot and hand lists | app/src/cpu/hot/hand.txt, localized.txt, localized_bulk.txt | banjo-nx's (the Katamari era had made them Katamari's) |
| Hand natives | app/src/cpu/hot/banjo_hand_natives.h, cpu/banjo_hot_guest.cpp | banjo-nx's, with the ports' changes (three ways from katamari-nx 1.0.1); Katamari's 19 natives out; port 96's REX_FMADDS kept (the ports moved banjo-nx's natives to it) |
| N&B's own hooks | renut_engine/hooks.cpp (cheats, mid-asm hooks, N&B's msaa_level), frameHooks.cpp (the frame cap), FPS.cpp (appMainDraw: the frame boundary), StopNSwap.cpp, cvar_menu.cpp, me/me_ring_wait.cpp, guest/widescreen.cpp, guest/photo_capture.cpp, mnk_controls.cpp's keystroke hook, me/banjo_me_hooks.cpp (Swap and the PM4 renderer's D3D hooks) | banjo-nx's. The base had gated them (`KATAMARI_BANJO_ADDRESS_HOOKS` 0); no port touched them |
| Renut's hooks and device layout | d3d/hooks/*, d3d.h, d3d_native.h, d3d_native_state.cpp, banjo_shared_hooks.h, render/draw.h (field boundaries), guest/command_buffers.cpp (the snapshot's last chunk) | banjo-nx's at N&B's title update (XDK 2.0.7978), with the ports' changes three ways (ancestor katamari-nx 1.0.1, so Katamari's address moves stay out) |
| Title update 3 and the DLC | renut_engine/title_update.cpp/.h (the update folder, default.xexp check, the language and style keys), dlc_install.cpp | banjo-nx's; renut_app.h calls them again |
| Four-core mode | sdk threading_switch.cpp (the forwarder's NPDM, Sphaira's ids), tools/forwarder | the base already had banjo-nx's (through blur-nx), with blur-nx's game-menu switch; banjo_forwarder_program_id stays 05D6EA1333487000 |
| The Switch defaults | app/src/banjo_switch_default.h, sdk/include/rex/banjo_switch_default.h (logs off, the PM4 renderer off, SMAA, eager kicks, skin once, the guard off on the Switch) | banjo-nx's |
| PGO | app/src/banjo_pgo.cpp (the same in both) | the base's (pgo/ and pgo-inplace/ are Katamari's profiles: removed; Banjo's needs a new training run) |
| Tomls and package | dist/switch/banjo-nx (renut.toml, renut_play / _ab and the configs), dist/switch/configs | banjo-nx's |
| Icon, NRO name, version | tools/switch/icon/banjo_icon.jpg, app/icon/app.ico, app/CMakeLists.txt (NAME, AUTHOR, VERSION 1.0.2) | banjo-nx's |
| Docs | docs/ (renut-nx's method docs) | banjo-nx's; the base's pages in docs/ultimate/ |

### banjo-nx's work after 2026-10-29: what the base already had

| banjo-nx | In the base? | Outcome |
|---|---|---|
| 1.0.0 public build: logs off on the Switch | port 183 does stderr more thoroughly | the base's crash hooks (port 183); banjo-nx's log_level default (off on the Switch) kept |
| 1.0.0: the web installer's shader package beside the NRO | Katamari's version 1 (deko3d_glsl.bin, table in the NRO) | both: the base's version-1 reader plus banjo-nx's version 2 (OpenInstalled, the shader table in the file) |
| 1.0.0: Sphaira's forwarder ids | yes (blur-nx took banjo-nx's) | the base's |
| 1.0.0: draw variants, GLSL unpacked on demand (the OOM fix), memory report | urgent variants only (Katamari's copy) | banjo-nx's draw variants and GLSL loader merged into plume and pipeline.cpp; the base's later code moved to the loader's `text` |
| 1.0.0: the water fetch spec (banjo_gpu_fetch_spec) | yes (vivapinata-nx's copy, extended to every draw; its A/B is 83, banjo-nx's was 69) | the base's; banjo-nx's duplicate pass_fetchspec dropped |
| 1.0.1: the language key | Katamari's XGetLanguage reads user_language (compatible) | banjo-nx's title_update's ApplyLanguage, the base's XGetLanguage |
| 1.0.2: game memory and the NRO's code on 2 MB blocks | yes (katamari-nx's 1.0.1, which kameo-nx's and banjo-nx's are copies of) | the base's, with renut.toml as the file the code-block switch reads |
| stick moves in input_script ('LSx/y/duration') | the base has Katamari's axis pushes (another form) | both |

### The base's Katamari layer: removed

app/src/katamari_keys.cpp, katamari_builtin_config.h.in, cpu/katamari_stage_diag.cpp, app/katamari_switch*.toml,
app/overrides.toml (its pointer sweep), pgo/ and pgo-inplace/ (Katamari's profiles), dist/switch/katamari-nx,
tools/make_katamari_tomls.py, katamari_files.py, katamari_shader_files.py, the hook remap tools and map, its forwarder
NPDM and icon, its 19 hand natives, its DLC pack list (in dlc_install.cpp), its `[katamari]` device declaration offset
(+0x2F98: N&B's +0x2FA8). Also fixes/mullhwucrash.cpp, translated N&B code that neither tree built.

Katamari's keys file also held the base's frame limiter, which later ports changed (the game menu's live cap, pacman-nx's
vblank pacing, sa2-nx's queue drain): those went into banjo-nx's own limiter (frameHooks.cpp). Port 156 (game frames
counted at Swap) is not taken: N&B counts them in its own Swap hook.

## The method

1. **The base's tree whole, one commit** ("The ultimate base at 6cd02ae7"), line ends as this repository keeps them.
2. **Katamari's layer out**, one commit.
3. **Banjo's game layer back** (the files above), one commit.
4. **Renut at Banjo's XDK**: the per-XDK files (`tools/ult_port_sync.py`'s `--hand` list) and the two shared files the
   Katamari era moved to XDK 5632, as banjo-nx's with only ports 1-188's changes, three ways with ancestor katamari-nx
   1.0.1 (701c9d32); every hex offset a port wrote into them checked against N&B's layout.
5. **Banjo's later work in the shared files**: three ways, the base's tip with banjo-nx's changes since the fork
   (ancestor 83b4ae77). Where both had the same work, the base's form and banjo-nx's own lines (its toml's name, its
   defaults, its extra parts).
6. **Docs**: banjo-nx's back, the base's under docs/ultimate.

`tools/ult_rebase_apply.py` does steps 2-5 from the classes; `tools/ult_resolve.py` resolves a diff3 block by side.
84 conflict blocks in 26 files were resolved by hand (the commits name the decisions). Later base ports come in with
`tools/ult_port_sync.py` (docs/ultimate-sync.md).

## Found on the way (for the base)

- Port 87 (tiles once) has vivapinata-nx's XDK 3529 device offsets written in (0x2A30, 0x2A34, 0x30B8, 0x2A38): other
  fields at Katamari's and N&B's XDKs. Here it reads `native::off` (handoff: `out/ultimate_handoff_*`).
- Port 169's scaler copy at Katamari's +0x3584 and the device declaration at +0x2F98 are per-XDK values in shared files.
