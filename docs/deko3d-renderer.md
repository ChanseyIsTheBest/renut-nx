# deko3d renderer (from 2026-10-17)

The user's order (2026-10-17): a separate deko3d renderer, made by converting **Renut's renderer** (the one `renut.toml`
boots: Direct3D level, no ring, no PM4) to deko3d, with its Vulkan source kept as it is. The aim is lower CPU and GPU
use on the console. Work continues without stopping between steps.

## 1. What it is

Renut's renderer talks to the GPU through plume (`plume::RenderInterface`, Vulkan on the Switch through NVK). The deko3d
renderer is **a plume back end on deko3d** (`thirdparty/plume/plume_deko3d.{h,cpp}`), chosen at boot with
`renut_gpu_backend = "deko3d"`. Everything above plume stays Renut's renderer: the Direct3D hooks, the draw state, the
texture and surface caches, the record worker, the GPU profile. The Vulkan back end, NVK and the SDK's renderer are
untouched and still selectable. The deko3d paths added to the renderer itself are behind `IsDeko3D()`.

| `banjo_renderer` | `renut_gpu_backend` | What draws |
|---|---|---|
| `native` | (none) | banjo-nx's PM4/EDRAM renderer on NVK |
| `renut` | `vulkan` | Renut's renderer on plume Vulkan / NVK (package 28b) |
| `renut` | `deko3d` | Renut's renderer on plume deko3d (this page) |

Why deko3d should cost less (console figures of logs 28, CPU-limited run): the render thread spent 22 % of its time in
NVK and 2 % in plume's Vulkan layer. deko3d writes Maxwell methods straight into command memory: no pipeline objects to
look up, no descriptor-set updates through the driver (a descriptor is 32 bytes the renderer writes itself), no
Vulkan validation of barriers or layouts, and submission is a GPFIFO entry. SwitchWakerHD measured about 7 us per draw on
deko3d against more for Mesa's GL driver on the same game. On the GPU side, the shaders are compiled by uam
(Mesa nouveau's nv50_ir with fincs's Maxwell work) instead of NAK; which code is faster per shader is measured, not
assumed.

## 2. The references, and what is taken from each

| Project | What it is | Taken |
|---|---|---|
| SwitchWakerHD (`github.com/centollOS/SwitchWakerHD`, MPL-2.0) | Wind Waker HD (Cemu) with a deko3d renderer | uam 1.1.0 with its patches (`thirdparty/uam`, PATCHES.md: real mutexes, in-memory DKSH, no exit/abort, deterministic scheduling, **no dual issue** (fixed 4x8-pixel speckles), stall before a new block); the list of deko3d 0.5.0 bugs (below); the memory layout (per-frame stream and command rings with a fence each, a first-fit image heap in 64 MB chunks, a bump-allocated code block); zcull off |
| Tenkaichi-3-NX (`gitlab.com/alexyt2021es/Tenkaichi-3-NX`, MIT) | Budokai Tenkaichi 3 (PS2 GS) with a deko3d backend | `dkCmdBufReplayCmds`: raw Maxwell methods through deko3d's public API, which is how the 0.5.0 bugs are worked around without patching deko3d (its blend-state words are fincs's zlib code, `deko_blend_state.h`) |
| dolphin-nx (`github.com/NaGaa95/dolphin-nx`) | Dolphin on the Switch | Not deko3d: Vulkan on a localized NVK, the same driver as ours. Nothing taken. |

Their code is read, not run. Licences: uam is zlib (fincs) with Mesa files under MIT; SwitchWakerHD's changes to it are
MPL-2.0, which is GPL-compatible (MPL section 3.3); deko3d is zlib; SPIRV-Cross (a PC build tool only) is Apache-2.0.

## 3. deko3d 0.5.0 (the devkitPro library) and its bugs

The NRO links devkitPro's `libdeko3d.a` 0.5.0 unchanged. Its known bugs, and what the back end does:

| Bug (found by) | Effect | Work-around |
|---|---|---|
| `dkCmdBufBindBlendStates` writes the colour destination factor into `FuncAlphaDst` (SwitchWakerHD, Tenkaichi) | separate alpha blend factors blend alpha with the colour's factor | after each bind, the six `IndependentBlend` words of each target are written again with `dkCmdBufReplayCmds` (method `0x781 + 8 * target`, subchannel 0) |
| `dkImageLayoutInitialize` picks a tile height its level 0 shrinks, but the engines use the unshrunk one (SwitchWakerHD) | 6..8-row images (and BC images of 24..32 rows) written with one layout, read with another | `DkImageFlags_CustomTileSize` with the shrunk tile when the two differ (SwitchWakerHD's `image_tile_size_fix`) |
| The framebuffer sRGB register (Maxwell `0x56E`) is never written (SwitchWakerHD) | sRGB render targets are not encoded | written with `dkCmdBufReplayCmds` when a target set is bound |
| Depth images sample as (D, D, D, 1) where D3D returns (D, 0, 0, 1) for some formats (SwitchWakerHD) | wrong channels from depth textures | the image view's swizzle |
| Zcull on by default (SwitchWakerHD) | lost 4x8 tiles with depth-biased decals | the queue is made with `DkQueueFlags_DisableZcull` |
| `dkCmdBufSetViewports` derives the clip rectangle from a negative height | garbage clip for flipped viewports | a positive-height call, then `ScaleY`/`TranslateY` (methods `0x281`/`0x284 + 8 * i`) written raw |

## 4. Mapping plume to deko3d

| plume | deko3d |
|---|---|
| `RenderDevice` | `DkDevice` (`DkDeviceFlags_OriginUpperLeft \| DkDeviceFlags_DepthZeroToOne`), one graphics `DkQueue` (zcull off) |
| `RenderBuffer` | a suballocation of a CPU-visible `DkMemBlock` (`CpuUncached \| GpuCached`; `CpuCached` for readback); `getDeviceAddress()` is the `DkGpuAddr` |
| `RenderTexture` | `DkImage` in the image heap (`GpuCached \| Image` blocks, 64 MB chunks, first-fit); layout from `DkImageLayoutMaker` with the tile fix |
| `RenderTextureView` | `DkImageView` + a 32-byte `DkImageDescriptor` written where a descriptor set puts it |
| `RenderSampler` | `DkSampler` + `DkSamplerDescriptor` |
| `RenderDescriptorSet` | a range of the one image-descriptor block and the one sampler-descriptor block; `setTexture` / `setSampler` write the descriptor (CPU, uncached memory) and `setGraphicsDescriptorSet` binds the block (`dkCmdBufBindImageDescriptorSet` / `dkCmdBufBindSamplerDescriptorSet`) |
| constant buffers (dynamic offsets) | `dkCmdBufBindUniformBuffer(s)` at address + offset |
| push constants | a small UBO per stage range, updated with `dkCmdBufPushConstants` (pipelined like Vulkan's) |
| storage buffers | `dkCmdBufBindStorageBuffer(s)` |
| `RenderPipeline` (graphics) | the stage `DkShader`s (DKSH in a code block) + rasterizer, colour, colour-write, blend, depth-stencil and vertex attribute/buffer states, bound with deko3d's bind calls when they differ from what is bound |
| `RenderPipelineLayout` | how plume set/binding numbers become deko3d binding slots (the same numbering the shader converter uses) |
| `RenderFramebuffer` | `DkImageView`s for `dkCmdBufBindRenderTargets` |
| `RenderCommandList` | a `DkCmdBuf` fed from a command-memory ring; `end()` gives a `DkCmdList` |
| barriers | `dkCmdBufBarrier` (`DkBarrier_Fragments` or `Full`) with the cache invalidations; image layouts do not exist |
| clears | `dkCmdBufClearColor` / `dkCmdBufClearDepthStencil` (rectangles through the scissor) |
| copies, resolves | `dkCmdBufCopyBuffer`, `dkCmdBufCopyImage`, `dkCmdBufBlitImage`, `dkCmdBufCopyBufferToImage`, `dkCmdBufResolveImage` |
| `RenderCommandFence` / `RenderCommandSemaphore` | `DkFence` (semaphores: queue order already serialises one queue) |
| `RenderSwapChain` | `DkSwapchain` on libnx's default window, images in the image heap |
| `RenderQueryPool` (timestamps) | `dkCmdBufReportCounter(DkCounter_TimestampPipelineTop)` into a CPU-uncached block; `dkTimestampToNs` |

## 5. Shaders

uam takes GLSL only, with explicit bindings (16 UBOs, 16 SSBOs, 32 combined samplers, 8 images per stage), no push
constants, no specialization constants and no `GL_EXT_buffer_reference`. It does enable `GL_ARB_bindless_texture`,
`GL_ARB_gpu_shader_int64` and SSBOs. The chain:

1. **HLSL** from Renut's XenosRecomp (game shaders) or `shaders/host/*.hlsl`, compiled by DXC to SPIR-V **with
   `-DBANJO_DEKO3D` and without `-fvk-invert-y`**: D3D's clip space (y up) is what deko3d 0.5.0's upper-left origin
   maps to the top row, so no flip is needed anywhere.
2. **SPIRV-Cross** (`out/deko3d_tools/spirv-cross-build`, built from UnleashedRecomp-NX's copy) to Vulkan GLSL 4.60.
3. **`tools/deko3d_glsl.py`** rewrites that GLSL for uam:
   - descriptor-heap texture fetches `sampler2D(g_Texture2DDescriptorHeap[t], g_SamplerDescriptorHeap[s])` become
     **bound textures** where `t` and `s` are SharedConstants words (3,935 of the shaders): binding k of the stage, its
     handle made by the back end before each draw from the two words (`texb=k:textureWord:samplerWord` in the
     `banjo-dk` meta line; `Deko3DCommandList::bindTextures` reads them where the CPU wrote the shared constants and
     calls `dkCmdBufBindTextures` when they changed). The other 17 keep bindless handles
     `sampler2D(uvec2((t & 0xFFFFF) | (s & 0xFFF00000), 0))`: deko3d's handle is the image id with the sampler id at bit
     20, relative to the bound descriptor sets (Renut's heaps). For both, the renderer writes the sampler words already
     shifted (`draw.cpp` `SamplerWord`), so a handle is the two words masked and or-ed: the form Ryujinx's bindless
     elimination follows to the constant buffer (a shift or other arithmetic there leaves the handle unresolved, and the
     texture reads black);
   - push-constant blocks become `std140` UBOs at a fixed binding;
   - `set`/`binding` pairs become per-stage binding numbers (UBOs: vertex constants 0, pixel constants 1, shared 2,
     push 5; SSBOs in (set, binding) order, the window 15 and memexport 14);
   - specialization constants become `#define`s (`SPIRV_CROSS_CONSTANT_ID_n`), so a variant is the same text with its
     values prepended;
   - `gl_VertexIndex` / `gl_InstanceIndex` become `gl_VertexID` / `gl_InstanceID`.
4. **uam** (`thirdparty/uam`, SwitchWakerHD's patched 1.1.0) to DKSH.

`-DBANJO_DEKO3D` changes three things in `shader_common.h`: the two vertex-fetch raw loads and the memexport raw store
(`vk::RawBufferLoad/Store` through 64-bit addresses, which uam cannot compile). The loads go through a read-only
`ByteAddressBuffer` (set 15 binding 15) the back end binds over the window of GPU addresses that holds the upload rings
(sized to the highest buffer, not 4 GB: Ryujinx makes a host buffer of the bound size), indexed by the address minus
the window's base (`RENUT_SHARED_UINT(560)`). The memexport store has its own `RWByteAddressBuffer` (binding 14,
`Deko3DBindMemExport`) over the memexport buffer alone: a writable binding over the whole window made Ryujinx write the
range back into guest memory the heap had reused (runs 7-9: corrupted heap, crashes in `operator new`).

Variants: the specialization constants (bools 1000+, blend skip 900, output trim 901) are `#define`s, so each pipeline's
values make their own text and their own DKSH. The variant's key is FNV-1a 64 of the text (`loadVariant`).
**`tools/deko3d_variant_cache.py`** builds the cache on the PC from Renut's pipeline lists (each key holds its shaders'
hashes and the bools, blend skip and output trim that `Build()` turns into constants; a specialised key also needs its
generic one) or from `banjo_deko3d_variant_log` lines, with the PC build of uam (the same patches): the console's list
and the PC's (15,943 pipelines) are 2,632 stage variants, 30 MB, compiled in 48 s with 14 threads. The cache goes next
to the NRO as `deko3d_shaders.bin` (index read at start, DKSH read on demand). A variant missing there is compiled on
the console by uam on a worker thread (uam in the NRO, as SwitchWakerHD does, 8 MB stack; ~0.6 s each in Ryujinx) and
appended to the file. The converted GLSL of each shader is in the shader package for that. The keys were checked: all
574 variants the emulator had compiled itself are among the tool's.

The file is read up to the first record that is not whole (its size runs past the end, or its data does not start with
"DKSH"): the process killed during an append leaves one. The file is cut there and new records follow the good ones;
each record is written with one write and flushed. Before (runs 20-22) the walk trusted a cut record's size, seeked past
the end, and later appends landed where the next start never found them: the same ~30 shaders were compiled at every
start. `deko3d_variant_cache.py --merge` reads a console's file the same way.

## 6. Memory and synchronisation

- Frames in flight: two (as Renut's Vulkan back end), each with a fence after its last command list.
- Command memory: a ring per frame slot fed to the `DkCmdBuf` in 64 KB pieces (`cbAddMem`); `dkCmdBufClear` and an
  explicit first piece at the start of each list (SwitchWakerHD: 0.5.0's clear rewinds into the previous slot's memory).
- Buffers: Renut creates its upload rings and static buffers once; each plume buffer is its own `DkMemBlock` rounded to
  64 KB, or a suballocation of a shared block for small ones.
- Images: the image heap; frees wait for the frame that last used them.
- Shader code: one `Code` block, bump allocated, with `dkCmdBufBarrier(..., DkInvalidateFlags_Shader)` after new code.

## 7. Steps

| Step | What | State |
|---|---|---|
| D0 | References read; design (this page) | done 2026-10-17 |
| D1 | PC tools: SPIRV-Cross CLI, patched uam (`thirdparty/uam`, built for the PC with MinGW: no `_GNU_SOURCE`, winpthreads) | done 2026-10-17 |
| D2 | `tools/deko3d_glsl.py`; the 11 host shaders to DKSH at build time (`tools/deko3d_host_shader.py`, gpu.cmake) | done |
| D3 | The translator's deko3d mode (`BANJO_DEKO3D_SPIRV_DIR`); `tools/deko3d_shaders.py`: all 4,881 game shaders compile, no uam warning (exact divisions: `BANJO_UDIV`); vertex inputs renumbered for uam's 16; `tools/deko3d_shader_package.py`: 157 MB of GLSL in a 15 MB package in the NRO (dictionary, per shader) | done |
| D4 | `plume_deko3d.{h,cpp}`, `plume_deko3d_commands.cpp` | done (builds) |
| D5 | Renut's renderer on it: `renut_gpu_backend = "deko3d"`, `IsDeko3D()` / `UsesVulkanBindings()`, `banjo_renut_deko3d.cpp` (GLSL package, DKSH cache `deko3d_shaders.bin` next to the NRO, report line) | done (builds) |
| D6 | Tests in Ryujinx (deko3d needs none of the NVK behaviour that stopped the emulator), then a console package | Ryujinx: boots to gameplay (run 20, section 10); package 29 (2026-10-17 22:11, `builds/2026-10-17_2211_round29_deko3d`): renut.toml on deko3d, `configs/banjo_renut_vulkan.toml` for the comparison, `deko3d_shaders.bin` (2,665 variants) next to the zip |
| D7 | deko3d-only gains: bound textures instead of bindless where a shader allows, pre-encoded pipeline state, the record worker writing deko3d commands directly | |

## 8. Audit before the first run (2026-10-17)

Checked against deko3d's source (devkitPro/deko3d master; the NRO links 0.5.0) and fixed:

- A pipeline without vertex input skipped the attribute and buffer state binds, leaving the previous pipeline's
  attributes fetching from buffers that may be gone. Both are bound always (deko3d makes the rest constants / disabled).
- Cube map arrays: deko3d multiplies `dimensions[2]` by six (it takes cubes); 1D arrays take their layers as the height.
- Scratch memory: 81 game shaders need some, the biggest 32 KB a warp; the queue had 16 KB. Now 64 KB, and a DKSH that
  needs more is refused with a log line.
- Missing vertex buffers bound address 0; they read a 4 KB zero buffer now (Vulkan's null buffer).
- Strides given with vertex buffers overrode the pipeline's; plume's Vulkan back end ignores them, so does this one.
- Compute bound UBO / SSBO slots nothing had set; its push constants' UBO is 256 bytes now.
- The index format alone changing rebinds the index buffer; the blend constant is written again in each list; a
  framebuffer of a mip level reports that level's size.

Confirmed: the blend registers' encoding (`getBlendFactorSetting`) and the alpha-destination bug; method 0x56E is
`FRAMEBUFFER_SRGB` (nouveau: 0x15B8); shader binds disable the graphics stages left out; image and sampler descriptor
sets bind for compute too; constant buffers are 256-byte aligned in Renut's upload ring; `depthClipEnabled` means what
plume's Vulkan back end makes of it; compressed render targets are a memory kind, so copies of them are right.

## 9. Open questions (to be settled by tests)

- Front-face winding with deko3d's origin and D3D's clip space (a flip of Renut's front face if culling comes out
  inverted). The intro's 2D frames are upright in Ryujinx; 3D culling is not checked yet.
- uam's code against NAK's for the main pass's shaders (sizes and GPU time per shader with `banjo_renut_diag_gpu_shaders`).
- Whether non-uniform bindless handles need anything on Maxwell (NAK emits none for them today).

## 10. Ryujinx runs (2026-10-17)

Ryujinx (Ryubing built from source, `D:\New folder\Ryubing-src`) runs the NRO on the deko3d back end with
`renut_gpu_backend = "deko3d"`, launched minimized with PowerShell's `Start-Process` (a `cmd /c start` with escaped
quotes hung without starting it). What each run found:

| Runs | Found | Fix |
|---|---|---|
| 1-4 | Crash in glcpp (`linear_alloc_child`): NVK's newer Mesa's symbol bound into uam | uam linked as one relocatable object with its strong symbols localised (`plume_uam_localize.cmake`; weak and COMDAT ones stay global) |
| 5 | `SetProcessMemoryPermission` throws in Ryujinx; a 1x1 swapchain resize | `RexGmProtect` does nothing without a loader; implausible sizes keep the swapchain |
| 6 | A 4 GB storage binding: Ryujinx makes a host buffer of the bound size | the window bound to the highest buffer's end |
| 7-9 | Heap corruption (crashes in `operator new`): the writable window made Ryujinx write the whole range back into guest memory | the window read only; memexport its own binding (14) |
| 10-13 | Green, then black textures: Ryujinx's bindless elimination follows a handle to the constant buffer only for plain or masked words | sampler words pre-shifted; bound textures (`dkCmdBufBindTextures`) for 3,935 shaders |
| 14 | Boots; the intro renders (Rare logo upright, right colours), 31-34 fps; 2 fps at each new shader (uam ~0.6 s a variant) | the offline DKSH cache (section 5) |
| 15 | 0 uam compiles, 1,374 cache hits, 31-33 fps on the intro; at the first level 12-25 s a frame | each draw binds its own fetch data (below) |
| 16 | The same slow frames; then shaders decoded from data and a lost device in Ryujinx | fence waits no longer give up (below); Ryujinx keeps a shader cache for the NRO |
| 17-19 | Through the first level's compile burst (1.2 fps and rising), then Ryujinx died of a stack overflow (0xC00000FD in coreclr). Run 19's stderr: `KScheduler.Schedule` asks a guest thread's name, read through the official SDK's TLS layout (libnx has another), the pointer is unmapped, `InvalidAccessHandler` prints the guest stack trace, which asks the name again: endless recursion. Not the renderer | Ryujinx's `KThread.GetThreadName` checks each read is mapped and stops asking a thread whose name it could not read. (Bigger stacks for the translating threads, tried first, stay: harmless) |
| 20 | **Boots to gameplay.** 8 minutes, no crash: the intro (Rare logo, Lord of Games' Pong TV, the 1998 film-reel flashback with subtitles) and Spiral Mountain with Banjo and the minimap, 26 captures; ~20 fps in Ryujinx (debug deko3d library and guards on); 1,546 DKSH cache hits, 30 variants compiled on the device (now in the SD cache); late fences during Ryujinx's shader burst signalled within 8 s | |

The red and blue exchanged in the Ryujinx captures (Banjo's shorts teal, Kazooie blue, the gold puzzle pieces blue)
was the captures, not Ryujinx and not the renderer (the console's colours were right, package 29):
`banjo_renut_capture_raw` (an emulator test key) copied the frame texture's bytes, which are R G B A
(`R8G8B8A8_UNORM`), and converted them as B G R A. Fixed in `present.cpp` (the channel order follows the frame
texture's format; only 8-bit RGBA and BGRA are copied raw).

Storage binding 15 covered every buffer (~500 MB: upload rings, static buffers and the image heap's mappings between
them). Now `draw.cpp` binds each draw's fetch data alone (`Deko3DBindFetchData`: from the address rounded down to 256
bytes, `vertex_fetch_bytes` long) and writes the offset from there into the address's low word, which is all the
shaders read of it, so the shaders are unchanged. That is right for the console too (a bounded binding), but it was
not what made run 15 slow. (`shader_common.h`'s comment on `g_DkAddressWindow` still describes the 4 GB window: to be
updated with the next shader change, which rebuilds every shader cache.)

The slow frames were Ryujinx itself: our render thread was busy 0.6-0.8 s of each 12-25 s frame, and those frames built
417 pipelines whose shaders Ryujinx translates and compiles for the host GPU on first sight. Ryujinx keeps no shader
cache for homebrew (`ProcessLoader.cs` nulls the title id), so every run paid it again; the local Ryujinx build now
takes `RYUJINX_HOMEBREW_SHADER_CACHE_ID` (16 hex digits; runs use `0500000042414E4A`) as the cache's id.
Run 16's crash: `waitForCommandFence` gave up after 10 s of a late fence, so the renderer reused command memory (with
the compute launch descriptors in it) and uploads the GPU had not read yet; Ryujinx then decoded garbage as shaders
and lost its device. A fence is now waited for until it signals or the queue is in an error state.

Ryujinx only, not the console: its shader translator logs "Failed to find storage buffer for global memory operation
Load" for 12-23 loads when the first level loads, and those loads read 0. It finds a storage buffer only for an
address of the form constant buffer + offset (+ constant); uam's code for some of the fetch reads reassociates the
offset's terms. Ryujinx's GPU thread also stalls (~11 s, "WaitOnSyncpoint ... took more than 1000ms") while it
translates a burst of new shaders; its own shader cache keeps them for the next run.

## 11. Package 29 on the console, and what followed (2026-10-17 evening)

The user's first console run on deko3d (handheld, CPU 1020 MHz, GPU 460.8 MHz, RAM 2733 MHz): the render thread busy
13-20 ms a frame in Spiral Mountain (about half of Vulkan's ~38 ms in package 28's logs), shader compiles on the console
about 0.4 s each (166 that the PC lists did not have; kept in `deko3d_shaders.bin` since), GPU busy 99 %: 23-35 ms a
frame where the game held 30 fps, 50-135 ms in the heavy windows (the main 1280x720 pass 81 % of it, shadow maps 9 %,
resolves 8 %). Banjo's colours right (the Ryujinx swap was the raw capture, section 10). Bugs reported: the depth of field
and the blocky pixels around characters (as on Vulkan), the river water missing, the full-screen colour flash (green
now, white or teal on Vulkan) and a partial one (the sky band cyan), Banjo's glow on the menu dropping out at 30 fps.

Found with frame-matched recordings (`banjo_video_s` / `banjo_video_frames`, aligned on the subtitle changes) against the
emulated renderer (`banjo_renderer = "native"` with `masseffect_renderer_native = false`, the reference):

- **Depth of field (Renut, any back end):** with `banjo_gpu_blend_skip` on, Kazooie, the radio and Banjo's fur edges are
  blurred where the reference keeps them sharp. Off, it matches; so does leaving out of it the five big material
  shaders (`67C8FBF86CF1570B`, `55DCA3ABEFC298FB`, `1EBB301092D0B603`, `AACAD91A37311B7B`, `28CBB618E0AE89FD`,
  `banjo_renut_diag_blend_skip_exclude`), and so does `banjo_gpu_bool_spec` off. Their pipelines: one 8-bit UNORM target,
  no depth or stencil write, SRC_ALPHA / INV_SRC_ALPHA colour blending: by the API's rules a pixel of alpha 0 or less
  leaves the target as it was, yet the pictures differ. The cause is not found; Renut's renderer no longer uses the
  blend skip (`banjo_renut_blend_skip`, off; the native renderer keeps `banjo_gpu_blend_skip`).
- **River water (deko3d):** reproduced in Ryujinx (the title screen's flyover, no input). Not the tessellation grid,
  not `stream_convert_cs` (`banjo_renut_diag_no_stream_convert`); with the blend skip off the water is drawn.
- **Glow and flashes:** not settled yet; the green flash fits a frame whose composite reads a texture still zero (zero
  decodes as green through the game's colour encoding, as the early Ryujinx runs' unresolved textures did).

`tools/deko3d_variant_cache.py` builds the variants without constant 900 now (`--blend-skip` keeps the lists' blend
skip).

## 12. The visual bugs found with RenderDoc (2026-10-18)

RenderDoc captures of Ryujinx (its Vulkan back end, through `VK_LAYER_RENDERDOC_Capture` set in the environment,
`tools/rdc_trigger.py` taking the captures by target control) show what the deko3d renderer drew, draw by draw. The
reference is a PC capture of Renut's D3D12 back end (`renut_gpu_backend = "d3d12"` under `renderdoccmd capture`,
`banjo_renderdoc_game_s`); PC Vulkan captures of renut.exe do not replay (device lost). Scripts: `tools/rdc_passes.py`,
`rdc_draw_detail.py` (the PC pipelines' names are `vs_<hash> ps_<hash>`), `rdc_event.py` (state, vertex inputs, bound
buffers, post-VS output, pixel history), `rdc_pixel.py`, `rdc_buffer.py`, `rdc_stencil.py`. Renut diagnostics added:
`banjo_renut_diag_show_texture` / `_show_gamma` (a texture on screen instead of the frame), `banjo_renut_diag_draw_log_s`
(every draw of two frames: shaders, counts, target, state), `banjo_renut_diag_stream_dump_vs` (a vertex shader's streams
and fetch copies).

- **Water (deko3d only): a uam miscompile.** `UBO.v[a][b]` with `b` not a constant compiles (TGSI) to a load of component
  `.x` of the vector the byte offset falls in. Renut's generic vertex fetch reads its slot so
  (`vertex_fetch_slots[index]`, index from the shared constants): the tessellated water (`vs_F09B139EE9188255`, fetch 95)
  read slot 92 (0) and took its patch corners from the start of the upload chunk (the tessellation factors 10, 10, ...):
  every water vertex was misplaced (the flooded valley; the pond had none). `tools/deko3d_glsl.py` now loads the vector
  whole and picks the component (`dk_pick_<type>`, a select chain): 10 vertex shaders, 70 reads. The GLSL package and the
  DKSH cache are rebuilt with it.
- **Non-square pixels (any back end on NVIDIA): alpha to coverage without samples.** With MSAA off the hardware dithers
  alpha to coverage (grass, fur and foliage edges in a checker pattern). `banjo_renut_alpha_to_mask_test` (on) makes it an
  alpha test at 0.5, as the native renderer's `banjo_native_alpha_to_mask_test`; the pipeline key keeps the bit (no new
  shader variants).
- **Depth of field and the halos around characters (any back end): the half pixel.** Xenos with
  `PA_SU_VTX_CNTL.pix_center` 0 (device word 0x29C0 bit 0, `D3DRS_HALFPIXELOFFSET` false) has its pixel centres on whole
  window coordinates; Xenia adds half a pixel to the viewport offset for it, Renut's renderer added nothing (its
  translator drops XenosRecomp's `g_HalfPixelOffset` line), so every draw sat half a pixel up-left. The depth of field's
  320x180 blur (`ps_48CC7A8717EC2A39`) samples one side only (0 and +2 texels): its result came out a whole texel (4
  pixels) up-left of the scene, the halo above Kazooie's crest (measured: the reference's blur aligned with its frame,
  Renut's at (-1, -1) on D3D12 and deko3d alike). `ComputeViewport` adds the half pixel (`banjo_renut_half_pixel_offset`,
  on): the edge difference to the reference recording halves over the whole intro (PC 126 -> 70; emulator 9.5 -> 4.2 a
  frame) and the halo is gone. The point sprites' `PA_SU_POINT_MINMAX` is read as radii too (`/ 8`, as Xenia;
  `banjo_renut_point_minmax_radius`).
- **Depth of field (any back end): the stencil in the depth words.** The composite (`ps_A5E26FB9039DA3BF`) tests bit 0 of
  the stencil byte of the depth resolve's words (`fract(z * 128) > 0.5`) to keep the characters sharp; Renut wrote it as 0,
  so Kazooie blurred in front of the focus. `depth_resolve.hlsli` reads the stencil through a stencil view of the depth
  target (plume: an `S8_UINT` view of a depth/stencil texture is its stencil plane on Vulkan and deko3d; the utility layout
  has the bindless table again on set 2 as uint) when `banjo_renut_depth_words_stencil` is on (default).
- **Colour flash:** a texture header read while the game rewrote it (dwords 3-5 zero) dropped a bloom level's resolved
  texture for one made from guest memory. `BindTextureLocked` now uses the device's fetch constant when the header
  describes other data (`[flash]` lines log it).

## 13. GPU time: copies, depth, Zcull (2026-10-18, package 33)

Package 32's console GPU profile (CPU 1020, GPU 460.8, RAM 1600 MHz): 46-152 ms of GPU a frame against 17-29 ms on the
render thread; resolves 4.4 ms and depth resolves 3.6 ms; the depth-of-field composite 4.9 ms for one draw (with its
words copy). What changed:

- **Colour resolves are drawn** (`banjo_deko3d_draw_resolves`, `DrawResolveCopy` in render/draw.cpp): a single-sampled
  resolve of a 2D destination in one of the formats a texel load returns exactly (RGBA8, BGRA8, the 16- and 32-bit
  float and unorm formats) is a draw of `resolve_copy_ps` (a `Load` of the source texel at the destination pixel
  plus the rect's offset) into the destination as a render target, with a framebuffer kept per destination. Before, a
  `copyTextureRegion` on deko3d's copy engine, which plume brackets with `DkBarrier_Full` both sides (the whole 3D
  pipe idle), about 17 times a frame. The 2D engine's MSAA resolve and the stacked or 3D destinations keep the copy.
- **The depth of field reads the depth words in place** (`banjo_gpu_depth_words_view`, on again): a view of the depth
  resolve's words target, no copy into the texture over the resolve's memory each frame (package 28b had turned it off
  as a depth-of-field suspect; the depth of field's faults were the half pixel and the stencil, section 12).
- **Depth compression** (`banjo_deko3d_depth_compression`, `Deko3DOptions::depthCompression`): depth/stencil targets get
  `DkImageFlags_HwCompression` as colour targets do.
- **Zcull** (`banjo_deko3d_zcull`): the queue is made without `DkQueueFlags_DisableZcull`; deko3d binds the queue's
  Zcull context and configures Zcull per depth target at `dkCmdBufBindRenderTargets` (invalidating it when another depth
  target is bound). A copy into a depth target (the copy engine does not update Zcull) is followed by
  `DkInvalidateFlags_Zcull` (`invalidateZcullAfterCopy`). The decal problem seen before was on NVK (the user's call).
- Ryujinx emulates neither compression nor Zcull: the emulator shows they break nothing (run 44, frame-matched against
  package 32: 0.4-1.9 levels mean difference, animation only); only the console measures them. Configs:
  `banjo_deko3d_copied_resolves`, `_words_copied`, `_depth_uncompressed`, `_zcull_off`, `banjo_deko3d_gpu_before`
  (all four off).

The shader work of the same package is in docs/deko3d-shader-optimisations.md.

## 14. Package 34 GPU work (2026-10-19)

Console run of 2026-10-19 (the emulator build of 2026-10-18 with the emulator's scripted route, the same route as
package 33's run window by window): busy-window medians 16.7 -> 25.2 fps, GPU 50.1 -> 32.0 ms a frame, render thread
38.7 -> 32.6 ms; the main pass 35.5 -> 25.0 ms a frame; resolves and depth resolves unchanged (2.0 and 3.1 ms: memory
bound); no shader compiled during play (231 in package 33's run). Heavy gameplay is GPU bound (52-56 ms), the intro
render-thread bound near the 30 cap.

- **Depth resolve words only where read** (`banjo_renut_depth_words_lazy`): every depth resolve wrote two targets, the
  float depth (what the shaders sample) and the words (depth and stencil as bytes, for the 8_8_8_8 view of the resolved
  memory). A census of the emulator route (the `[trace] sample depth words` lines): of the five depth resolves a frame
  (the shadow maps 1024x1024 and 768x768, two of 82F83304, one of 82F8329C) only 82F8329C's words are read, by the
  depth of field. A destination now writes its words in its first 300 resolves and, after that, only once something
  has read them (`words_wanted`); otherwise the resolve uses `depth_resolve_float_pipeline` (the words target bound,
  its write mask 0, no stencil read). A read of words its last resolve did not write is served by that resolve again,
  with the words, when its source surface is unchanged (`EnsureDepthWordsLocked`: the surface by generation, its write
  count); were it not, the read is counted as stale. Emulator run 53: 19,579 resolves without their words, 0 written
  late, 0 read stale. About 14 MB a frame less to write (4 bytes a pixel of 3.5 million pixels).
- **Resolves of an unchanged source** (`banjo_renut_resolve_skip`): each surface counts the draws that can write it
  (a colour write mask; for depth, a depth write or stencil writes) and its clears; a resolve whose signature (source,
  its rebuilds and writes, rectangle, place) equals the last one into its destination, and whose destination nothing
  else wrote since (uploads and words copies clear the signature), is not done. The repeated resolves of the route have
  a depth-writing draw between them: 4 skipped in 6 minutes. Kept (harmless).

## 15. Package 34 CPU work on the render thread (2026-10-19)

From the CPU report (docs/cpu-codegen-proposals-2026-10-19.md, section 0 lists what was taken and why not the rest).
The console profile of 2026-10-19 put a quarter of the render thread in `PmullHash`, under the exact stream checks.

- **Store cards** (`banjo_renut_store_cards`, PC test `banjo_renut_store_cards_verify`). With
  `banjo_renut_exact_streams` every cached vertex and index copy was hashed whole at its first use after each kick
  (about 10 kicks a frame; 16 MB a frame in gameplay; "0 stale caught" in every console window). Now every guest store
  outside the stack zeroes the card of its 4 KB page (`tools/store_cards.py`, the last post-codegen step:
  `REX_CARD(ea)` = `base[ea >> 12] = 0`, the cards in the guest window's first MB, which `Memory::Initialize` keeps for
  them), and so does every host writer of guest memory (`rex::memory::MarkStoreCards*`: the CRT memory hooks,
  `PhysicalHeap::TriggerCallbacks` for file reads, decommits and releases, the hand-written natives, the memexport
  write-back). `CardsProve` (`render/draw.cpp`) reads the cards of a copy's physical pages under the four views the
  guest writes them through (0xA0000000, 0xC0000000, 0xE0000000 one page down, 0x7F000000 below 16 MB), sets the
  written ones back to 1 and records the interval it found them in (`g_page_serial`); a copy is current when none of
  its pages, nor the page before its first (an unaligned store marks only its first byte's page), was found written in
  or after the interval it was last proven in. A card read before its store's data is visible is found in that
  interval, so the copy is hashed again in the next one: the cards never answer for an interval in which they were
  cleared. Interlock: the renderer uses them only when the generated code marks them (`g_banjo_store_cards`,
  `banjo_hot_guest.cpp`: `REX_CARD` defined and `REX_STORE_CARDS_OFF` not). Cost: 226,000 store sites, one `lsr` +
  `strb` each (the NRO +2.7 MB).
  PC check (console settings, `renut_auto.toml`'s scripted route to Spiral Mountain, `banjo_renut_store_cards_verify`,
  which hashes every copy the cards prove as well): about 450,000 copies a 10 s window proven by their cards, 10-150
  hashed because a page was written, **0 changed under clean cards** in every window; without the verify, the render
  thread at the 30 fps cap 6.1 -> 4.5 ms a frame, hashing 4-10 GB per 10 s -> under 25 MB. The first version read
  only pointers into the raw physical view; the copies are read through the buffers' CPU addresses (the 0xA/0xC/0xE
  views of the guest window), so it never proved one (`PhysicalOf` maps both). The `[cards]` line (verify mode only)
  counts what the checks find by view, the host writers' marks and the card table.
  Emulator run 54 (Ryujinx, deko3d, the scripted route): about 478,000 copies a 10 s window proven by their cards,
  0-6 hashed; render thread median 20.8 ms a frame at 29 fps. The same route before the cards (every copy hashed,
  5.2 GB a 10 s window): run 51 31.9 ms at 28.8 fps, the cleanest of runs 49-53, which ranged from 9 to 29 fps with the
  host's load (emulator timings are only comparable between runs on an idle host). Ryubing turns the 64-bit PMULL into
  one PCLMULQDQ, so the hash is not slower there than its share on the console suggests: about -35 % of the render
  thread on the emulator, against the console profile's quarter of it. Pictures as before; 4 shaders compiled at start-up,
  none during play.
- **Memos** (`banjo_renut_cpu_memos`): texture bindings per fetch slot (`FillTextures`, valid within the frame while
  `g_texture_epoch` holds: every texture made, retired, uploaded, resolve destination set, words copied or viewed bumps
  it; a hit does the bind's layout transitions and counters again), render targets (`GetHostSurfaceLocked`,
  `g_surface_epoch`), the recording threads' buffer lookups (`NewOp`: per thread, `g_record_generation`, the lock only
  with Begin/End vertices pending). The pipeline memo (`banjo_renut_pipeline_memo`).
- **plume deko3d binds**: a pipeline keeps its stages (no `dkShaderGetStage` per bind) and nine hashes of its
  fixed-function groups; a bind after a pipeline of other state sends only the groups that differ. The dynamic depth
  bias and stencil reference make their groups unknown.
- Smaller: the replay's predication set once per command buffer; clears and resolves from the draw-capture pool (no
  heap allocation on either thread); `Active()`, `State()`, `ActiveBackend()` and the tuning switches inline.

## 16. GPU work without shader changes (2026-10-19, package 35)

From the package 34 console profile (renut_auto.toml, two runs): gameplay GPU 34.1 ms a frame, of which depth
resolves 3.3 ms, colour resolves 2.2-2.6 ms, present 0.6 ms, exported stream conversion 0.3 ms (up to 2 ms in heavy
scenes). The items below change no game shader.

- **Barriers merged** (`banjo_deko3d_barrier_merge`, `Deko3DOptions::mergeBarriers`): plume issued one
  `dkCmdBufBarrier` per `barriers()` call, and Renut moves several textures in a row (a draw's textures, a resolve's
  source and destination), each a pipeline drain. A barrier is now held until the next command that does GPU work
  (`flushBarriers()` before draws, dispatches, clears, copies, resolves, blits, timestamps, target binds and the end
  of the list) and merged with any recorded before then: the strongest wait, every invalidation. The same ordering.
- **Lean barriers** (`banjo_deko3d_barrier_lean`): a transition into a layout that is only written next (colour,
  depth, present) waits for the work before it but keeps the texture cache: only a read after a write needs it
  invalidated, and that read's own transition does it. A texture going from one read layout to another (shader read,
  depth read, copy or resolve source) needs no barrier at all (`ReadOnlyLayout`, render/textures.cpp; the same for
  surfaces). deko3d has no image layouts: nothing is reordered.
- **Barrier counts** in the deko3d report line ("barriers: N primitives, N fragments, N full (N invalidating
  textures)"), cumulative.
- **Present without the letterbox clear** (backend/present.cpp): the clear of the whole output before the scan-out
  only when the frame does not cover it (FitFrame's viewport); 1280x720 on the console, so never.
- **GPU profile per-shader breakdown sampled** (backend/gpu_profile.cpp): with `banjo_renut_diag_gpu_shaders` a
  timestamp at nearly every draw; now one frame in four is timed, and the averages (and draw counts) are over the
  timed frames.
- **Depth hand-over** (`banjo_deko3d_depth_handover`, render/draw.cpp "Depth hand-over"): the depth resolve's float
  target is the depth it loads, unchanged (depth_resolve.hlsli), so sampling the depth target's own image gives the
  same values. A resolve of a whole single-sampled depth target into a destination of its size that writes no words
  (`banjo_renut_depth_words_lazy`) is not drawn: the destination records the target, its generation and its writes,
  and a bind samples the target's image through a depth view with the destination's component mapping
  (`DepthImageViewSlotLocked`). Before the target is bound again (`BindTargets`: a draw or a clear), goes
  (`RetireTarget`), or is a target of the draw that samples the destination, the resolve is drawn after all from the
  unchanged image (`MaterialiseLocked`). A clear of the target's whole depth (with its stencil, or of a target whose
  stencil no draw tests and no clear writes) instead gives the image to the destination (`held`) and the
  destination's previous image, or a new one, to the target (`TakeImageLocked`, `SwapDepthImageLocked`: texture,
  generation, layout and views move together; framebuffers are cached by surface and generation, so each image keeps
  its own). The shadow maps are cleared whole (depth only), drawn, resolved once and sampled by the scene each frame:
  they are no longer copied. The main depth's resolves are followed by draws into it, so they are drawn as before
  (one draw later). Memory: one more depth image per shadow map (8 MB for 1024x1024, 4.5 MB for 768x768). The
  10 s line "depth hand-over" counts resolves not drawn, binds of a target's image, images taken at a clear, resolves
  drawn after all (from a taken image) and contents lost (none expected).
  Emulator runs 55 and 56 (the scripted route): in gameplay about 2 of the 4-5 depth resolves a frame are no longer
  drawn (the two shadow maps; 100-380 images taken per 10 s window), the scene samples the shadow maps from their
  depth images (400-1,400 binds per window), 0 lost; the main depth's resolves are drawn one draw later as before.
  Pictures as before (shadows, depth of field), including the captures taken while images were being taken. First
  version: a depth resolve's words (written in a destination's first 300 resolves) marked the target's stencil as used
  and no image was ever taken; a stencil nothing writes is undefined either way, so only stencil tests and stencil
  clears count now.
- **D24S8 depth targets as 24-bit unorm** (`banjo_deko3d_depth24`, render/format.cpp `DepthTargetFormat(d3d_format)`):
  plume has a new `RenderFormat::D24_UNORM_S8_UINT` (appended to the enum, so earlier values and pipeline lists keep
  their meaning; deko3d `DkImageFormat_Z24S8`, Vulkan `VK_FORMAT_D24_UNORM_S8_UINT`, D3D12 `R24G8_TYPELESS`). On deko3d a
  D24S8 target (the shadow maps, the small render-to-texture targets) takes it instead of `D32_FLOAT_S8_UINT`: 4 bytes a
  pixel instead of 8 for its draws, clears, resolves and samples. Its depth is the 24-bit unorm value the Xbox 360
  keeps (a resolve or a sample gives the unorm as a float, as the console's fetch does); the float target held more bits
  than the console, so this one changes values by less than a 24-bit step, towards the console's. The scene's 20e4
  float depth (D24FS8) stays 32-bit float. An EDRAM depth view bound as both formats would get two host targets (the
  format is part of a target's key): once a view has been bound as D24FS8, every surface over it is 32-bit float
  (`g_depth_view_formats`, render/surfaces.cpp, logged). Renut applies no depth bias, so no bias scale changes.

## 17. Package 35: the render-thread report, the Jinjo Bingo fix, input latency (2026-10-19)

**Render thread** (docs/render-thread-proposals-2026-10-19.md; numbers are the report's proposals):
- 1, store cards for texture content checks (`CheckedHash`, render/textures.cpp; `GuestRangeCardsClean`,
  `CardsInterval` in draw.cpp): a check reuses the last hash while no page of the texture's extent was written since
  the interval it was taken in; a dynamic linear texture is not uploaded again while its pages are clean since its last
  read (`read_interval`). Emulator verify run 59: over 99 % of checks proven, 0 changed under clean cards.
- 2 and 5, pushed constants (`banjo_deko3d_push_constants`, draw.cpp "Pushed constants"): three fixed uniform buffers
  updated with `dkCmdBufPushConstants` (plume `Deko3DPushUniform`, `Deko3DBindUniform`, `Deko3DSetSharedWords`): the
  64-byte groups a shader reads that differ from the per-list model of the buffer, its literal registers, and the shared
  block's changed lines. Point-sprite draws keep the ring (point_sprite_gs reads SharedConstants by address).
  `banjo_deko3d_push_constants_verify` checks the model at every draw. Three model pitfalls the verify found on the
  emulator, for other ports: (a) the ring uploads and the pushes need separate models, and each path resets the other's
  replay tracking (they share the replay's dirty masks); (b) a draw compares only the registers its shader reads, so
  after it, in a group it compared but did not push, only those registers are known: the other registers of the group
  must leave `valid` (run 61: 8 % of draws read a stale one when a later shader read the rest of the group); (c) a
  shader that indexes reads every register, its own literal registers too, which hold the literal: those are not
  compared or pushed with the device's bytes, and `literal_regs` holds one shader's registers only. Ryujinx's
  constant-buffer update staging is 512 words: `Deko3DPushUniform` splits updates into 1 KB pieces.
- 3, the texture binding memo keyed by texture (512 entries) and `g_texture_epoch` bumped only where a binding can
  change (not by uploads or resolves into the destination already there); `g_depth_epoch` for binds through depth words.
- 4, memexport bookkeeping: shaders without exports leave `GatherMemExportStreams` at once (per-entry flag), the
  draw's streams gathered once, export jobs made in place, `TextureGuestExtent`'s memo a static 256-entry table.
- 6, the pipeline memo 512 sets of 2 ways; `BlendSkipMode` / `OutputTrimMask` memoised. 7, GPU profile labels only on
  timed frames. 8, recent-framebuffer list, surface memo of 64 hashed entries, viewport memo. 9, stream and index memo
  slots hold their keys. 10, float constant uploads sized to what the shader reads (`AllocateWithRoom`), fetch data
  allocated only for copies not already in the chunk. 11, guest memory bases cached, `Latch()` always inline. 12,
  per-shader group summaries. 13, plume's per-draw counters per list, stats and options inline. 16, prefetch at draw
  entry. 18, upload trim after 900 frames, block logs capped. 20, no heap allocation per draw. 21, bound-fetch scan
  skipped for shaders without such inputs. 22, small inlines. 23, the ring clean on a thread on the background core
  (`RingCleaner`, backend/video.cpp; a range not started is cleaned by the render thread when needed).
- Not taken: 19's lock batching (it hung the emulator at the first loading screen, run 58: a game thread's hook and the
  render thread's next item wait on each other; one clock read per item kept), 9's per-page card memo (a write after the
  first check of an interval would be missed), 17 (moot with 2 and 5), 14, 15, 24 (after the console test).

**The Jinjo Bingo silhouette** (Renut upstream 574b488, Beta-Nut 4): SharedConstants grows to 1,360 bytes with
`pixel_origin` at 1344, the corner of the tile a draw predicated to one tile renders to (`ComputeViewport`), and the
translator's pixel position (VPOS) subtracts it (`g_PixelOrigin`, shader_common.h). Every shader's shared block grew, so
the GLSL package and the DKSH cache were rebuilt (with the early-alpha overrides regenerated: they carry their own copy
of shader_common.h).

**Input latency**:
- Kick submits (`banjo_deko3d_kick_submit`): at a kick (`Video::KickLocked`, after the kick's fills) the open list ends
  when it holds 200 draws (at most 3 more lists a frame, `kick_lists`) and goes to the GPU at the next kick or at the
  present, before the frame's last list. The GPU starts the frame while the render thread records the rest.
- Late input (`banjo_late_input`, backend/render_thread.cpp): the game's first controller read after a Swap is delayed
  by the least slack of the last 16 frames (the time it then waited at its Swap for the frame before, plus the delay it
  had) less 2 ms (`rex::latency::SetFrameReadDelayFunction`, sdk/src/core/frame_latency.cpp, from futari-nx's probe).
  With the game thread or the frame cap the limit there is no wait and no delay.
- The `[latency]` line every 10 s: input -> swap -> present, and the mean delay.


**Console run of package 35 (2026-10-19 evening)**, the scripted route against package 34: render thread 25 -> 18 ms a
frame in gameplay (CPU total 177 % -> 160 %), depth resolves 3.2 -> 2.15 ms a frame (the hand-over), but the heavy fur
scenes 48-52 -> 52-55 ms of GPU: the blended fur shells (2F36 9.0 -> 12.1 ms, B3C1 / D40E / C92B about twice) ran
their whole shader before the alpha test. The cause: `tools/renut_early_alpha.py` did not know `g_PixelOrigin`, so the
3,604 pixel shaders that read the pixel position lost Renut's early alpha test ("unsupported: variable g_PixelOrigin";
3,663 copied again after the fix). For other ports: a new per-draw value the translator reads in `main()` must be added
to the early-alpha tool's `UNIFORM_WIDTHS`, and the copies counted after every shader regeneration (the opaque
variants do not show it: only alpha-tested and blended draws run the early test).

## 18. Latency batch 3: the frame limiter on the display's refresh (2026-10-19, package 35b)

Package 35's `[latency]` lines on the console: input -> present 25-38 ms in the 30 fps scenes, 78-107 ms in the 18 fps
GPU-bound ones (swap -> present 66-95 ms: finished frames queue behind the GPU; the late input delay sat at its fixed
30 ms ceiling there).
- The display's refresh (`banjo_display_sync`, `renut_engine/display_sync.cpp`, after futari-nx's
  `futari_vblank_display_sync`): a thread at priority 0x2A on core 2 waits for the display's vsync event
  (`viOpenDefaultDisplay`, `viGetDisplayVsyncEvent`) and keeps the refresh time and period. The frame limiter
  (`frame_cap`, `renutFrameLimit`) releases each frame at a refresh plus a phase instead of on its own 33.3 ms timer,
  whose phase slid against the display (an image finished just after a refresh waited almost 16.7 ms more; frames
  shown three refreshes and one). A frame that ends after its time is released at once, as the timer did (no 20 fps).
- The phase (`banjo_display_phase_auto`): at each refresh the newest presented frame whose GPU work is done (a copy of
  its fence, `plume::Deko3DCopyFence` / `Deko3DFenceCopyDone`, `Video::PresentDone`) is the one shown from it; a frame
  released on the grid counts the refreshes from its release's refresh to its display. One more than the least of the
  last 32 frames: a miss, the phase 2 ms earlier and no later for a minute; 90 frames in time: 0.25 ms later, up to
  `banjo_display_phase_max_us` (12 ms). Unlike Futari (a 60 Hz game whose image is done within the refresh), Banjo's
  frames take two or more refreshes from release to image, so "not done at the first refresh after the present" is
  normal here and the miss is the refresh count.
- Late input's ceiling: 80 % of the game's frame interval (at most 60 ms) instead of a fixed 30 ms.
- The `[display]` line every 10 s: refresh period, present -> display, input -> display, release -> display, images
  never shown, the phase, misses, and diagnostics (slowest fence test, longest release wait, clamped releases).
- **Off by default in package 35b.** Ryujinx refused a second `OpenDisplay` of "Default" (`AlreadyOpened`; libnx's
  default window holds it; our Ryujinx fork now answers as the console does). With the thread active, every emulator
  run stalled or hung (65: hung at the start of gameplay; 67: 2.5 minutes without frames or file reads, then went on;
  68: hung at boot), and the runs without it (64, 66) did not. Taking the locks out of the fence test (the render
  thread waited on the display thread's mutex while it tested a fence) and a larger stack did not settle it. Not
  explained yet; `configs/banjo_display_sync_on.toml` is the console try. Futari's version of the thread is the
  reference that it can work.

## 19. The night of 2026-10-19: GPU for the heaviest scene, the colour flash

The target (the user): the heaviest scene of the scripted route at 30 fps without drops, at least 25, the same
pixels. Its GPU time on the console (package 34 state, stock handheld 460.8 MHz): 51.6 ms a frame on average; 30 fps
needs about 33, 25 fps about 40. The breakdown of those frames: the fur family about 17 ms (2F36's 21 blended shells
8.8 ms at 1280x720 and 1.9 ms in the HUD's 96x96 box; C92B, B3C1, D40E, 2D82), the lit materials about 15 ms (27-34
texture fetches each, 24 of them shadow taps over three cascades), depth resolves 3.2 ms (2.15 since package 35),
colour resolves 1.9 ms, shadow-map draws about 2.5 ms, A5E2 (one full-screen pass) 1.9 ms.

- **Texture size buffer** (`banjo_deko3d_txq_constants`, tools/deko3d_glsl_opt.py pass txq, constant 908): A5E2's
  1.94 ms matches its texture work exactly (8 texture operations a pixel over 921,600 pixels at the 2 SMs' 4 a clock
  at 460.8 MHz: 2.0 ms), and 4 of the 8 are `textureSize(t, 0)` queries. Plume deko3d records each descriptor's
  level-0 size when it writes the view (`Deko3DDescriptorSet::imageSizes`) and keeps a 64-entry uniform buffer at
  binding 3 up to date with pipelined updates for the bindings a shader queries (the banjo-dk line's `txq=`); the
  shaders read the size there. A5E2 8 -> 4 texture operations, 259A 3 -> 2, the fur family 1 fewer. 908 = 0 is the
  old query (the A/B, in the cache), 908 = 2 runs both and paints magenta where they differ (the verify).
- **Depth tested before discarding shaders** (`banjo_deko3d_early_z`, pass earlyz, constant 909): a pixel shader with a
  discard has the SPH's kills_pixels bit; in a draw that writes neither depth nor stencil (RB_DEPTHCONTROL bits 0 and
  2) `layout(early_fragment_tests)` gives the same pixels (the discard has nothing to undo; the renderer has no
  occlusion queries) and spares the shader the pixels the depth test rejects: particles and other blended effects.
  Not the fur: its shells, blended or not, write depth (RB_DEPTHCONTROL 0x00708746 / 0x00708767). 2026-10-20: a stencil
  test that cannot write (write mask 0, or every op of both faces KEEP) counts as writing nothing (7 more pipelines;
  the stencil-tested blended materials such as 3A25 REPLACE on pass with mask 0xC6, so they keep the late test).
  The DKSH differs only in the header's early-fragment-tests byte.
- **The fur family with 32 GPRs** (`banjo_deko3d_gpr32`, off: an A/B, constant 910, tools/gpr32_shaders.txt): 33-40
  GPRs leave about 48 of the SM's 64 warp slots, 32 all of them; uam's CompileBest caps the register file for a variant
  whose text defines 910 = 1 (thread-local cap read by TargetNVC0::getFileSize). The fur shader: 39 -> 32 GPRs, 15
  loads and 5 stores of spills, 1020 -> 1037 instructions. Whether the occupancy pays for the spills is for the
  console.
- Measured and not taken: specialising the stacked-texture flags (2-5 % fewer fur instructions, no register change);
  gathers for the shadow taps (the tap offsets are game constants: not provably the same texels); per-sample shading
  of the HUD's 4x box (the shaders shade per pixel already).
- **The colour flash** (`banjo_renut_stall_present`, off): while the game stalls (loading) the overlay pump re-presented
  the last front buffer every 16 ms; a texture the game freed or remade meanwhile is, on deko3d, a new image whose
  memory nobody cleared (Vulkan's came zeroed): a full-screen frame of other data, more often in the slow emulator.
  Now nothing is presented during a stall unless a menu or notification is up (the display keeps the last frame, as the
  console does), and a front buffer no resolve wrote is never shown. A `[flash]` warning also names any texture sampled
  with nothing written to it (an upload from guest memory that failed). Not caught on video yet (two recordings of the
  flash guard's moment showed none): the console run says.
- **The colour flash, a second cause** (`banjo_renut_device_swizzle`, on, package 36c): caught in a RenderDoc capture
  of Ryujinx (run rdc84, frame 5437): the whole frame washed yellow. `tools/rdc_draws.py` showed two of the bloom
  chain's 80x45 draws reading their source through the swizzle 1,1,0,0 and Z,X,X,1 where the other captures read the
  same texture as RGBA. BindTextureLocked reads the texture header in guest memory at render time and kept it whenever
  it described the same data as the device's fetch constant (SameData: base, size, dimension), swizzle included; the
  game builds the bloom levels' headers as temporaries and had rewritten this one for a later pass. The swizzle now
  comes from the device's fetch constant (SetTexture's copy, what the GPU reads); a `[flash] ... sampled through the
  device's swizzle` line logs the first 16 and the periodic report counts them (`[flash] device swizzle: N binds in M
  frames`, with the key off too). Not rare: emulator run 85 hit it in every frame of the jigsaw transition into the main
  menu (stack header 0x701CF9F8: X,Y,1,1 where the device has X,X,X,1, i.e. blue and alpha forced to 1), the likely
  cause of package 30's "menu glow cuts out at 30 fps" (the game thread further ahead of the render thread at the cap)
  and its cyan flashes. Final form (36c): SetTexture copies the header's dwords 0, 3 and 5 unchanged and changes only
  dword 4's mip levels (d3d_native_state.cpp), so a same-data header differing in 0, 3 or 5 is stale and the whole
  device fetch constant is used (a run's counts: dword 4 tens of thousands, by design; dword 3 a few dozen; dword 5
  once; dword 0 never). Run emu88 logged the bloom level's header at 93D = 1,1,0,0 (the yellow) against the device's
  60A: the flash, prevented.
- Shader compiles in play: the console's second run of package 35 compiled 0 (the first 12, then appended to the SD's
  cache); the pipeline list's prebuild at boot and loading is what precompiles. The next cache should be built from
  the console's own pipeline_list.bin.
- **Skinning, 2026-10-20** (tools/deko3d_glsl_opt.py passes bonesel and aw4; 324 vertex shaders): the fur vertex
  shader runs once a shell (21 shells a frame at 1280x720 and 21 in the HUD's 96x96 box, whose draws cost 89 us each
  on the console for a few thousand pixels: the vertex work). bonesel: a predicated bone's rows `D = mix(P, L, c)` feed
  only its own sum `mix(A, (D * w) + A, c)`, which keeps A whenever D would be P, so the sum reads L (the data selects
  go: 36 a vertex). aw4: each bone row's four address-window words W..W+3 are one `uvec4` read of a second view of the
  same binding at W >> 2, where W is provably a multiple of 4: the fetch data's offset, slot bases and size are
  16-byte multiples (render/draw.cpp's copies; the renderer counts draws where they are not and warns, expected 0),
  and the per-vertex part is `int(floor(x)) * K` with K a multiple of 4. uam still splits the uvec4 into four 32-bit
  loads, so the gain is the index arithmetic. The fur vertex shader: 594 -> 509 instructions, 56 -> 48 GPRs; its
  depth-only (shadow map) variant 473 -> 388, 43 -> 35.
- **Selects whose other arm nobody reads, 2026-10-20** (pass guardsel, both stages): a value read only where a
  condition holds (inside the chosen arm of selects on it, or inside `if (c)` blocks, directly or through the values it
  feeds) cannot show what a select on that condition in its own definition gives when it does not hold, so that select
  is its chosen arm. Renut's lights select every term on the light's flag and add it only under the same flag: 20
  selects a lit shader (fur 775 -> 763 instructions in the heavy scene's variant), A5E2 153 -> 137. A greatest fixpoint
  over every variable's readers; the conditions must have one value a run (assigned once, or once in each arm of one
  if / else); not in bodies with loops.
- Point lights are about a third of the lit shaders (the fur's heavy-scene variant 775 instructions, 516 with the
  lights off). deadcomp + a second guardsel round take ten more selects a lit shader (fur 714). The lights' work in
  branches on their reach is an A/B, off (`banjo_deko3d_light_branch`, constant 911, the 24 heaviest lit shaders):
  the game picks lights per object, so characters are usually inside all of them, large surfaces less so; the
  console's A/B says whether whole warps skip often enough (docs/deko3d-shader-optimisations.md 2.38, 2.39).
- **The game's gamma ramp is not applied** (found 2026-10-20 from the Peggle 2 port's note): N&B loads a ramp of about
  x^1.17 (10-bit entries [64] = 192, [128] = 459, [255] = 1019; the identity is 257 / 514 / 1023), which the 360's
  display and banjo-nx's native renderer apply (me_native_system.cpp NoteGammaRampWrite). Renut's hooks store it at
  device+0x3A9C (d3d/hooks/device.cpp StoreGamma) and present_ps.hlsl applies only `pow(c, 1)`: the deko3d builds are
  brighter in the midtones than the console. Futari (x^1.14) and Peggle 2 (x^1.17) measured the same in theirs.
  `banjo_renut_game_gamma` (off: it changes the picture, the user decides; configs/banjo_renut_game_gamma_on.toml):
  the Swap hook hands the device's ramp to the renderer (`Video::NoteGameGamma`), which keeps it in a 256 x 1 RGBA32F
  texture (uploaded only when the ramp changes, logged once as "[renut] game gamma ramp applied") and present_ps.hlsl
  looks each channel's 8-bit value up in it (param1 = 1 + its bindless slot; 0 = off, the output as before). Exact as
  the 360's display: the 10-bit entry for each 8-bit value, then the swapchain's 8 bits. The log's entries match the
  native renderer's ([64] = 192, [128] = 459, [255] = 1019). Pitfall (emulator run 78, black picture; the SA2 port found
  the cause): Ryujinx follows a bindless handle only back to a constant-buffer load, so a slot computed in the shader
  (`uint(param1) - 1`) read zero there; the slot now goes in as raw bits (`asuint(param1)`, 0 = off). The ramp is
  uploaded every frame (4 KB). Run 81: right.

### Where the heaviest scene stands, and what is left (2026-10-20 morning)

Measured (2026-10-20, the console, package 36g with skin once on, renut_auto.toml's scripted route, against package 35
on the same route): the dialogue scene's periods 52.7-55.0 -> 42.4-43.9 ms of GPU a frame (18-19 -> 22-23 fps), the
heaviest period 67.3 -> 55.8 ms (14.8 -> 17.7 fps), the whole route's mean 32.2 -> 28.5 ms. Of the heaviest period's
55.6 ms the fur family is 22.6 ms (2F36 alone 10.9 ms main view, 0.9 ms portrait), the lit 3A25 / 0BBB / 2D82 about 7
ms, depth and colour resolves 4.1 ms (the main depth resolved twice a frame with its words for the depth of field;
about 20 colour resolves, none of an unchanged source), shadow maps 2.3 ms (about 500 draws, a few us each: per-draw
cost, not shading). What is left exact: the console's A/Bs (skin once, light branch, gpr32). The 40 ms of 25 fps in
the dialogue scene is within reach of fewer fur shells (not exact, the user's call) or the docked clock.
The estimate below was written before these measurements.

Package 34/35's heaviest frames: 51.6 ms of GPU at the stock handheld 460.8 MHz (the GPU busy the whole frame: 52.0 ms
of work in a 52.4 ms frame, so nothing is gained by feeding it sooner). Tonight's exact work (packages 36, 36b) is
estimated at about 3 ms of it (weighted pixel-shader instructions -5.4 %, vertex -10.1 %, A5E2's texture work halved):
about 48-49 ms, 20-21 fps. 25 fps (40 ms) and 30 fps (33 ms) are out of reach with the same pixels at that clock; the
console's stock docked clock (768 MHz) would put the same frames near 29 ms.

Next, in order, once the console's runs of 36b are in:
- `configs/banjo_renut_diag_fur_vertex_only.toml` measures the fur draws without their pixel work. If the vertex share
  is large (estimate ~5 ms: 21 shells re-skin the same mesh, twice with the dialogue portrait), skin once a frame and
  let the shells read it (a compute or vertex pass writing positions and normals to a buffer; exact only if the
  skinning instructions compile the same, to be proven with tools/difftest), and 128-bit bone-row loads (aw4 rows
  through their own binding plus a uam flag letting MemoryOpt merge loads it knows are 16-byte aligned; a misaligned
  128-bit load faults, so the renderer must refuse draws whose fetch data is off 16 bytes before submitting them).
  Measured 2026-10-20 (tools/difftest/uam_ops.py, the fur VS B814D7237E615990 generic): without uam flag 2 (the
  global-load merge block) 596 -> 560 instructions, LDG 48 -> 12, GPRs 48 -> 50. Dropping flag 2 for every shader is
  NOT safe: Renut's vertex fetch reads words at vertex * stride + offset with strides of any multiple of 4, so merged
  64/128-bit loads could be misaligned. Done in package 36d (docs/deko3d-shader-optimisations.md 2.41, uam flag 1024,
  `banjo_deko3d_uam_wide_rows`): a 4-component load of buffer 15 (only the aw4 view) is one 128-bit load, aligned by
  construction (the binding starts on 256 bytes, a row is element n of 16 bytes), so no refusal is needed. The console
  profile shows these vertex shaders memory-bound (the fur in the 96x96 portrait: 89 us a draw), so more than the
  1 % the instruction count suggested; the console's A/B decides.
- The light-branch A/B (`banjo_deko3d_light_branch`) and the 32-GPR A/B decide whether occupancy or branch skipping
  pays on lit shaders. Package 36c makes both in-run A/Bs (`banjo_renut_ab_target` 15 / 16; configs
  `banjo_deko3d_light_branch_ab.toml`, `banjo_deko3d_gpr32_ab.toml`): the off half's pipelines are keys of their own
  (PipelineKey.reserved bits 0 / 1, built without constant 911 / 910), so one run gives both GPU times of the same
  scenes.
- Not exact, for the user to choose: fewer fur shells (~6 ms for half; package 36c has it as `banjo_renut_fur_shells`,
  default 1 = every shell: N draws every Nth of each fur part's draws in a frame (the game draws every part once a
  shell, shell by shell), the first always, render/draw.cpp
  DrawStateLocked; configs/banjo_renut_fur_shells_2.toml), the dialogue portrait every other frame (~1.4 ms while it
  shows), fewer lights on the outer shells, or a GPU clock above stock.

## 20. Input latency without vsync: the latency trim (2026-10-20, package 36e)

Package 35's `[latency]` lines showed where the GPU-bound scenes lose their input: swap -> present 66-95 ms at 18 fps,
finished frames queued behind the GPU. The pipeline: the game thread may run one frame ahead of the render thread
(`kMaxFramesAhead`), the render thread one frame ahead of the GPU (`kFramesInFlight` = 2: after submitting frame k,
`SubmitAndAdvance` waits only for frame k-1). Where the GPU is the limit, the render thread finishes recording frame
k+1 about (GPU frame - recording) before the GPU can start it, 30 ms and more in the heaviest scenes, and late input
(section 17) cannot see that wait: the game thread is released when the render thread finishes, not when the GPU
needs the next frame.

- `banjo_deko3d_latency_trim` (off; configs/banjo_deko3d_latency_trim_on.toml), backend/video.cpp: each kick
  submission signals a fence (plume's `Deko3DCopyFence`, waited with the new `Deko3DFenceCopyWait`, render thread
  only: no fence polling thread, which is what made the vsync thread of section 18 hang Ryujinx). After a frame is
  submitted and presented (`Video::Present`, `detail::LatencyTrimLocked`), the render thread waits until the GPU has
  finished all but the frame's last part (its last kick list): the next frame is recorded, and the game thread released,
  that much later, while the GPU still has the last part to run. The frame's first kick list then goes to the GPU at its
  own kick (the ring cleaner waited for) instead of at the next one, so the GPU gets the next frame's work as soon as
  possible. If the GPU still finishes a held-back frame before the next one sends it work (it would sit idle), holding
  back stops for 60 frames. Same pixels; the GPU's work is unchanged.
- The probe (sdk/src/core/frame_latency.cpp) now has `NoteGpuDone`: the `[latency]` line ends with "input -> GPU done"
  (the earliest the display can show the frame; seen at the kicks, the presents and the slot waits, a few ms late at
  most), and with the key on a "latency trim" line: frames held back, the mean hold, how often the GPU ran dry.
- Expected: about a GPU frame less the last part's time off input -> GPU done in GPU-bound scenes (the heaviest: ~30-40
  ms); nothing where the frame cap or the CPU is the limit (the hold returns at once).
- Not taken: one frame in flight (`kFramesInFlight` = 1, the GPU idle while each frame's first part is recorded), the
  vsync-locked release (section 18).
- It needs kick lists: a list ends at a kick once it holds `kKickSubmitDraws` (200) draws, so a game drawing a few
  dozen a frame (Peggle 2) never holds back. N&B's GPU-bound frames have well over a thousand.
- PC (D3D12, 2026-10-20 check): the probe's "input -> GPU done" works there too (noted at the slot waits); the trim
  itself is deko3d only.

### At the frame cap: eager kicks (2026-10-20, package 36i, `banjo_deko3d_eager_kicks`, an A/B)

The console's 36g logs at 30 fps (the cap the limit, GPU 24-27 ms, render thread 14-19 ms): input -> swap 12.5 ms
(the game's frame), swap -> present 14 ms (the render thread replaying it), input -> GPU done 54.6 ms: the GPU finishes
about 28 ms after the present, a whole GPU frame. The three stages run one after the other: a kick list goes to the GPU
at the kick after the one it ended at (its upload memory cleaned in between), the game kicks 4-5 times a frame and
lists end at 200 draws, at most 3 a frame (about 2 a frame were submitted at kicks), so most of the frame reaches the
GPU at the present. Late input does nothing there (the frame cap's wait is already just before the controller read).

- `banjo_deko3d_eager_kicks` (on since 36i at the user's request; configs/banjo_deko3d_eager_kicks_off.toml the old way,
  _ab.toml = in-run A/B target 18; the cvar's own default stays off for other ports): lists
  end at a kick once they hold 100 draws, up to 6 a frame (VideoState::kKickLists 6; 3 with the key off), and each
  goes to the GPU at its own kick, its ranges cleaned first (the ring cleaner waited for, as the latency trim's first
  list). The GPU then works while the render thread replays; expected 8-14 ms off input -> GPU done at the cap, the
  render thread paying the cleaner waits. The same commands in the same order.
- Not done (larger): replaying the frame's draws as the game issues them rather than after its swap (most of the 14 ms
  middle stage); releasing frames on a vblank phase measured without the vsync thread (about 6 ms of display wait on
  average), console-only.

## 21. Precompiled shaders that stay precompiled (2026-10-20, package 36f)

Emulator runs compiled the same 4-7 shader variants at every start although the DKSH cache keeps what the console
compiles. Three causes, found with a new log line (plume_deko3d.cpp: "uam: run-time variant (stage S, N bytes):
constants" for every run-time compile; scratchpad match_rt.py turns them back into shader names and cache keys):
- The cache file and the pipeline list were flushed but not synced: fflush only hands the data to the file system
  service, which keeps it buffered, and a game closed with HOME (or stopped in the emulator) lost the run's records
  (Peggle 2 found it for the cache). The cache now fsyncs each new record (one per compile); the pipeline list
  (render/pipeline.cpp, now a FILE*) syncs at most once a second, and a second after its last record
  (`SyncPipelineListLocked`, the render thread at each present).
- Old list records (Renut's blend skip from before package 30) were built ahead with constant 900, which no draw uses
  any more: three variants compiled at boot for nothing. Loaded keys are now normalised to the configuration
  (blend skip, output trim, alpha function, bools, the A/B bits), so they deduplicate with the keys draws make.
- New combinations of the game's bool constants (banjo_gpu_bool_spec) appear as play reaches new states: those compile
  on the prebuild worker while the generic pipeline draws (no stall), and with the two fixes above they are compiled
  once, kept in the cache and listed for the next start (and for the next cache built from the console's list).
- 2026-10-20 (after package 36g): the "run-time variant" line names the shader too: "(stage S, N bytes, base M bytes
  HASH): defines" (the text without the inserted defines: its size and FNV-1a 64; only the inserted defines are
  listed). `tools/deko3d_variant_cache.py --runtime-log <renut_NNN.log>` matches each line to its GLSL file and adds the
  variant, so a console's logs alone complete the next cache (36g's first console run compiled 13 such variants, kept
  on the card; their lines predate the base hash).

## 22. The fur skinned once a frame (2026-10-20, package 36g, `banjo_deko3d_skin_once`, an exact A/B)

Package 35's console profile of the heaviest frames (55.9 ms): the fur family about 21.5 ms. The main view's 2F36 draws
cost about 560 us each (pixel work: 24 draws, 13.5 ms); the 96x96 dialogue portrait's about 93 us each, of which the
pixels are perhaps 30-50 us, so a fur draw's vertex work is plausibly 40-60 us: about 4 ms a frame over the fur's ~110
draws. Every fur part is drawn once a shell, shell by shell, and every draw re-skins the same vertices: 12 bone rows
loaded (12 128-bit loads since 36d) and blended, byte-order selects and all, before the shell offset uses them.

- tools/deko3d_glsl_opt.py pass `skinonce` (the vertex shaders of tools/skinonce_shaders.txt: B814D7237E615990,
  12399EE21A0440AE, 146DA1F10B7D746E, 7F73E48B8EA43715): the three skinned matrix rows (the last bone's
  `vec4 _R = mix(_P, (row * weights.wwww) + _P, _C)`) and their backward slice in main. With constant 913 = 1 the rows
  are loaded from a storage buffer at binding 13 (three vec4 a vertex at gl_VertexID * 3) where the draw's shared word
  v[84].z (SharedConstants::pad_1352[0]) is 2, and computed by the slice, which moves into the other branch, where it is
  0 or 1 (1 also stores them). The code after the rows is the same text. Without the constant (the key off) the
  shader is its old text, main and all (`#if DK_SKIN_ONCE`): the same DKSH bytes, checked for all four. The pass refuses a
  shader whose slice reads the float constants (the shell offset is there), textures, the instance or a shared word
  outside the key.
- uam (banjo flag 1024, extended): a vec4 load of storage buffer 13 is one 128-bit load, a vec4 store one 128-bit store
  (aligned: the renderer's regions start on 256 bytes). Read path of B814: 475 -> 231 instructions, 50 -> 30 GPRs, 3
  loads; write path 481 (the original and 3 stores). 12399: 409 -> 165; 146D: 495 -> 251; 7F73: 483 -> 241.
- render/draw.cpp `FurSkinLocked` (after BindStreams, deko3d with pushed constants only): a part's first draw of the
  frame writes (mode 1) into a region of the slot's skin buffer; a later draw reads (mode 2) only when everything the
  skinning reads is the same: the vertex shader, the bound vertex data (a hash of each stream's GPU address, size,
  stride and slot, taken in BindStreams: the upload ring never hands out a range twice in a frame), the index buffer
  view and draw range, the bone data by its guest bytes (each fetch copy's source, size and endian mode, and a
  memcmp of the bytes there now against a snapshot taken when the rows were written: every kick copies the bone data
  again to another offset of the upload ring, so the copy's GPU address differs shell to shell; emulator run 107 with
  the address as the key: 348 writes, 0 reads) and the vertex-fetch part of SharedConstants with the copies' offsets
  left out
  (kSkinShared: booleans v[32-33], fetch data, slots and size v[35-60], tessellation and bound fetches v[65-81],
  flt_max; the pass refuses a shader whose slice reads any other shared word), so the fur's other pixel shaders on the
  same mesh (2B31 shares B814) read the rows too. The mode
  word is pushed with the shared constants (one more 64-byte line). Before the first read after writes, a buffer barrier
  (DkBarrier_Primitives with the shader and image caches invalidated): one or two a frame. Not for tessellated,
  instanced or memexport draws. pipeline.cpp gives the pipelines constant 913 under the same condition (key on and not
  the A/B off half: PipelineKey.reserved bit 2, set only for the four vertex shaders).
- The periodic report: "[renut] skin once (banjo_deko3d_skin_once): W fur draws skinned and wrote their rows, R read
  them, P skinned as before, N without room". Expected: R about 11-20 times W.
- Exact: the stored rows are the bits the writer computed with the original arithmetic (the slice is unchanged text);
  a bone index inside the palette reads the same bytes in the writer and the reader (compared); an index past the
  palette (none in valid meshes) reads whatever follows the copy in the upload chunk, which already differs draw to
  draw without the key;
  a reader would have computed the same bits from the same inputs, which the key compares; the code after the rows is
  the same text in every variant. What is not checked by a tool: that uam compiles that shared code the same with
  the rows loaded as with the rows computed (no transformation in nv50_ir crosses the rows' selects; the difftest
  tools run pixel shaders only).
- Configs: `banjo_deko3d_skin_once_off.toml` (the old way; on by default since 36g at the user's request),
  `banjo_deko3d_skin_once_ab.toml` (in-run A/B target 17). Cache 53 has both variants of the four shaders.
- Measured: emulator run 108, 85,394 writes and 817,012 reads (12 a write: the shells), no fallback. Console, package
  36g against 35 on the scripted route (several packages' changes together): the dialogue scene 52.7-55.0 -> 42.4-43.9
  ms of GPU, the heaviest period 67.3 -> 55.8 ms; the 96x96 portrait's fur 93 -> 40-59 us a draw (vertex work).
- Tried and taken out: the 23 skinned vertex shaders of the depth-only draws (the shadow maps). Emulator run 109: in
  10 s 8,101 draws wrote rows, 1,685 read them and 5,563 found the 64-entry table full. A mesh is mostly drawn once a
  cascade with other inputs (bone data differed in most near misses), so the writes (stores, a longer shader) would
  mostly be paid for nothing; and their entries filled the frame's table, so the fur fell back to skinning every
  shell (448,685 fur draws "as before" over the run). With the fur alone the table never fills.

## 23. No Vulkan driver in the Switch build (2026-10-20, package 36m, the user's request)

The Switch NRO no longer links Mesa's NVK (libvulkan.a, expat, libdrm_nouveau): 88.9 MB -> 74.4 MB. deko3d is the only
renderer it can run. `REXGLUE_SWITCH_NVK` (sdk/cmake/rexglue_switch.cmake, default OFF) and `PLUME_SWITCH_NVK_LINK`
(thirdparty/plume, set from it in cmake/banjo_renut_gpu.cmake) put the driver back. Without it:

- `sdk/src/ui/switch_vulkan_stubs.c` answers the four Vulkan entry points still referenced (`vkGetInstanceProcAddr`,
  `vkDestroyInstance`, `vk_icdGetInstanceProcAddr`, `vk_icdNegotiateLoaderICDInterfaceVersion`) with "no driver" and
  defines `posix_memalign` (Mesa used to, for FFmpeg). The first call writes one line to stderr; the deko3d path makes
  none.
- `BANJO_SWITCH_NO_VULKAN` (banjo_renut_gpu): `banjo_renderer` is always "renut" and `renut_gpu_backend` always
  "deko3d"; a config asking for the native renderer or Vulkan gets a warning in the log and runs deko3d.
- plume's Switch `VulkanInterface` returns invalid instead of calling a NULL `vkEnumerateInstanceExtensionProperties`.
- The packages leave out the 59 configs that ask for a Vulkan renderer (the native renderer's, `banjo_renut_vulkan`)
  and renut_normal.toml, banjo_diagnostics.toml and banjo_fragments.toml (the native renderer's).
- Not taken out: the native renderer's code and ReXGlue's Xenos plugin are still in the NRO (Renut's hooks share
  addresses with the native ones; the plugin is linked whole-archive). Both are idle on deko3d.

## 24. GPU work of 2026-10-20 evening: what the console's data leaves, and the A/B pack (package 36q)

The console's 36m run, heaviest window (54.6 ms of GPU a frame, 18 fps): the fur family 18.8 ms (2F36 10.4 main view +
0.9 the portrait's box, C92B 3.9, D40E 1.9, B3C1 1.8), the lit materials (3A25, 0BBB, 2D82, 49A9...) most of the rest
(500-800 instructions, 26-34 texture fetches each), depth resolves 2.2 ms, colour resolves 1.9 ms, shadow maps 2.2 ms
(about 490 draws, vertex work), A5E2 1.2 ms. The compiled code of the hottest variants was read (tools/difftest
dump_variants.py, uam_ops.py, shtool): the shadow taps are two FMAs a coordinate with the rotation shared by the
cascades, one cascade runs per pixel (the game's fall-through), the early test's fetches are merged with the main
body's by uam (33 texture instructions where 37 are written), two selects with the same arms a shader. Nothing large
is left at the instruction level.

The barriers a gameplay frame (36m, per frame): about 30 primitives, 50 fragments and 15-18 full ones. The full ones:
about 10 copy-engine copies of exported (memexport) vertex data into the frame's fetch buffer (render/draw.cpp
UploadVertexFetchData, a full barrier before each group), 5 MSAA resolves on the 2D engine (the dialogue portrait's
4x box: disable_msaa covers only the scene's target) and 0-3 texture uploads.

Measured and not taken:
- **Read-only binds keeping a depth hand-over** (a draw that writes neither depth nor stencil leaving the resolve in the
  target's image, and sampling it while it only tests it): emulator run 123b, 0 such binds on the whole route. Every
  bind of the main depth after its resolve is a depth-writing draw, so the copy is needed; the main depth's resolves
  are followed by depth-writing draws, and the third (with the words, for the depth of field) by a clear in two halves.
- **Drawn resolves into cube faces** (a 2D view of the layer as the target): the cube (the environment map, six
  128x128 faces) is resolved about once a level, not a frame (6 image copies in the whole 36m run).
- **A compute copy for the exported fetch data**: on Maxwell a switch between graphics and compute work in one queue
  drains the pipeline like a full barrier.
- Not exact, the user's call: the portrait box without its 4x MSAA (5 2D-engine resolves and their full barriers a
  frame, and 4 samples a pixel of its fur).

**The A/B pack** (`banjo_renut_ab_targets`, banjo_renut_tuning.cpp): a list of in-run A/B targets; each in turn is
switched off and on for one `banjo_renut_ab_s` period each, and the 10 s report has a "[renut A/B] ... (target N,
banjo_renut_ab_targets)" line for each. `configs/banjo_gpu_ab_pack.toml` turns banjo_deko3d_light_branch and
banjo_deko3d_gpr32 on and rotates over 15 and 16: one scripted run measures both (their GPU ms on and off). The fur
family is on both lists, so cache 56 adds the variants with both constants (910 = 911 = 1; tools/deko3d_variant_cache.py).

## 25. banjo_late_input_gpu removed (2026-10-20, package 36s, the user's call)

Packages 36l-36r had a second late-input slack, `banjo_late_input_gpu` (with `banjo_late_input_gpu_margin_us`): how
long each frame's first GPU work waited for the GPU to finish the frame before, plus the delay its controller read
already had; the next reads were delayed by the least of 16 frames less the margin. Package 36s removes it, its keys,
its configs and in-run A/B target 19 (now "none"). Plain late input (`banjo_late_input`, the render thread's slack)
stays. The console's runs on the scripted route (36k/36r off, 36m/36q on), controller read -> GPU done:

| Scene | Off | On |
|---|---|---|
| 30 fps cap, GPU 24-28 ms a frame | 52-57 ms | 52-57 ms (delay 0-1.5 ms) |
| GPU-bound, GPU 42-45 ms | 99 ms, 22.9 fps | 85 ms, 22.0 fps |
| Heaviest, GPU 55-57 ms | 132 ms, 17.7 fps | 104 ms, 17.5 fps |
| t220 scene, GPU 33 ms (just holds 30) | 71-81 ms, 29.7-30.0 fps | 70 ms, 24.3 fps |

At the cap with spare GPU nothing waits in the GPU queue, so it measures no slack and cannot help; where 30 fps is just
reachable its slack (which includes the delay already applied, the waits seen late at the kicks) ratcheted the delay
up to 29 ms and the frames missed their slot. The gain was only where the frame rate was already far below 30. The
lag left at the cap is the work itself (game thread + recording + GPU) and the wait for the display's refresh; only a
read timed from the refresh could take the latter. sa2-nx, futari-nx, Peggle 2 and SASRT were told to remove it too.
