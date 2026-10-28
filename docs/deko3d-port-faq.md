# deko3d port FAQ for the other ports (2026-10-19 night)

> Status 2026-10-28: this guide is older than the current renderer. Start at `docs/porting-start-here.md` (the reading order, what is current, the method rules) and see `docs/porting-guide-renderer.md` section 6b for the items measured slower on the console. Superseded here: "no resolution changes" (Banjo now has internal and dynamic resolution); MSAA is off.

Answers to the questions Futari, SA2, Peggle 2 and SASRT asked while starting the route of
`docs/deko3d-native-renderer-kit.md`. Paths are banjo-nx's unless marked.

## Getting the renderer in

- **Import / hook rewrite:** `python tools/renut_gpu_import.py --renut "D:/Banjo kazooie port/reNut"` (`--dry-run`,
  `--save-patch` after editing imported files, `--no-patch`). It copies reNut's `src/renut_engine/gpu`, rewrites each
  `REX_HOOK(Name, ...)` to `sub_<address>` from reNut's `config/renut_gpu_funcs.toml` (address -> name, N&B's
  addresses), and makes each hook a dispatcher: `BANJO_RENUT_HOOK(sub_X, fn)` / `BANJO_RENUT_HOOK_RAW(sub_X)` run
  Renut's code only when Renut's renderer is chosen and the game's original otherwise; where banjo-nx's own renderer
  hooks the same address, `BANJO_RENUT_HOOK_SHARED` makes Renut's function `renut_native_sub_X`, called from banjo-nx's
  hook (list: `app/src/me/banjo_renut_shared.txt`, `banjo_shared_hooks.h`). Then `tools/renut_patches/banjo.patch`.
  For your game: a copy of reNut's `config` with your addresses for the same names; adjust the tool's ROOT/DEST/KEEP.
- **Boot choice:** `banjo_renderer` ("native" / "renut"), read once: `app/src/renut_engine/gpu/banjo_renut_select.{h,cpp}`
  (`banjo::renut_gpu::Active()`, a byte load after the first call). The dispatch macros are in that header.
- **N&B-specific files to cut or rewrite:** `guest/widescreen.cpp` (N&B engine hooks for the aspect ratio),
  `guest/photo_capture.cpp` (N&B's photo feature), `render/graphics_options.cpp` (N&B's options), the game-function
  imports the tool renames (XMemFree and friends), and `banjo_renut_*` keys you do not want.

## What depends on the Direct3D layout (re-derive per XDK)

- `d3d/d3d.h`: the `Device` static_asserts (m_pRing 0x30, m_SetRenderStateCall 0x40, m_Constants 0x480, the packets
  0x2880-0x2A50, m_MaxAnisotropy, m_ZFilter) and every `kDeviceOffset*` (vertex declaration, index buffer, render
  targets, depth, streams, strides, textures, bound shaders, tessellation, predication, command buffer, the
  Begin*Vertices ring end, swap count, fence tags, swap flags), `kDeviceSnapshotSize`, `kDeviceAllocSize`.
  N&B (2008 XDK) values; SA2 / Peggle / SASRT / Futari report the block after about 0x2E00 shifted.
- `guest/command_buffers.cpp`: the chunk table over the snapshot (ChunkOffset / ChunkSize) and the m_Pending dirty
  masks (only if the game records command buffers).
- `d3d/d3d_native.h`, `d3d/d3d_native_state.cpp`: the device work Direct3D did (dirty masks, fence bookkeeping) that
  the hooks replay so the game's inlined setters stay consistent.
- Resource headers read in `d3d/hooks/resource.cpp`, `render/textures.cpp`, `render/surfaces.cpp` (D3DResource /
  D3DBaseTexture / surface layouts, Common, the fetch-constant words in the header, Size): check against your XDK.
  The Xenos fetch-constant formats themselves are hardware: the same for every XDK.
- Fixed guest addresses in hooks (grep the hooks for `0x82` constants and `GuestPtr`).

## Draws

- `D3DDevice_BeginVertices` (d3d/hooks/draw.cpp): the hook writes the inline end into the device
  (`kDeviceOffsetVerticesRingEnd`, N&B 0x3484; Futari reports 13652), hands the game a scratch buffer, captures the
  draw's state at once and copies the vertices at the device's next hook (deferred). It covers BeginVertices as the
  main path: N&B uses it for its 2D and particles. DrawVertices / DrawIndexedVertices / *UP / tessellated have their
  own hooks.
- What must be the renderer's for first light: CreateDevice, the draws, Clear, Resolve, Swap / Present, the ring
  protocol (BeginRingBig / BeginRingManual / AllocateInlineData / RingBufferDeviceAllocate, KickOff / KickOffSegment,
  BlockOnFence / IsFencePending / D3DDevice_BlockOnFence, the fence tags), and the shader objects (CreateVertexShader /
  CreatePixelShader / XGRegister* / D3DVertexShader_Bind: objects to containers). Textures, streams and state are read
  from the device at each draw, so the setters can stay the game's own if the layout matches. No proven smaller set:
  banjo-nx took all of Renut's hooks.

## Shaders

- Renut looks shaders up by the XXH3 of the guest container in a cache built offline by XenosRecomp (RENUT_RECOMP)
  from a dump of the game's containers: reNut `tools/dump_shaders.py` -> `out/renut_shader_dump` (the xex and its
  bundles; shaders the game builds itself are captured at run time by the PC build for the next translation:
  `app/out/pc-me/shader_capture`). See `docs/renut-renderer-port.md` "Shaders".
- PC: the translator writes `shader_cache.cpp` (build the PC tree first after any translator change).
  Switch: `tools/renut_switch_shader_cache.py` (the `.incbin` form).
- deko3d: XenosRecomp with `BANJO_DEKO3D_SPIRV_DIR` also writes SPIR-V per shader; `tools/deko3d_shaders.py --spirv
  <dir> --out <dir> --no-uam` (GLSL), `tools/deko3d_glsl_opt.py --in glsl --out glsl_opt --check`, then
  `tools/deko3d_shader_package.py` (deko3d_glsl.bin, embedded with `.incbin`: touch its .cpp after rebuilding).
  Early alpha: `tools/renut_early_alpha.py` on the dumped HLSL (`BANJO_HLSL_DUMP_ONLY`, then `BANJO_HLSL_OVERRIDE`);
  add every per-draw value main() reads to its `UNIFORM_WIDTHS`.
- First light without the DKSH cache works: plume deko3d compiles each variant with uam at run time on its compile
  thread (slow at first, fine in Ryujinx). The DKSH cache (`tools/deko3d_variant_cache.py --list <pipeline_list.bin>`)
  is for speed only. With 55 shaders (Futari) all of this takes minutes.

## Ryujinx (several instances, one data folder per port)

- `D:\New folder\Ryubing-src\ryubing\build\Ryujinx.exe` (github ChanseyIsTheBest/ryujinx, release banjo-nx-2026-10-19b),
  `DOTNET_ROOT=D:\New folder\dotnet10`. Starts minimised and muted in our fork (`RYUBING_INTERACTIVE=1` for a normal
  window). Firmware 16.1.0 and the user's keys are installed in each data folder.
- Each port has its own data folder: `Ryujinx.exe -r "D:\New folder\ryu-data\<port>" "<nro>"` (config, SD card,
  logs and shader cache inside it), so several ports can run at once. Put the game in your SD folder as a **junction**
  to your one extracted copy (`mklink /J`), never a copy; never copy a folder that holds a junction recursively (the
  copy follows it).
- Set `RYUJINX_HOMEBREW_SHADER_CACHE_ID` to your own 16-hex id (Banjo: 0500000042414E4A).
- Banjo's wrapper (paths are Banjo's; read, do not run as is):
  `C:\Users\Potatoe\AppData\Local\Temp\claude\D--Banjo-kazooie-port\5cd24572-4f74-45ca-8b31-b848317e5219\scratchpad\emu_run.sh`
  (PowerShell Start-Process minimised with `-PassThru`, Wait-Process with a timeout, Stop-Process by that PID).
- **Stop only your own instance, by PID (never by name: the other ports' runs would stop too). Never leave one
  running. Keep runs under 10 minutes.**
- Known: Ryujinx answers a second `OpenDisplay` of "Default" like the console now (our fork); a game thread waiting on
  the display's vsync event stalled Banjo in the emulator (not explained; Banjo's display_sync is off by default).
- **A stall watchdog that pauses threads can freeze the whole game in Ryujinx (Banjo, 2026-10-29):** pausing a thread
  with `svcSetThreadActivity` (to read its registers, as the watchdog from UnleashedRecomp-NX does) makes Ryujinx loop
  on its global kernel lock until that thread leaves its core; on a busy PC the paused thread can lose the lock to the
  loop indefinitely. The watchdog hangs with the thread still paused, and every guest thread soon waits on it (2 of 3
  runs froze around the intro movies; 0 of 2 with the watchdog off). Turn the watchdog off for emulator runs
  (`banjo_stall_watch_seconds = 0` in Banjo); keep it for the console.

## Traps other ports hit (reported by the ports)

- **Post-codegen direct calls (SA2):** ports using masseffect-nx's `tools/direct_calls.py` turn `sub_X` calls into
  `__imp__sub_X` for every function the tool does not see hooked. Renut's new hooks (CreateDevice, XGRegister*, ...)
  were bypassed and every draw was dropped as "vertex shader not one the renderer knows". Re-run post-codegen after
  the import, and have the tool read only the `BANJO_RENUT_HOOK*(sub_X)` macros under `renut_engine` (the tree names
  N&B addresses in constants and in files that are not built).
- **Frame pacing (SA2):** where game speed follows frame rate, the console paced the game by the vblank (the GPU
  waits for the flip after each Swap). With no ring and no vblank interrupt the game runs uncapped (SA2: 300-1,900
  fps). Pace the Swap hook at the game's own `D3DPRESENT_PARAMETERS::PresentationInterval` (SA2 keeps the copy at
  device+14000) on a fixed 60 Hz grid, with the game's relaxed-vsync rule if it has one. Banjo's limiter:
  `app/src/renut_engine/frameHooks.cpp` (with `target_refreshRate = 0` and `frame_cap`, docs/deko3d-renderer.md).
- **Hybrid route (SA2):** keeping the game's own Direct3D for state setters, resources, Present and the device work
  of Clear / Resolve, with only creation, ring and fences, draws, Swap and the shader / declaration side tables
  Renut's, reached first light. A smaller set than Banjo's is possible when the device layout is re-derived.
- **Early alpha with named constants (Futari):** `tools/renut_early_alpha.py` took none of Futari's 24 pixel shaders
  ("unsupported: variable g_PSC_F1_ColorRGB" / "smpColorMap_Texture2DDescriptorIndex"). Containers that carry constant
  and sampler names get them from the translator as `#define g_PSC_F1_ColorRGB g_C(2)` and
  `smpColorMap_*DescriptorIndex` (Banjo's are the unnamed `s0_...` forms). Futari's fix in its copy: accept
  `\w+_(Texture2D|Texture3D|TextureCube|Sampler)DescriptorIndex` and `\w+_InvSize` as uniform names, and replace each
  `#define NAME g_C(N)` name by `g_C(N)` inside main() before the tool reads it (13 of 24 shaders taken, self-checked).
- **Vblank callbacks (Futari):** Renut has no GPU interrupt, and the SDK runs no vsync thread without its graphics
  system. A game that counts vblanks through `D3DDevice_SetVerticalBlankCallback` gets them from D3D's vblank handler,
  whose interrupt dispatcher first reads a GPU register (0x7FC86544, unmapped without a graphics system). Futari runs a
  guest XHostThread that calls D3D's vblank handler itself (a 60 Hz timer, per Swap in lockstep, or the display's
  vsync on the Switch).
- **Store cards (Futari):** a port without Banjo's `store_cards.py` codegen must define `g_banjo_store_cards = false`,
  or the cards are never zeroed.
- **N&B guest addresses beyond the device layout (Futari):** the renderer still names N&B globals: the state tables
  0x82E506F0 / 0x82E50BD0, the format table 0x820052F8, the caps 0x82183B00, the HiZ owner 0x82FA36C8, XPhysicalAlloc's
  no-large-pages flag 0x82F39E34 (AllocPhysical reads through that pointer without a null check), the full viewport
  0x820018DC and the export switches 0x82FAD204 / 0x82FAD208. Find your game's, or cut what reads them.
- **Resolve wrapper (Peggle 2, XDK 21256):** the public Resolve is a wrapper around an internal resolve with two more
  arguments; hooking the inner one shifts every argument (Swap then has no front-buffer texture). Hook the public one.
- **SetFVF declarations (Peggle 2):** SetFVF's declaration embedded in the device (N&B +0x2FA0) has no record in the
  side tables: those draws are dropped until it gets one.
- **Device block pointer (Peggle 2):** Peggle's Direct3D keeps a 32-byte block pointer at device+11028
  (InitializeHardwareDevice) that unhooked fence / interrupt paths dereference. Device offsets for XDK 21256 (Peggle 2
  and SA2): peggle2-nx/out/renut_port/device_offsets.md.
- **The game's gamma ramp (Peggle 2; Banjo too):** Renut's present never applies the ramp the game loads
  (present_ps.hlsl `pow(c, param0)` with param0 = 1). Peggle's ramp is about x^1.17; N&B's is about the same (10-bit
  entries [64] = 192, [128] = 459, [255] = 1019 where the identity is 257 / 514 / 1023; banjo-nx's native renderer
  applies it, me_native_system.cpp NoteGammaRampWrite). Without it the image is brighter in the midtones than the
  console's. Banjo's Renut builds do not apply it yet (the user's call: it changes the look).
- **Vertex colours R/B swapped in Ryujinx only (Peggle 2; Banjo too):** Ryujinx ignored VERTEX_ATTRIB_FORMAT bit 31
  (deko3d's `DkVtxAttribState.isBgra`, which plume sets for B8G8R8A8 attributes: every D3DCOLOR vertex colour). The
  console honours it. Fixed in the fork 2026-10-20 (StateUpdater.UpdateVertexAttribState); before that, judge vertex
  colours in emulator captures with care.
- **Texture hashing without store cards (Peggle 2):** hashing every linear texture each frame (4096x4096 atlases the game
  never unlocks through the hooked path) was 71 % of Peggle's render thread; backing off 2..32 frames on an unchanged
  texture took it from 8 to 0.8 ms a frame on the PC.
- **Automatic front buffer (Futari):** a game that leaves `DisableAutoFrontBuffer` = 0 and passes
  `device->AutoFrontBuffer` to Swap gets black presents: Renut's InitializePresentationParameters only logs "an
  automatic front buffer is not supported". Create it as the XDK does: `CreateTexture(width, height, 1, 1, 0,
  front_format, 0, D3DRTYPE_TEXTURE)` into AutoFrontBuffer.
- **Gamma, Futari's measurement:** its picture was 8-13 levels a channel brighter than the emulated GPU's (which applies
  the display registers' ramp); Futari fits the device's corrected ramp to one exponent at each Swap and passes it as
  the present's param0 (`futari_renut_display_gamma`). A fitted exponent is close, not exact; a 256-entry lookup of the
  ramp in the present pass would be exact.
- **Alpha-test specialisation and mip selection in Ryujinx (SA2, not checked on a console):** with
  `banjo_deko3d_alpha_spec` on, SA2's textures sampled as at their smallest mip in Ryujinx (every surface one flat
  colour); the generic variants (903 = 0) were right, uam's TGSI differs only by the missing KILL_IF. Banjo's console
  has run the specialised variants since package 33 with the right picture (and Banjo's emulator captures were right),
  so the cause is likely in the emulator's handling of a shader without KIL, or SA2-specific; SA2 ships it off for now.
  Bisect method: a key that reads the GLSL package from the SD (sa2-nx `sa2_deko3d_glsl_package_file`), no relink.
- **Ryujinx's shader cache after a fork update:** the homebrew shader cache is invalidated when the GPU dll changes;
  the first run after an update is slow (SA2: 1-11 fps for a minute).
- **Bindless handles computed in a shader read zero in Ryujinx (SA2; Banjo hit it too):** Ryujinx follows a bindless
  texture handle only back to a constant-buffer load; a slot computed from a value (`uint(param1) - 1` from a float push
  constant) read zero (a black picture). Pass the slot as raw bits (`asuint(param1)`) and use it as loaded. Both SA2's
  and Banjo's gamma-ramp lookups (a 256 x 1 RGBA32F texture in a bindless slot, present_ps) work that way.
- **Gamma ramp, exact (SA2, Banjo):** the device's corrected ramp (+0x3A9C in N&B's device) uploaded as a 256 x 1
  RGBA32F texture and looked up per 8-bit channel value (/1023) in present_ps: SA2's lockstep title-screen difference
  against its native renderer went from 12.8 to 0.066 levels. Banjo: `banjo_renut_game_gamma` (present.cpp).
- **C++ exceptions crash Ryujinx (Futari):** devkitA64 GCC 16.1's libgcc unwinder reads GCSPR_EL0 (`mrs x1, gcspr_el0`)
  behind a `chkfeat` test that skips it on hardware; ARMeilleure translates the skipped block anyway and threw
  "Unknown MRS 0xD53B2521", even for an exception that would be caught. See the fork's fix (MRS GCSPR_EL0 reads 0).
- **Docked swapchain at 720p (Peggle 2):** plume's `Deko3DSwapChain::resize` takes `nwindowGetDimensions` of the default
  NWindow, which reads 1280x720 docked too, so a docked frame is presented at 720p and scaled by the display. Peggle
  added `plume::g_deko3dWindowSize` (an app callback: 1920x1080 docked) and `nwindowSetDimensions` after `destroy()`
  (Ryujinx docked: "swapchain: 3 images of 1920x1080"). Banjo keeps 720p by choice (no resolution changes).
- **Renut's deko3d 1x lock (Peggle 2):** only shaders that keep the resolution-scale code (`resolution_scale`; Banjo's
  scale1 / fragscale passes) need it. Peggle's shaders have none, and plume masks bound texture words to 20 bits, so
  Peggle renders 1080p docked on deko3d (per game, by key).
- **Video decoders against deferred texture fills (Peggle 2):** a Bink-style decoder writing the planes the frame just
  submitted draws races Renut's deferred fills (kMaxFramesAhead = 1): spots in videos. Peggle's video-update hook
  calls `render_thread::Drain()` first (its native renderer had the same bug with its ring).
- **A config with only log_level writes no log (Peggle 2):** when the built-in settings set `log_to_file = false`, a
  test config needs `log_to_file = true` too.
- **Texture swizzle from the header, not the device (Banjo, the colour flash):** Renut's bind reads the texture header
  in guest memory at render time; a header that is a temporary (the game builds them on its stack for bloom levels) may
  have been rewritten by then with the same data and another swizzle. Banjo's whole-frame yellow flash was a bloom
  level read through 1,1,0,0 (RenderDoc capture of Ryujinx; every other frame reads it as RGBA). The device's fetch
  constant (SetTexture's copy, what the GPU reads) has the right one: `banjo_renut_device_swizzle` (textures.cpp
  BindTextureLocked). Final form: SetTexture copies the header's dwords 0, 3 and 5 unchanged (signs, pitch, tiling;
  swizzle, number format, exponent adjust; dimension, packed mips, mip address) and changes only dword 4's mip levels
  (the sampler's MinMipLevel / MaxMipLevel), so a header of the same data that differs in dword 0, 3 or 5 is stale and
  the whole device fetch constant is used. Banjo's counts: dword 4 differs in tens of thousands of binds (by design),
  dword 3 in a few dozen (stale), dword 5 once, dword 0 never. Not rare:
  Banjo's emulator run logged it in every frame of the jigsaw transition into the main menu (a stack header at
  0x701CF9F8 with swizzle X,Y,1,1 where the device's was X,X,X,1).

- **D3D 2.0.21119 traps (SASRT, SA2's D3D version):** (1) `D3DDevice_BeginIndexedVertices` has no Renut hook (SASRT
  draws its post chain and scene composite through it; hook it like BeginVertices: scratch handed out, one deferred
  copy, vertices then indices); (2) D3D_SetSurfaceClip writes the window offset / scissor (regs 0x2080-0x2082)
  straight into the command buffer with no device shadow, so `dev.m_WindowPacket` goes stale (SASRT shadows the type-0
  packet 0x00022080 after the original); (3) reverse Z through the viewport (MinZ 1, MaxZ 0) works on Vulkan / deko3d
  but D3D12 rejects it (SASRT: host viewport 0-1 plus a vertex-shader epilogue z = z*zs + w*zo on the PC only); (4)
  command buffers recorded on a second worker device with its own MRT surface headers and spliced into composed
  buffers: replay at the ring submit keeps the order.
- **Caches that forget (Peggle 2, Banjo 2026-10-20):** the DKSH cache append and the pipeline list were flushed but never
  synced; fflush only reaches the file system service, and HOME-close (or an emulator stop) lost the session's records,
  so the same shaders compiled at every start. Fix: fsync after each cache record; the list as a FILE* synced at most
  once a second plus `SyncPipelineListLocked` from the present. Also normalise loaded list keys to the current
  configuration (`DecodeRecord`), or stale records (Banjo: blend skip from before package 30) get built ahead with
  constants no draw uses. plume logs "uam: run-time variant (stage S, N bytes): constants" for each run-time compile.
- **Kick submits and the latency trim need draws (Futari, Peggle 2):** a list ends at a kick only once it holds 200
  draws (`kKickSubmitDraws`); a game drawing 20-35 a frame (Peggle) never splits its frame, so both are inert there.
- **Reading the "[renut] deko3d:" line (Peggle 2):** its draws, barriers and texture-upload counts are cumulative since
  start, not per 10 s report: divide differences of two lines by the frames between them.
- **std::thread::detach aborts on the Switch (Peggle 2; Banjo's package 24 hit it too):** with devkitA64 / libnx 4.12
  pthreads, `std::thread(f).detach()` throws -> terminate -> svcBreak (Ryujinx aborts with std::thread::detach on the
  stack). Keep long-lived threads joinable and never destroyed (`static std::thread *t = new std::thread(f);`).
- **In-run A/B of a specialization-constant key (Banjo, 2026-10-20):** a key that adds a constant at pipeline creation
  cannot simply flip in the run (the pipelines are cached by key). Banjo's targets 15 / 16 set a bit of
  `PipelineKey.reserved` in the A/B's off half (render/draw.cpp, the key builder), and pipeline.cpp leaves the constant
  out for keys with it: both halves' pipelines are keys of their own and stay built, and the "[renut A/B]" line gives
  both GPU times from the same scenes. Keep both variants in the DKSH cache (deko3d_variant_cache.py adds 910 / 911 =
  1 variants) or the first switch compiles.

Credit reNut and masterspike52 (the user has the author's permission for these ports).

## The display's vsync event: the console refuses a second open; Ryujinx's event followed presents (2026-10-20)

- On the console `viOpenDefaultDisplay` fails when libnx's default window (which the deko3d swapchain uses through
  `nwindowGetDefault`) already holds the "Default" display open: banjo-nx package 36i's logs ("cannot open the
  display"), so a vsync / vblank thread built that way never runs there (the emulator answered the second open, which
  hid it). banjo-nx's fix (renut_engine/display_sync.cpp): define `__nx_win_init`, `__nx_win_exit` and
  `nwindowGetDefault` yourself (all three, so libnx's default_window.o stays out of the link; libnx's init.o calls the
  first two through weak references), with libnx's calls in libnx's order: `viInitialize(ViServiceType_Default)`,
  `viOpenDisplay("Default")`, `viCreateLayer`, `viSetLayerScalingMode(FitToLayer)`, `nwindowCreateFromLayer`,
  `nwindowSetDimensions(1280, 720)` (on failure close what was opened and `diagAbortWithResult(0x2559)`), and keep the
  `ViDisplay` for `viGetDisplayVsyncEvent`.
- Ryujinx (the shared fork, `SurfaceFlinger.HandleComposition`): with swap interval 0 (game vsync off) it signalled the
  vsync event after every composition, i.e. at the game's own presents; a frame limiter paced by that event paced
  itself by its presents and stalled. The fork now signals it on a 60 Hz grid whatever the swap interval, as the
  console's display does (BANJO-NX.md in the fork). Every port on `D:\New folder\Ryubing-src\ryubing\build` gets it.

## A Switch build without the Vulkan driver (2026-10-20, package 36m)

Once deko3d is the only renderer you ship, Mesa's static NVK (libvulkan.a with expat and libdrm_nouveau) is dead
weight: 14.4 MB of banjo-nx's NRO (88.9 -> 74.4 MB). What it took:

- The SDK (`sdk/cmake/rexglue_switch.cmake`): `option(REXGLUE_SWITCH_NVK ... OFF)`. OFF makes `rex::nvk` an interface
  library with only the other libraries NVK's line had (`zstd z nx stdc++ m`, `-pthread`) and adds
  `sdk/src/ui/switch_vulkan_stubs.c` to the startup objects. The NVK SDK path is then not needed at all.
- plume (`thirdparty/plume/CMakeLists.txt`) links libvulkan.a itself, with `-u vk_icdGetInstanceProcAddr`: a second
  switch, `PLUME_SWITCH_NVK_LINK` (banjo-nx sets it from `REXGLUE_SWITCH_NVK`). Miss it and the driver is still linked.
- What is left undefined is small, because everything else goes through `GetInstanceProcAddr` / volk:
  `vkGetInstanceProcAddr` and `vkDestroyInstance` (the SDK's `VulkanInstance::Create`), `vk_icdGetInstanceProcAddr` and
  `vk_icdNegotiateLoaderICDInterfaceVersion` (plume's Switch `VulkanInterface`), and `posix_memalign`, which Mesa's
  `rust_switch_stubs.c` defined inside libvulkan.a and FFmpeg's `av_malloc` uses (devkitA64's newlib has only
  `memalign`). The stubs answer NULL / `VK_ERROR_INCOMPATIBLE_DRIVER`; `posix_memalign` is a real one on `memalign`.
- plume's Switch `VulkanInterface` constructor called `vkEnumerateInstanceExtensionProperties` right after
  `volkInitializeCustom` without checking it: with no driver that is a NULL call. It now returns with no instance
  (`isValid()` false) when `vkCreateInstance` or that function is NULL.
- Configs that ask for a Vulkan renderer (banjo-nx: the native renderer, `renut_gpu_backend = "vulkan"`) cannot work:
  with `BANJO_SWITCH_NO_VULKAN` the boot choice is forced to Renut on deko3d with a warning in the log, and the
  packages leave those configs out.
- Check with `nm` on the ELF: no `nvk_`, `nak_`, `vk_common_`, `nouveau_` or `XML_` symbols.

## Taking sa2-nx's and banjo-nx's Renut CPU work into another port (SASRT's notes, 2026-10-20)

- **Store cards:** banjo-nx's `StoreCards()` (render/draw.cpp) is the guest window's base: banjo-nx's codegen keeps its
  card table in the window's first MB, and `CompleteMemExportsLocked` calls `rex::memory::MarkStoreCardsAt` for every
  export it copies back, unconditionally. In a port without that card table the call writes into the game's own memory
  at the window's base. Define the port's store-cards flag false (`g_banjo_store_cards`; sa2-nx's compat layer drops
  it) and remove or guard that call.
- **Snapshot deltas** (queued.cpp): a draw's captured device is a delta now, so per-draw captures that read the
  device's state (SASRT: its surface-header capture) must read the live device at capture time, not `capture->state`.
- **Symbols the newer files expect:** `RexSetGuestThreadCoreLayout` (sa2-nx's core layout; a stub is enough with
  `banjo_renut_core_layout` off) and sa2-nx's `rex/input/read_hook.h`. Late input never runs unless the SDK's
  controller read calls the delay: sa2-nx through that hook, banjo-nx's `sdk/src/input/input_system.cpp` directly
  (`rex::latency::BeforeInputRead()` then `NoteInputRead()`).
- Measured defaults on N&B, to re-measure on your game: chain reuse, replay hints and the inline line compare off;
  core layout off (hung the first Swap in Ryujinx); the deltas' unread device range is per game (N&B 10880-11775).
