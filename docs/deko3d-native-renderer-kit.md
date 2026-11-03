# A native deko3d renderer for the other ReXGlue Switch ports: the kit and the route (2026-10-19)

> Status 2026-10-28: this guide is older than the current renderer. Start at `docs/porting-start-here.md` (the reading order, what is current, the method rules) and see `docs/porting-guide-renderer.md` section 6b for the items measured slower on the console. Superseded here: "MSAA off and no resolution changes" (Banjo now has internal resolution, then dynamic resolution with steps).

For the other ReXGlue Switch ports: how to run reNut's API-level renderer on deko3d (no ring, no register file rebuilt from
packets, no Vulkan driver), which of banjo-nx's parts you can take as they are, the order of work that kept the picture
checkable at every step, and every trap we fell into: the Switch back end, the shader pipeline and the tuning, with Banjo's
console numbers.

## 0. What Banjo runs now (package 35, console, stock GPU clock)

- **The renderer:** Renut's Direct3D-level renderer (`app/src/renut_engine/gpu`, from reNut upstream by masterspike52,
  see the licence note below) on **plume's deko3d back end** (`app/thirdparty/plume/plume_deko3d*.{h,cpp}`, banjo-nx's
  own). The game's Direct3D calls are hooked (`D3DDevice_*`, the command buffer calls); recorded command buffers are
  captured as host ops and replayed on one render thread; no PM4 is written or parsed.
- **Shaders:** the game's shader containers go through Renut's XenosRecomp (HLSL -> SPIR-V), SPIRV-Cross (GLSL),
  `tools/deko3d_glsl_opt.py` (text passes), then **uam** (the deko3d shader compiler: Mesa's nouveau codegen). A DKSH
  cache built on the PC from the console's pipeline lists ships next to the NRO; the console compiles nothing in play.
- **Numbers** (scripted route, package 35 console logs): gameplay at the 30 fps cap with the render thread at 18 ms a
  frame (it was 38.7 ms in package 33 and 25 ms in package 34); GPU 25-30 ms a frame in normal scenes; the heaviest
  scenes 18-19 fps, GPU-bound.
- **Why deko3d**: a Vulkan driver's per-call costs measured on the A57 (pipeline bind about 3 µs, draw 2.4-2.9 µs,
  descriptor or UBO set 1.3-1.6 µs) are driver work deko3d does not have: it writes the GPU's command words in user
  space, with no validation or descriptor management. On the GPU side, uam with banjo-nx's passes took 25-42 % off
  the heavy scenes' GPU time between package 33 and the next build (`docs/deko3d-shader-optimisations.md`).

## 1. What you can take, and under what licence

| Part | Where (banjo-nx) | Licence | Reuse |
|---|---|---|---|
| plume deko3d back end | `app/thirdparty/plume/plume_deko3d.h`, `plume_deko3d.cpp`, `plume_deko3d_commands.cpp` (3,900 lines) | MIT (banjo-nx's, as plume) | as is: any plume-based renderer runs on it |
| uam with banjo-nx's passes | `app/thirdparty/uam` (`PATCHES.md` lists them: PRMT, storage offsets, GVN, uniform branches, interpolation cache) | zlib (uam), MIT (Mesa files), MPL-2.0 (SwitchWakerHD's patches) | as is; built for the Switch and for the PC (the cache tool) |
| shader pipeline tools | `tools/deko3d_shaders.py`, `deko3d_glsl_opt.py`, `deko3d_shader_package.py`, `deko3d_variant_cache.py`, `deko3d_shader_stats.py`, `renut_early_alpha.py` + `early_alpha.py` | banjo-nx's | adapt the binding map to your translator's ABI (section 3) |
| translator hooks | `app/thirdparty/reblue-XenosRecomp` (`BANJO_DEKO3D_SPIRV_DIR`, `BANJO_HLSL_OVERRIDE`, `RENUT_RECOMP` branches) | MIT (reblue) | the SPIR-V dump and the override hook are small patches |
| latency | `sdk/src/core/frame_latency.cpp` (from futari-nx), `app/src/renut_engine/display_sync.cpp`, the late input in `backend/render_thread.cpp` | the user's ports | as is |
| tuning around the renderer | `app/src/renut_engine/gpu/banjo_renut_*.cpp` (keys, reports, checks, record worker) | banjo-nx's | patterns to copy |
| the renderer itself | `app/src/renut_engine/gpu/{d3d,guest,render,backend}` | reNut (masterspike52): **reuse by the user's other ports permitted by its author** (the user, 2026-10-19) | credit reNut and masterspike52 |
| Ryujinx for tests | github.com/ChanseyIsTheBest/ryujinx (private): Ryubing with PRMT, quiet start, memory fixes | MIT (Ryujinx) | runs deko3d NROs to gameplay |

**Licence:** reNut has no licence file, but its author has given the user permission for the user's other ports to
reuse it (the user, 2026-10-19). Take Renut's renderer as banjo-nx carries it, and credit reNut and masterspike52 in the
port's README and third-party notices, as banjo-nx does. Its Direct3D hooks are N&B's addresses: section 2's census
gives yours. UnleashedRecomp's and MarathonRecomp's `gpu/video.cpp` (GPL-3.0, plume-based) remain the reference for
parts a game needs that Banjo's does not (Unleashed's file-loader texture path, Marathon's tiling as one Clear and one
StretchRect); Renut's design comes from Unleashed's, so the concepts map one to one.

## 2. The route (each step keeps a runnable reference)

1. **Census** (`tools/d3d/d3d_census_report.py`): every XDK Direct3D function your game calls, by address. The
   XDK library is statically linked, so the functions exist in every game, at other addresses and sometimes another
   XDK version (Banjo's is 2008: `DrawIndexedVertices`, predicated tiling, four drawing threads; check the
   `D3DDevice` layout of your XDK against the API layer you take: Unleashed's `GuestDevice` is 0x5E00 bytes).
2. **The API layer on plume's Vulkan back end, on the PC first.** Take banjo-nx's `app/src/renut_engine/gpu` (with
   `banjo_renut_*`) and retarget its hooks to your census by address (Banjo did the same when it took Renut's: a tool
   rewrote the hooks, and each hook is a dispatcher to the chosen renderer, so your current renderer stays the
   reference behind a boot key: `banjo_renderer`). Check the device-layout offsets (`d3d::Device`, `kDeviceOffset*`)
   against your XDK. Verify against your current renderer frame by frame (Banjo's method: captures of the same frame
   from both, `docs/deko3d-renderer.md` sections 10-12). On the PC this is plume Vulkan; nothing Switch-specific yet.
3. **The deko3d back end.** Add `plume_deko3d*` to your plume, `CreateDeko3DInterface()` on the Switch, and the
   shader format `RenderShaderFormat::DEKO3D` (GLSL with a `// banjo-dk` bindings line, section 3). Run it in Ryujinx
   (our patched build) before the console: it showed every first-light bug Banjo had, and RenderDoc works on it.
4. **The shader pipeline** (section 3) and **the DKSH cache** from pipeline lists (yours, the emulator's, the
   console's: `deko3d_variant_cache.py --list ... --variants ...`). Without the cache every pipeline compiles in play.
5. **The tuning** (sections 4-6), each behind a key with an A/B config, each measured on the console's scripted route.

## 3. Shaders on deko3d

- **The SPIR-V comes from your translator**, dumped per shader (`BANJO_DEKO3D_SPIRV_DIR`: one `<vs|ps>_<hash>.spv`);
  `deko3d_shaders.py --no-uam` converts it with SPIRV-Cross to GLSL 4.60 for uam. uam takes GLSL, not SPIR-V.
- **Bindings:** Renut's and Unleashed's shaders index textures and samplers bindlessly through push-constant
  descriptor indices. uam has no descriptor indexing: `deko3d_shaders.py` rewrites each bindless access to a fixed
  texture binding and writes the map in the first line (`// banjo-dk ubo=... tex=... texb=binding:descriptor:...`);
  plume's `bindTextures` reads the draw's descriptor indices from the shared constants and binds those images there.
  Float constants are three uniform buffers (vertex 0, pixel 1, shared 2); fetch data is storage binding 15.
- **Specialization constants** pick variants (Renut's bool constants, the alpha test function 903, vertex outputs
  905, early hoist 907): the same GLSL with the constants defined first, one DKSH per variant, keyed by the text's
  FNV-1a 64 (`Deko3DShaderCacheLookup`).
- **`deko3d_glsl_opt.py`** passes (all default on, each with a measured reason in `deko3d-shader-optimisations.md`):
  the alpha test folded per variant, literal registers, predicated selects, tessellation grid, vertex outputs only
  where the pixel shader reads them, constant pixel inputs, early hoist (the fur shader's double fetch), if-merging.
  `--check` compiles every output with the PC uam; a shader that fails keeps its input text.
- **Early alpha** (`renut_early_alpha.py`): Renut's early discard of quads whose pixels all fail the alpha test.
  **Trap (package 35):** any new per-draw value the translator reads in `main()` must be added to the tool's
  `UNIFORM_WIDTHS`, or every shader reading it silently loses its early test (3,604 shaders; the console's fur shells
  ran twice as long; the pictures were identical). Count the copies after every regeneration.
- **The `.incbin` GLSL package** is not dependency-tracked: touch the `.cpp` that includes it after rebuilding it.
- **uam on the Switch** compiles on a thread pinned to the background core at a low priority; a draw waiting for its
  variant lends its priority (`compileCore`, `compilePriority`). With a full cache, 0 compiles in play.

## 4. The render thread (where Banjo's CPU time went)

Console: 25 -> 18 ms a frame.
- **Pushed constants** (`banjo_deko3d_push_constants`): three fixed uniform buffers updated with
  `dkCmdBufPushConstants` (pipelined constant updates) by the 64-byte groups that changed, instead of a 5.7 KB upload
  per draw. A CPU model of what the GPU holds decides; **run its verify key on the emulator before shipping**. Three
  model bugs it found (Banjo's `PushFloatConstants`): the ring path and the push path need separate models; after a
  draw, a group compared only for some of its registers keeps only those registers valid; a shader that indexes reads
  its own literal registers, which hold literals. Ryujinx's constant update staging is 512 words: push in 1 KB pieces.
- **Store cards** for content checks: each game store marks its 4 KB page; a vertex, index or texture copy is hashed
  again only when one of its pages was written (`banjo_renut_store_cards`, `tools/store_cards.py`). Over 99 % of the
  checks proven; the render thread stopped hashing 16 MB a frame.
- **Memos keyed exactly**: texture bindings by texture, pipelines (512 sets x 2 ways), framebuffers, viewports, blend
  skip and output trim inputs, per-shader group summaries. Every memo has a key that contains everything its result
  reads; a missing field is a wrong picture, not a slow one.
- **Ring clean on another core** (`banjo_deko3d_ring_cleaner`): the upload ring's cache clean (DC CVAC) by a worker on
  the background core with a ticket the submit waits for.
- **Do not hold the renderer's lock across render items** (item 19): it hung the emulator at the first loading screen.

## 5. The GPU (no shader changes)

`docs/deko3d-renderer.md` sections 13, 14 and 16; console numbers from Banjo's profile (`renut_gpu_profile` with
per-shader timing):
- **Depth hand-over** (`banjo_deko3d_depth_handover`): a depth resolve the scene then samples (shadow maps) is not
  copied; the scene samples the depth target's own image, and the shadow pass draws into a second image while the
  first is read. Depth resolves 3.2 -> 2.15 ms a frame.
- **Z24S8 depth** for D24S8 targets (`banjo_deko3d_depth24`): the console's own 24-bit depth, 4 bytes a pixel instead
  of ZF32_X24S8's 8.
- **Barriers merged and lean**: consecutive barriers as one, no texture-cache invalidation before a write-only use,
  none between two reads. Gameplay about 28 primitive / 50 fragment / 15 full drains a frame.
- **Depth compression and Zcull** on deko3d (they were trouble only on NVK); Zcull told when a copy writes depth.
- **Resolves as draws** where the copy engine would need Full barriers; colour resolves skipped when unchanged.
- **MSAA off** (the user's choice for Banjo; Renut's 2x was costly) and no resolution changes.

## 6. Input latency

- **The probe** (`rex/frame_latency.h`, from futari-nx): input -> swap -> present every 10 s; with
  `display_sync.cpp`, present -> display and input -> display from the display's vsync event and the presents'
  fence copies (`plume::Deko3DCopyFence`).
- **Late input** (`banjo_late_input`): the first controller read after a Swap waits by the slack the game thread had
  at its Swap (it waits there for the render thread), less 2 ms; ceiling 80 % of the frame interval.
- **Kick submits** (`banjo_deko3d_kick_submit`): the frame goes to the GPU in pieces at the game's kicks.
- **The limiter on the display's refresh** (`banjo_display_sync`, package 35b): `frame_cap` releases frames at a
  refresh plus a measured phase. The Switch compositor shows a new image only at its 60 Hz refresh (no tearing mode).
- Not double-buffered vsync (the user ruled it out for Banjo).

## 7. Traps, in the order Banjo met them

- Two Vulkan users in one executable (the SDK's Mesa entry points and plume's volk; two VMAs): rename in plume
  (`plume_volk_names.h`, `plume_vma_names.h`). Gone with deko3d, but needed while both back ends build.
- uam drops a uniform-buffer component read at run time in some shapes (Banjo's water): found with RenderDoc on Ryujinx.
- Alpha to coverage dithers on Maxwell (non-square pixels): emulate the 360's coverage instead.
- A torn shared-constants header gave single-frame flashes: fill everything a draw reads before the fence.
- Half-pixel offset: the 360 samples at pixel centres differently; the DOF halo came from it.
- Ryujinx lacked PRMT (characters vanished): our build has it; `banjo_deko3d_uam_prmt_off` for an unpatched emulator.
- The DKSH cache file grows on the console; keep it out of zips and give the console's pipeline list back to the cache
  tool for the next package.
- Switch page faults and write-watches corrupt NEON registers (1-4 % of round trips): no detection built on faults;
  the store cards mark pages from the generated code instead.
- Strict vsync in the game's own setter snaps to 20/15/12 fps: `target_refreshRate = 0` and our `frame_cap = "30"`.

## 8. How to ask

The banjo-nx session can answer specifics (paths, keys, why a choice was made). Its docs, newest first:
`docs/deko3d-renderer.md` (sections 10-18), `docs/deko3d-shader-optimisations.md`,
`docs/renut-renderer-port.md`. Source: github.com/ChanseyIsTheBest/renut-nx.
