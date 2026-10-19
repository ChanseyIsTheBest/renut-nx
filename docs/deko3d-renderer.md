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
