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

## 26. SMAA 1x before the HUD (2026-10-21, package 36u, the user's request)

`banjo_renut_smaa` (on in renut.toml, renut_auto.toml and every config since 36u, the user's call; off in code) anti-aliases the 3D image with SMAA 1x
(Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez: SMAA.hlsl, AreaTex.h and
SearchTex.h from github.com/iryoku/smaa, MIT licence, in app/thirdparty/smaa with its LICENSE.txt). An alternative to
MSAA: the game's 2x MSAA stays off (disable_msaa), SMAA costs three full-screen passes and a copy at 1280x720.

**Where:** an emulator trace of a gameplay frame (banjo_renut_diag_resolve_trace + banjo_renut_diag_draw_log_s) shows
the 3D scene (1280x720 8-bit, ~730 draws, resolved in two halves), depth of field into a second 1280x720 target, the
bloom chain, then on that second target one full-screen composite (pixel shader 981A6A58F2ABFA42, opaque, colour mask
RGB) followed by the HUD (alpha-blended: minimap, icons, text) and the resolve to the front buffer. SMAA runs right
after the composite draw (render/draw.cpp, end of DrawStateLocked; `banjo_renut_smaa_after_ps` lists the composite
shaders), once a frame: the image is post-processed and tone-mapped (gamma space, what luma edge detection expects)
and no HUD is drawn yet. In the emulator run it ran in every cutscene and gameplay frame; the boot logos, title and
menus have no such composite and are left as they are.

**Passes** (render/smaa.cpp, shaders/host/smaa*.hlsl, the utility layout like DrawResolveCopy): the target copied into
`src` (resolve_copy_ps), luma edges of `src` into R8G8 `edges` (cleared first), blending weights into RGBA8 `weights`
(SMAA's medium preset; `banjo_renut_smaa_quality = 2` the high one with diagonal and corner detection), neighbourhood
blending of `src` written back into the target with the RGB write mask (its alpha kept). SMAA's point samples are
clamped texel loads, its other samples the linear clamp sampler; the bindless slots go in the push block as raw bits
(Ryujinx follows a handle only back to a constant buffer load). `banjo_renut_smaa_threshold` (0.1) is the luma
threshold. The passes reset the draw path's bound framebuffer, pipeline, layout and viewport; the target's `writes`
is bumped (resolve skip).

**Checks:** emulator run 137 (SMAA every frame of the intro and gameplay, pictures right) and run 138 with
`banjo_renut_diag_smaa_compare_s` (the same frame before and after SMAA, read back and written as TGA without stalls):
builds/smaa_comparison_2026-10-21 (6 scenes full size and 3x crops, 25 pairs in all/). Stair-steps on furniture,
fences, roofs and characters smoothed; textures keep their detail; about 9,000-57,000 pixels of 921,600 change a
frame. GPU cost on the console: configs/banjo_renut_smaa_ab.toml (in-run A/B target 19) measures it.

**Cheaper, same pixels (36v, the user's request after 36u's console run).** 36u's four full-screen passes cost 3.3-5.3
ms a frame at 460.8 MHz (GPU profile "SMAA"; the matching windows +4-5 ms; the t220 scene 30.0 -> 25.9 fps). Now:
- the copy folded into the edge pass (a second render target: the pixel the edge pass reads anyway): one full-screen
  pass and one barrier less;
- a depth mask (D16) written by the edge pass (SV_Depth): 0 on edge pixels, 0.5 on pixels without an edge of their own
  whose right or bottom luma step reaches the threshold (only those can take weights from a neighbour), 1 elsewhere.
  The weights pass (viewport depth 0.25, GREATER) shades only the edge pixels into a target cleared to 0, which is
  what the reference writes everywhere else; the blend pass (0.75, GREATER) only the pixels it can change, the others
  keeping the composite's value, which the blend would write unchanged. Both `[earlydepthstencil]`;
- the edge detection without the reference's discard (0 written where it would discard), its point samples through
  the point clamp sampler (no clamping arithmetic);
- one barrier before each of the three passes (plume's deko3d back end merges a point's transitions).
`banjo_renut_diag_smaa_compare_s` now also runs the full-screen passes in its frames: emulator run 139, 23 frames
(4,587-110,974 pixels changed by SMAA each): 0 pixels differ between the masked and the full-screen results.


**36w: only the masked pixels write.** 36v's console run: SMAA 2.47 ms a frame on average (36u 3.77; the composite's own
0.80 ms the same in all three runs, so the label holds SMAA's own work). The edge pass still wrote edges, a copy and the
mask for all 921,600 pixels (about 7.4 MB and three render-target writes a pixel). Now:
- the edge and mask targets are cleared (fast clears: plume's deko3d colour targets are compressed, the depth targets
  with depth compression on) and the edge pass discards where the reference does, also keeping the pixels only the
  blend can change: only the masked pixels (about a sixth in gameplay) write edges and mask;
- no copy in the edge pass: the blend pass reads the game's target (untouched until the end) and writes 'src' on the
  masked pixels; a fourth pass copies those pixels back into the target (red, green, blue), masked the same way;
- the mask the other way round (cleared to 0, edge pixels 1, the blend's other pixels 0.5) so the masked passes use
  the test LESS (weights at 0.75, blend and copy at 0.25), the direction the coarse depth culling (zcull) works in,
  rejecting whole tiles left as cleared;
- a GPU profile label per pass: "SMAA edges", "SMAA weights", "SMAA blend", "SMAA copy".
Not done (looked at): quad-shared lumas (QuadReadAcrossX/Y would cut the edge pass's 5 fetches to about 3, but uam has
no subgroup quad operations); half-precision arithmetic, skipping frames or the HUD's area (not the same pixels); a
compute edge pass (no early depth test for the passes after it); gathers (no fewer fetches for SMAA's cross pattern).

**36x.** The comparison frames now run SMAA's own shaders end to end (SMAALumaEdgeDetectionPS with its discard into a
cleared target, the full-screen weights and blending: shaders/host/smaa_edge_ref_ps.hlsl) and compare the target after
the optimised passes with that: emulator run 142, 24 frames, 0 pixels differ (36w's passes). Tried and dropped: the edge
pass's lumas shared in each 2x2 quad through fine derivatives (ddx_fine/ddy_fine are the very subtraction SMAA takes
the absolute value of; 3 samples a pixel instead of 5): emulator run 141 differed from SMAA in 6,000-72,000 pixels a
frame and found fewer edges, so the derivatives are not that subtraction there (Ryujinx's quad emulation or the
hardware's); not shippable without proof on the console.
Also in 36x, the user's call (NOT exact): `banjo_renut_fur_shells_percent` (default 100; 75 in the tomls): that share
of each fur part's shells, spread evenly (draw n when ceil((n+1)p/100) > ceil(np/100)), the first always.

**36y (console data of 36w).** 36w: SMAA 3.12 ms a frame on average (36v 2.79): edges 1.46, weights 0.88, blend 0.36,
copy 0.27. The edge pass costs the same with every pixel writing (36v) or a sixth (36w): it is bound by its five samples
a pixel, not its writes, and the copy pass was a loss. 36y = 36v's three passes (the copy as the edge pass's second
target, the mask written for every pixel, no clears but the weights') with 36w's LESS mask (cleared nowhere now: every
pixel written) and the labels "SMAA edges", "SMAA weights", "SMAA blend". The weights pass (0.3-1.8 ms, by the scene's
edges: grass) and the edge pass are what is left; the exact means for those are spent.
Also 36y (the user's call, NOT exact): `banjo_renut_portrait_fur_shells_percent` (50 in the tomls: 11 of 21 shells in
targets of 128 x 128 or less) and `banjo_renut_portrait_msaa_samples` (2 in the tomls, the user's choice over 1: the
game's small MSAA targets, the dialogue portrait's 96 x 96 4x box, made 2x in BuildTarget, colour and depth alike;
pipelines take the target's sample count; 1 makes them single-sampled: resolves become copies, alpha to coverage
the alpha test as on the scene).

## 27. Internal resolution: the 3D image at 648p / 900p, the HUD at the display's (2026-10-21, package 36z, the user's request)

NOT exact (the user's call: a smart cut). `banjo_renut_render_height_handheld` (648 in the tomls, 720 in code) and
`banjo_renut_render_height_docked` (900) set the 3D image's height; `banjo_renut_display_height_docked` (1080 in the
tomls, 720 in code) the display's when docked. The HUD, menus and text are drawn at the display's resolution (1280x720
handheld, 1920x1080 docked). configs/banjo_renut_native_720p.toml is the game's own 720p everywhere. The upscale is
bilinear in every config (the user's choice: `banjo_renut_upscale_filter = 0`, also the code's default); 1 is bicubic.

**Where the split is.** The emulator trace of section 26: the 3D scene into BBB103F0 (1280x720, depth BBB104F0), the
depth of field into BBB10470 with that depth bound, the bloom chain (1x buffers), the final composite (981A6A58F2ABFA42)
into BBB10470, the HUD into BBB10470 (the depth still bound; draw logs of runs 64 and 150: every HUD draw has
RB_DEPTHCONTROL 00708734, 00700040 or 00700060, no Z or stencil test), the resolve of BBB10470 to the front buffer. So:
- every target is drawn at the internal resolution (`ResolutionScale`, latched at the Swap; shadow maps and the
  post-processing buffers keep the console's sizes, graphics_options.cpp);
- right after the composite and SMAA (render/draw.cpp, the same hook), render/ui_scale.cpp draws the composite's target
  into its UI target at the display's resolution (`UiSurfaceLocked`: same format and samples, not any EDRAM view's
  target) with shaders/host/ui_upscale_ps.hlsl: one bilinear sample a pixel and nothing else (no branch, uniform
  bindless indices, no clear, no blending, the whole write mask: a partial one would read the target back; one
  barrier for both images), so it costs a read of the image and a write of it; ui_upscale_bicubic_ps.hlsl is
  Catmull-Rom in the usual five bilinear taps (`banjo_renut_upscale_filter` 1). GPU profile label "UI upscale";
- from then to the Swap every lookup of the composite's target answers the UI target (`RedirectToUiSurfaceLocked`,
  GetHostSurfaceLocked; the surface epoch bumped, so the per-draw memos look again): the HUD's draws and clears and the
  resolve to the front buffer, which is then at the display's size and the present shows it as it is;
- the HUD's framebuffer leaves the scene's depth target out when it is smaller than the colour target and the draw
  neither tests nor writes depth or stencil (`DropSmallerDepth`: a deko3d framebuffer is as large as its smallest
  attachment; a Z test with ZFUNC ALWAYS and no writes counts as none); the pipeline key keeps the draw's depth words
  (`Targets::depth_dropped`), so its shader variants (constant 909) and the DKSH cache entries stay the same;
- the menus draw 3D icons after the composite (the single-player menu's item bar: Banjo heads, consoles, controller;
  pixel shader B2E98780B07392BC, RB_DEPTHCONTROL 00708766: GEQUAL with writes; emulator run 146: up to 918 such draws
  in 10 s). The first draw of a frame that tests depth after the redirect fills a UI depth target at the display's
  resolution from the scene's (`UiDepthAfterCompositeLocked`, shaders/host/ui_depth_upscale_ps.hlsl: nearest texel,
  values copied as they are, the stencil cleared to 0; label "UI depth upscale") and the depth target's lookups are
  redirected there too until the Swap. Gameplay frames never make it (their HUD tests no depth). A draw that still
  keeps a smaller depth target is counted and logged ("A draw tests depth or stencil against a depth target smaller
  ..." with its shaders); the [renut] internal resolution line counts both every 10 s;
- clears of colour and depth targets of different scales are made one at a time, each at its own scale;
- frames without the composite for 30 frames in a row (menus, loading screens, and the boot) are drawn at the display's
  resolution whole: no split there, the menus' text stays sharp. The first gameplay frame after a menu is drawn at the
  display's resolution too; the targets are made again at each switch (a few MB, once).

**Shaders.** The GLSL package was made with tools/deko3d_glsl_opt.py's passes scale1 (every texture's scale code folded
to 0) and fragscale (the pixel position not divided by the render scale): both assume 1x. The default pass list now
leaves them out and starts with a new pass, scalecodes: the scale-code chain shader_common.h's renutTextureScale
compiles to (`code == 4u ? 3.0`, `code == 6u ? 6.0`) gets this renderer's factors for codes 4 and 6, 9/10 and 5/4
(render_scale.h has the same on deko3d; 4K and AO at 6x, which those codes were, are never drawn on the Switch; the
PC's back ends keep Renut's table and shaders). 2271 of 4881 shaders change (2209 pixel shaders: `gl_FragCoord.xy /
resolution_scale` instead of `/ 1.0`; the 168 with a scale-code read), all checked through uam; DKSH cache 57 has their
variants (cache 56's for the rest).

**Docked.** From sa2-nx 1.0.3.1 (credited in the code): plume's `Deko3DSetSwapChainSize` makes the swapchain
1920x1080 when docked (the window gets that size with nwindowSetDimensions once the old swapchain has let go of its
buffers); `ConsoleDocked` is libnx's operation mode with Reverse-NX obeyed (rex::ui::switch_saltynx::BaseMode, as the
SDK's window). Docking and undocking take effect at the next frame.

**36z2 (console log of 36z; the user's requests: 864p docked, any height in the config).**
- Docked was 41.3 ms of GPU a frame (24 fps) in the scene that ran at 30 fps handheld (33.0 ms): the GPU runs at 768
  MHz docked but the memory at 1600 MHz in both modes, so the memory-bound passes grow with their pixels (colour
  resolves 1.74x, depth resolves 1.56x, SMAA 1.39x, present 2.05x, UI upscale 2.16x) while the scene's draws stay
  level (0.98x); and Renut's extra host mip level for every resolve destination above 1x ("resolve host mip levels")
  cost 3.98 ms, which handheld (0.9x) never has. `banjo_renut_host_mips` (on in code, Renut's) switches it: off in
  renut.toml, renut_auto.toml and every config but configs/banjo_renut_host_mips_on.toml (the user's call; NOT exact
  above 1x: level 0 only, as at 1x).
- Any height: render_scale.h's codes 4 (handheld) and 6 (docked) are set once from the keys (height / 720 reduced;
  360 to 1080, rounded to a multiple of 9 so 1280x720, 640x360 and 320x180 are whole host pixels); the shaders read the
  same heights from SharedConstants pad_1352[1] (v[84].w: handheld | docked << 16), the scalecodes pass's chain constants
  now `float(v[84].w & 0xFFFF) / 720` and `float(v[84].w >> 16) / 720`. 720 is code 0, 1080 code 2 (no upscale when
  docked at 1080). Docked default 864 (1536x864, 1.2x).
- The docked white line: the half-pixel offset (`banjo_renut_half_pixel_offset`, section 12) was half a guest pixel,
  scaled with the viewport: at 1.25x and 1.5x the game's full-screen quads (edges on the guest's pixel centres) began
  0.625 / 0.75 host pixels in, past the centres of the top row and left column, which kept stale values (white in the
  emulator's docked frames: rows 236,236,236, columns 221,221,221). Now half a host pixel (0.5 / scale guest pixels):
  the same at 1x, 0.05 host pixels more at 0.9x.
- A 128x128 colour target (a post-processing size: 1x) had its 128x128 depth target at the internal scale (0.9x:
  115x115): ten draws a frame were cut. Depth targets of a quarter of the screen or less are 1x now too.

## 28. No draw waits for a shader compile (2026-10-21, package 36z3, `banjo_deko3d_async_variants`, the user's request)

**The freeze (console log of 36z2, renut_003 and banjo_profiler stall #15).** Loading the next area at 17:56:41, the
render thread built a pipeline (DrawStateLocked -> GetPipelineLocked -> GetPipelineSlow, twice: Renut's generic pipeline
of a bool-specialised key) whose variant was not in the DKSH cache and waited in plume's loadVariant for the uam thread,
which spent the stall watch's whole 9 s (1,925 ms of CPU in each 2 s) and the two minutes after it in Mesa's GLSL
optimisation loop (do_common_optimization -> do_tree_grafting). The game froze until it was closed. That session had 375
run-time compiles (avg 376 ms each: a stutter each) in areas past the scripted route. The 983 variants the console's
pipeline list adds all compiled on the PC in a minute (none hung): the one that hung never finished its pipeline, so no
list has it. Cache 59 has the console's own cache (every variant it compiled) and its list's variants.

**The change (plume deko3d, Deko3DOptions::asyncVariants).** A variant neither loaded nor in the cache is queued for the
uam thread in the background (once per variant key; draws' synchronous compiles go ahead of those), and loadVariant
returns the shader's generic variant: the request without the constants that only fold what the shader reads at run
time when they keep their defaults (genericDrops: the game's bools 1000-1255, blend skip 900, output trim 901, alpha
function 903, tessellation kind 904, vertex outputs 905, zero inputs 906, early depth 909). The configuration's
constants stay (907, 908, 910-913: skin-once, 913, writes rows that other draws read). The same pixels: each dropped
constant's default reads the value at run time or does work whose result the pipeline's state then discards. The
pipeline records the stages that wait (provisional); when a background compile finishes (the uam thread writes it to the
cache file, loads it and bumps the async epoch), the next draw that takes the pipeline (render/draw.cpp, one atomic load
otherwise) gives it the stages whose own variants are done (Deko3DRefreshPipeline: each stage pointer stored at once; any
mix of generic and own stages draws the same pixels) and binds it again. A variant that fails keeps the generic one.
Renut's own generic pipelines for bool- and alpha-specialised keys (render/pipeline.cpp) stay as they were: this covers
what they did not (the rest of the key's constants, and every key that is not bool-specialised).

**The generic variants of every shader** are in the cache: tools/deko3d_variant_cache.py --generic adds each GLSL file's
generic variant (no key-state constant) with the configuration's A/B variants of it (config_specs: 907, 908, 910, 911,
913), as for the pipelines. With them, no draw anywhere in the game waits for uam. The report line counts it: "uam N
compiles (... ; X stages given the generic variant meanwhile, Y compiled in the background, Z swapped in)".

**Checks.** Emulator run 152 (cache 60, the scripted route): 0 compiles, 0 generic stages (the cache has every variant).
The stress test runs with a cache of the generic variants only (deko3d_variant_cache.py --generic --merge 60 --prune:
4,717), so every specialised variant takes the background path. Run 153 aborted in finishAsync: the job kept the
Deko3DShader object it was made for, and Renut's prebuild workers destroy theirs when a batch is done (a freed mutex:
std::system_error, terminate). The jobs and the pipelines now keep only the variant's key; the finished variant lives in
the device's `variants` (never freed), where the refresh finds it.
Run 154 (the stress test again with the fix): the whole route (7 minutes), 204 variants compiled in the background (avg
1.6 s each under the emulator, 0 failed), 14,222 stage lookups given the generic variant meanwhile, 388 stages swapped
to their own; every compile a background one (no draw waited on uam); the pictures right throughout. Its one long gap (a
frame in 38.9 s) was a GPU fence: Ryujinx translating the hundreds of DKSH binaries it had not seen (the console runs
them as they are). Run 155 (cache 60): 0 compiles, 0 generic stages.

**36z4 (console run of 36z3 with the generic-only cache: a crash).** The heap ran out loading an area (2,980 of 3,098 MB
in use, 83 MB free in 200,000 pieces; 36z2's sessions held 2.2-2.4 GB). The pipeline list's prebuild asked for nearly
every specialised variant at the start (8,458 generic stage lookups in 41 s) while the console compiled about one a
second (264 in 5.5 minutes), and each queued job held a copy of its variant's text (~50 KB): hundreds of MB. Also seen:
the render thread at 50-70 ms a frame while the uam thread compiled without pause (newlib's one malloc lock: the 36z2
freeze's stack had a thread waiting on it while uam ran). Now:
- a shader's GLSL is shared (Deko3DShader::glsl, a shared_ptr): a job and a waiting pipeline stage hold a
  Deko3DVariantRequest (the key, the shared text, the defines: a few hundred bytes); the uam thread makes the text when it
  takes the job, one at a time; the text outlives a destroyed shader object while a request holds it;
- a draw's request goes ahead of the background threads' ones (Deko3DSetBackgroundThread: Renut's prebuild workers), and
  at most 256 background requests wait; one that found no room is queued by the first draw that refreshes its pipeline;
- (2026-10-31) a prebuild worker building a specialised pipeline a draw asked for (Renut's `Job::for_draw`; the draw
  uses the generic pipeline meanwhile) queues that pipeline's variants as a draw's (Deko3DSetDrawVariants), and the job
  goes to the front of the prebuild queue. The first water fetch-spec run in the emulator had the specialised water
  vertex shader queued behind the list's background compiles and their rests for 6 minutes.
With cache 60 none of this happens in play (it has every variant the lists name, and the generic ones of every shader).

## 29. Host textures released when the heap runs short (2026-10-22, package 36z7)

The 36z3 and 36z6 console runs crashed loading the town (`std::bad_alloc` in Renut's command recording, the heap at
2,903 of 3,090 MB, 744 MB of host textures). Renut's two texture rules (its backstop: idle 600 frames at 85 % of the
budget; banjo-nx's `banjo_renut_memory_policy`: cold 120 frames with less than `banjo_renut_textures_reserve_mb` of
budget left) both read `QueryGpuMemory`'s budget, and on deko3d that was 0 (`backend.cpp`): both returned at once and no
host texture was ever released on the Switch.

- `QueryGpuMemory` on deko3d (Switch): budget = what deko3d holds + the heap's room. plume takes deko3d's memory from the
  malloc heap (`memalign`), the heap the guest's memory and everything else share, so "budget left" is that room: what
  `sbrk` has not handed out (`fake_heap_end - sbrk(0)`, O(1)). The free pieces inside the taken part (`mallinfo`, a walk
  of every piece: 140,000 of them in the crash) are left out, so it errs on releasing earlier.
- `render/textures.cpp`: with less than half the reserve left, a texture is cold after 8 frames (still more than the 2
  in flight) and the check runs every second of wall time as well as every 32 frames (the town loaded at 0.5-4 fps:
  32 frames were up to a minute). Never a texture only the GPU wrote (`banjo_renut_keep_gpu_written`).
- `renut.toml`: `banjo_renut_textures_reserve_mb = 512` (code default 192: normal play uses about 2.4 GB of the 3.1 GB,
  so this only acts in heavy areas).

Emulator: with a reserve of 16 GB (always under pressure) the route ran at 26-30 fps, textures released every second
and made again when drawn; with 512 the budget reads 913 of 1,605 MB there. Still in the heap and not addressed: the
12 or so game threads' 16 MB host stacks (`xthread.cpp`), about 200 MB.

## 30. The distribution build and the web installer (2026-10-22, the user's request)

Shaders made from the game's files are game data, so a release NRO cannot carry them. The NRO now also reads them from
**`deko3d_glsl.bin` next to itself**, which banjo-nx-installer (a web page beside this repository, like sa2-nx's and
bejeweled3-nx's installers) makes from the player's own copy, together with the DKSH cache `deko3d_shaders.bin`.

- **Version 2 of the GLSL package** (`banjo_renut_deko3d.cpp`, OpenInstalled): version 1 without a dictionary, each
  shader's GLSL one zstd frame of raw blocks, plus the numbers of the shader table (`shader_cache.h`: masks, vertex
  inputs, literals) of the shaders it holds, and a 16-byte tag (`kInstalledTag`, "renut7-bnxglsl1") naming the
  translator and GLSL chain it was made with. A package with another tag, or one that is not sound, is logged and not
  used. The table is read at start; a shader's GLSL is read from the card when the shader is made (all of them would
  be 175 MB). Format: banjo-nx-installer's `lib/package.js`.
- **Look-ups** go to the installed package first, then the NRO's own (CreateGameShader, BoolSpecMask). Renut's
  `FindShaderEntry` searches the NRO's table merged with the package's (`shader_library.cpp`, Table): the package's
  entries, then the build's own for the shaders the package lacks; the vertex inputs and literals an entry's offsets
  index are `g_activeShaderVertexInputs` / `g_activeShaderLiterals` (draw.cpp, hooks/shader.cpp).
- **Distribution build**: the NRO's own table and GLSL hold only the shaders the game makes at runtime (150,
  `app/out/pc-me/shader_capture`), which no installer finds in the game's files:

      python tools/renut_switch_shader_cache.py --only app/out/pc-me/shader_capture --out-dir app/out/renut_switch_dist
      python tools/deko3d_shader_package.py --glsl out/deko3d_shaders/glsl_opt_any --only app/out/pc-me/shader_capture --raw --out app/out/renut_switch_dist

  then configure a Switch build with `BANJO_RENUT_SHADER_CACHE_CPP` and `BANJO_DEKO3D_GLSL_CPP` pointing at
  `app/out/renut_switch_dist/`. `--raw` packages without a dictionary in raw zstd blocks (1.2 MB instead of 125 KB),
  so the installer's page reads those 150 shaders out of the NRO (`lib/nro.js`) and compiles their variants too. No
  SPIR-V is embedded (the Switch build has only the deko3d back end).
- A normal (full) build reads an installed package the same way, so the installer's output also works with it.

The installer (banjo-nx-installer/README.md) runs this repository's own tools: reNut's `dump_shaders.py`,
`renut_early_alpha.py`, `deko3d_glsl.py` and `deko3d_glsl_opt.py` unchanged in Pyodide, the translator and uam built
for WebAssembly from `app/thirdparty`. Its DXC is v2025.1 (the native build's is 1.8.2407): about a quarter of the
shaders get different temporaries in their GLSL, which compile and behave the same.

## 31. Render-thread round for the city (2026-10-23, the user's request: 75 -> 33 ms, pixel-identical)

The console's city (handheld, CPU 1020 / GPU 460.8 / RAM 1600 MHz): 13.4 fps, render thread 73-80 ms a frame at 97 % of
core 1, GPU 45 ms, about 5,000 draws a frame (46 % depth-only), so ~15 us a draw. The profile's hottest lines were
loads into memo tables, guest headers and caches: the frame's records do not stay in the A57's 2 MB L2. Three batches,
all exact (the same commands in the same order):

- Shared constants: the changed 64-byte lines as one mask (line_compare.h `Lines64DifferMask`, no branch per line;
  `banjo_renut_inline_line_compare` has no effect now; it was SLOWER ON THE CONSOLE in 36q). Literal registers pushed as runs.
- Memos: stream and index memos of 16-byte slots (hash and node; the key compared in the node) with 16,384 each;
  texture extents 2,048; texture slots 2,048 entries of two whole lines; fetch copies 1,024; pipeline memo ways with a
  separate line of key-hash tags. Per-shader answers (float constant groups, exports, bound fetches) kept in the
  `ShaderRecord` (`DrawSummary`, `DrawSummaryOf`) instead of three memos keyed by the cache entry; the record's per-draw
  fields grouped in its first lines.
- Prefetching (no effect on results): at a draw's start its textures' headers and memo entries, extents, vertex and
  index buffers' headers and summaries; after its index buffer header the index memo slot, then its entry and the
  range's store cards; after PrepareGeometry the stream memo slots, then entries and cards; in FillTextures the host
  textures a memo hit transitions; the pipeline object before BindTargets; and a draw ahead, in the replay, the next
  draw's shader lookups and the guest headers it changed (`PeekNextWord`).
- Smaller: `PagesClean` sixteen pages at a time with NEON where no card is written; the bound streams' hash only for
  FurSkinLocked; render-thread counters as a load and a store (no exclusive-monitor loops), the replay's draw count once
  a buffer; the chain-reuse counters ("[renut] chain reuse" line, only with `banjo_renut_chain_reuse` on).

Emulator note: Ryujinx runs the replay under a JIT on x86 caches, so it shows the instruction savings and checks the
results (store-card and pushed-constant verify modes) but not the cache effects; the console is the measure.

Console result (batch 3 against the release NRO, same city route): render thread 66.7 -> 64.0 ms a frame (13.35 ->
12.85 us a draw), GPU 45 ms, both about 12.9 fps. The frame (about 77 ms) is held by the CPU as a whole: the process
used 316 % of its 3 cores, a third of core 2 going to the uam thread (next section).

## 32. Background compiles off the game's CPU (2026-10-23, the user's report: compiling at the title and standing still)

- The cause of the endless compiles: the pipeline list asks for variants the cache never had (the lit shaders' bool
  combinations, 1129/1130/1243-1254, alpha function 5 or 8). The console compiled about 120 a session at about 1 s
  each and never caught up; the list needed 14,634 stage variants and the cache had 12,797. Built on the PC instead:
  the SD's deko3d_glsl.bin unpacked over glsl_opt (the keys need the installed text: 2,989 of 4,731 differ), then
  `tools/deko3d_variant_cache.py --glsl <unpacked> --list <SD pipeline_list.bin> --merge <SD deko3d_shaders.bin>`
  (1,837 variants in about 2 minutes, 0 failed; the 128 run-time variants the logs name all gave the same keys).
- The uam thread's priority: `banjo_deko3d_compile_priority` 0x3E was refused (an application's lowest priority is
  0x3B, the game threads' own) and the log printed the asked value; the profile showed the thread at 0x3B in every
  report, so Horizon's round robin at 0x3B gave it half of core 2 (53 % in the city). plume now reads the allowed mask
  (svcGetInfo InfoType_PriorityMask), takes the asked priority or the lowest allowed above it, and logs what the kernel
  gave ("uam thread: core 2, priority 0x3B (0x3E asked, ...)"); lending compares with that priority.
- `banjo_deko3d_background_compile_share` (default 25): after each background compile the uam thread rests in
  proportion (at most 8 s), so background compiles use at most that share of the core; a compile a thread waits for
  ends the rest (it is queued ahead of the background ones). Same pixels: the generic variant draws meanwhile.
- The deko3d report line now gives the compiles a thread waited for and the time ("N waited for: X ms in all") and
  the rests: a draw that stops the game for a compile shows there.
- Blocking and non-blocking compiles told apart (the user's request): a compile a drawing thread waits for (plume
  `compile()` from a thread without Deko3DSetBackgroundThread: the game stops), one the pipeline prebuild waits for
  (nothing on screen waits), and background ones (queued, the generic variant drawn meanwhile). The log line names
  the kind ahead of "run-time variant" ("BLOCKING (a draw waits)", "blocking (prebuild waits)", "background"); every
  blocking-draw compile is logged (up to 512), the others the first 64. The report line: "blocking: N a draw waited
  for (X ms in all, boost mode B times for Y ms), M the prebuild waited for".
- `banjo_deko3d_compile_wait_boost` (default on, the user's request): only for a compile a draw waits for, boost mode
  (`appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad)`: CPU 1785 MHz, the GPU at 76.8 MHz, which has nothing new while
  the frame waits), counted by waiters and back to normal the moment the last one is done; a refusal (applet mode) is
  logged once and not tried again; never left on at shutdown.

## 33. Audio by hand, stream cache pressure, lighter render and recording threads (2026-10-23, batch 4)

The console's city run with the cache-61 package (15.0 fps, render thread 63 ms of a 66.8 ms frame, 301 % of 300 % CPU)
had the render thread at 96 % of core 1 and cores 0 and 2 full (game 139 %, audio 43 %, the uam thread 19 %): frame time
follows total CPU, so audio savings count like render-thread ones. The user asked for the audio to be hand-optimised
and the render thread made lighter; the stream cache fix of section 32's run is here too.

Audio (bit-identical, each with a verify path):
- XAudio's per-voice state-variable filter, sub_82B0DCB8 (a fifth of the audio thread; one load in it, of the `low` value
  stored the sample before through an integer store, had 331 of the thread's samples): hand-written native
  (app/src/cpu/hot/banjo_hand_natives.h, namespace audio_filter, `banjo_hand_audio_filter`), the state in registers,
  four channels at a time in two NEON double chains, the same double multiply-adds rounded to float (the sign of an
  exact zero included). Hooked in front of the generated copy (hot/hand.txt; the copy's header edited by hand the same
  way the tool writes it, because today's tool output also changed unrelated copies); the guard compares it with the
  original (`banjo_cpu_verify`).
- XAudio's mono 16-bit linear sample-rate converter, SRC::LinearVector::ProcessBlock<short,1> (11 % of the thread):
  native (app/src/cpu/banjo_xaudio_native.cpp, `banjo_native_xaudio_src`, `_verify`): the same NEON operations SIMDe
  gives the recompiled code, the same flush modes, without the emulated lvlx gathers; the scalar prologue and epilogue
  are the original's statements (u64tod included). Declined: a destination not 16-aligned, an odd source address, a
  count of 0 or less, a channel count of 0 (the original traps). -ffp-contract=off (whole target and LTO link) keeps
  every multiply and add separate on both sides.
- Looked at and left: the router (CRouterEffect::Process, a tight loop already), the XMA decoder's codec reopen per
  sound (VLC tables: a small share of a small thread).

Stream cache (`banjo_renut_stream_cache_pressure`, default on): copies refused at banjo_renut_stream_cache_mb are
counted; with any since the last 60-frame pass, that pass also drops copies no draw read for 60 frames. The report
line: "N copies refused at the cap, X MB dropped for room". Same pixels.

Render thread:
- draw.cpp and command_buffers.cpp built with -fno-threadsafe-statics (cmake/banjo_renut_gpu.cmake): their 26-odd
  cvar statics and StoreCards / PhysicalOf were a load-acquire each at every call (the profile's atomic_base.h:518
  lines). Only the render thread runs them; the content verifier reaches only pointer statics every thread sets alike.
- PrefetchNextHeaders (5 % of the thread in the console profile): only the tail chunks the next op changed that hold
  header words (a constexpr table), and in them only words whose value differs from this draw's.
- ApplyState: constant groups copied 64 bytes at a time (inline) instead of a C library call per run.

Recording threads (NewOp, about 10 % of a core over two threads): ChangedChunks copies a changed whole chunk with a
fixed-size copy; CopyState counts the op's bytes, grows the arena once and copies chunks and groups with fixed-size
copies (it made a vector insert per chunk and per run).

## 34. Batch 5: stutters on the card, 30 CPU reductions (2026-10-23, the user's request)

Console facts that set it (batch 4, stock and overclocked runs): the area after the city went from 5-10 fps to 17-18.5
(stream cache pressure); the hand filter's first form (NEON double lanes) was slower than the original (vector float
/ double converts ~8 cycles on the A57, a four-lane chain for calls that filter one or two channels); the native
resampler took the converter from ~10.5 % to 3.7 % of the audio thread. The overclocked run's stutters (0.1-1.5 s) were
the render thread reading the card: each new shader's GLSL from deko3d_glsl.bin (fseek / fread under the package lock,
which the pipeline prebuild held for its own reads), each new variant from deko3d_shaders.bin, and textures the 512 MB
reserve released every 32 frames although 486 MB were free.

Stutters:
- deko3d_glsl.bin with a dictionary: tools/deko3d_glsl_compress.py makes it from the installer's raw package (161 MB of
  GLSL -> 15 MB, dictionary 128 KB, zstd -19; 190 sampled shaders unpack byte for byte, the info section copied); the
  NRO (OpenInstalled) reads a package with a dictionary whole into memory and unpacks each shader there. Raw packages are
  read as before. The installer should make the compressed form (a zstd encoder in the page) in a later release.
- `banjo_deko3d_async_cache_reads` (default on): a variant the cache file holds is read on the uam thread, ahead of the
  compiles and with no rest after it, while the draw uses the generic variant (plume asyncCacheReads, cache index
  through SetDeko3DShaderCacheContains). Same pixels.
- renut.toml: banjo_renut_textures_reserve_mb 384 (512), banjo_renut_stream_cache_mb 192 (128).

Audio: the voice filter as scalar chains, a channel each, interleaved (the subtraction in float, the same bits); the
static VLC tables of FFmpeg's WMA Pro / XMA decoder built once (ff_thread_once in wmaprodec.c: they were rebuilt at every
decoder open, i.e. every new sound, a twentieth of the XMA thread, and rewritten while other decoders read them); copies
(localized.txt) of the router, XACT sound events, the engine frame, the stereo converter and its outer loop, the scalar
converter, routed voice effects, both voice-state getters, the two XMA sync functions.

The localize tool: the store-card form of vector stores (REX_CARD(ea), store) accepted (index format 5); every hot copy
regenerated from today's codegen (39 hooks, 359 bulk). Also mapped: tools/inline_leaves.py's REX_INL(sub_X) calls (each generated file's static copy of a tiny leaf, invisible to the shard files) back to sub_X (index format 7); three bulk functions the tool had refused for a switch-body form in a callee now copy too. tools/switch_hot_bulk.py classes the game's named functions
(rex_*) as host code, so none had been listed: the named ones were added by hand from the console profiles.

Render thread: DrawNeedsExportFlush keeps each texture fetch slot's answer while its words and the pending exports
(g_export_generation) are the same; ExpandLoop's identity swizzle stores the decoded slots directly; FillTextures goes
through the set slots only and converts each fetch constant once; Replay uses the op's call unless it has UP data;
ConvertExportedStreamsLocked walks used_mask; pipeline key compares inline (NEON) instead of memcmp calls; the stream and
index caches test their cap before hashing (no hash for a copy that cannot be kept); the pressure pass's idle time 150
frames (60 was 2 s at 30 fps). Recording threads: ChunkDiffers in NEON.

Batch 5's first emulator run (guards every 64th call) found two faults, fixed before the package:
- three bulk copies (sub_822340D0, sub_82234168, sub_822341E8) wrote a wrong back chain (0x40... for 0x70...): the
  codegen's r1 mirror (tools/r1_local.py: r1l, REX_R1_SYNC / KEEP / RELOAD / ASSUMED) was outside the localize tool's
  register model. The tool now reads the text as before that pass (r1l -> ctx.r1, the macros dropped; index format 8),
  which gives the copies batch 4 verified.
- the render thread stopped at the title: an async cache read handed the draw the shader's generic variant, which was not
  in the cache, so it compiled on the render thread (a 64 KB lit shader, minutes in the emulator). The background read is
  now taken only when the generic variant is at hand (loadVariant noCompile); otherwise the variant is read at once.
- second run: one guard difference left, sub_82226330 (bulk), its old one: the codegen's carded unaligned vector stores
  (REX_STVLX_C / REX_STVRX_C) were missed by the tool's logged form again (the guard only: the copy stores the same as
  the original). VEC_LR takes the _C forms. The resampler's verify showed 49 of 544,000 calls differing in state words
  +16 and +40, which ProcessBlock does not write (another audio thread changed them between the two runs); the outputs
  matched.


## 35. Batch 6: forty more CPU items, render thread and audio mixer (2026-10-23, the user's request)

The batch-5 city profile (stock clocks, 13.2 fps, 5,030 draws a frame at about 12.4 us each): render thread 90 % of a
core, the audio mixer thread 26 % (voice filter 21 % of it, router 15 %, reverb 25 %, the kernel's IRQL and spinlock
calls about 6 %). One emulator test after all forty (guards on), as the user asked. Pixel-identical; the audio the same
bits (the one exception noted below is a NaN's sign).

Also in it, batch 5's console regression (stock clocks: city 11-14.6 fps, GPU 50 ms): plume's refreshPipeline queued
cached variants as compiles and the background cap blocked cache reads too (both fixed: refresh re-queues with the
cache flag; cached jobs are neither capped nor counted); the limiter's lazy log10 had been lost when the copies were
regenerated (tools/limiter_lazy_log10.py --hot must run after every localize); the 192 MB stream cache plus re-uploads
shrank the memory budget's room to 130 MB (the tomls back at 128 MB).

Render thread (24):
1. Memexport readback in CPU-cached memory: plume's deko3d buffer takes HOST_CACHED for READBACK too, cleans and
   invalidates the block once at creation, and map(readRange) drops the lines it will read (dc civac); unmap writes
   nothing back. CompleteMemExportsLocked maps the slot's windows only. The copy out of uncached memory was 2.8 % of the
   render thread (memcpy samples returning to its line).
2. Fetch-data copies kept across kicks (`banjo_renut_kick_reuse`): FetchCopy remembers the interval of the flush that
   made it (made_serial), the upload rings' resets then and the copy itself; a later draw of the frame takes it when the
   rings were not reset since and the store cards prove its guest pages unwritten since the interval before that flush
   (KickHeld; PagesClean with made - 1, for a write whose card was taken before its bytes landed). The game writes a
   buffer after the draws that read it only once the GPU is done with them (after a kick), so a draw-time proof is the
   flush-time answer. The city made 8 kicks a frame, each copying the fetch data again (2.7 % at FlushVertexFillsLocked's
   memcpy). Verify (`banjo_renut_store_cards_verify`): the kept bytes compared with guest memory.
3. Converted vertex streams kept across kicks the same way: the queued-stream table lives for the frame (its ring's
   resets) instead of a flush, 4,096 slots. Verify: the stream converted again and compared.
4. Queued fills keep their elements in an arena (g_fill_elements) and the host stride, not a copy of the declaration's
   HostVertexStream (a vector: a heap allocation and free per fill); ConvertVertices has a span form.
5. Host shaders of destroyed shader objects kept (`banjo_renut_shader_reuse`, 128, least recently kept goes; never freed
   at exit): N&B destroys and makes shader objects while it streams, and each new one unpacked and parsed its GLSL on
   the render thread (ZSTD 1 %, the GLSL scans, the string copies). A host shader is immutable but for its variants.
6. The composite pixel shader's answer kept in the record's draw summary (CompositePs scanned its list at every draw).
7. A declaration's streams in first-element order made once (stream_order); DrawNeedsExportFlush walked the elements.
8. PendingExportSlots' answers kept per caller slot (a fetch constant's stream check, its fetch by address, its copy,
   the index buffer) while the range and g_export_generation are the same: DrawNeedsExportFlush, FetchFromExportWindow,
   ConvertExportedStreamsLocked and FindExportedStream.
9. The bound slots' texture fetch constants converted once a draw (the prefetches, DrawNeedsExportFlush and
   FillTextures each converted them).
10. GetStreamSource once a stream (DrawStreamKeys' sources passed to BindStreams).
11. ApplyState's last chunk (the only one not 64 bytes) copied at its fixed size: inline, no C library call.
12. The recording threads' ChangedChunks and CopyState: the same for the last chunk's compare and copies.
13. Packed vertex components (8, 10, 11 and 2-bit) from tables of DecodeComponent's own results (no float division).
14. Half-float vertex components with NEON's conversion (the same values); a word with an infinity or NaN half takes
    HalfToFloat (the conversion would quiet a signalling NaN).
15. plume: bindGraphicsBuffers skipped when no binding and no pipeline changed since it ran (it would bind nothing).
16. The shared constants' texture lines left out of the compare when FillTextures wrote its previous call's words and that
    call's draw pushed last (the buffer holds them); push-constants verify compares them.
17. The GPU profile's per-shader labels from a small memo (a map search per draw of the timed frames).
18. Uncached 16-bit indices read, widened and ranged in one NEON pass.
19. Their rebase to 16-bit host indices in NEON (PrepareGeometry and TrackIndices).
20. A shader's literal registers as runs made once per cache entry (LiteralRuns), pushed as the per-draw loop pushed them.
21. PushSharedConstants copies its held lines at a fixed size (no C library call per run).
22. The NVK pipeline-bind hint not called on deko3d (two calls a bind that did nothing).
23. Pending exports counted (AnyPendingExports a test, not a walk of the slots).
24. A texture's extent prefetched only when DrawNeedsExportFlush's kept answer will not answer it.

Audio (16):
1-4. Raw exports of KfLowerIrql, KeRaiseIrqlToDpcLevel / KfRaiseIrql, KeAcquire/ReleaseSpinLock(At/From)RaisedIrql and
   KfAcquire/ReleaseSpinLock: the caller's context as it comes (no current_ppc_context(): a thread-pointer read and a
   call) and no argument shim. XAudio's router makes four of them for every voice and frame.
5. The voice filter's negation off the per-sample chain: high = -h with h = float(fma(q, band, -d)), and the band update
   takes h with -freq (exact: rounding is symmetric). Only a NaN's sign can differ (real audio has none).
6. Hand-written native of XAudio's ramped channel mix (rex_XAudioMixFrameBufferChannel, hand.txt), in front of its copy
   inside the router's: the same multiplies and adds a block at a time, each float byte-reversed once, the store cards
   marked once for the 1,024 bytes (`banjo_hand_xaudio_mix`).
7-16. Localized copies of ten more mixer functions (localized.txt): UpdateRPCs (CTrack), CRPC::ProcessEvents,
   CCue::GetState, CEngine::ProcessEvents, CEngine::ProcessEngineCallbacks, the mono converter's outer loop, CWave and
   play-wave-variation SetVolume, CRPC::OnVariableChange, CClip::Stop. XMASourceEffect::Process, PCMSourceEffect::Process,
   CSubmixVoice::Process, CSourceVoice::ProcessEffects, CEngine::ProcessVoiceList, CWave::ProcessEvents (raw guest
   access) and XMAPlaybackResumePlayback (MMIO) cannot be copies.

Pitfall: the localize tool reads every identifier in the app's sources (strings included) as a function a hook may
replace; a cvar description naming rex_XAudioMixFrameBufferChannel made the router call it as a real function, without
its native. Name generated functions in app/src/cpu/hot only.

Emulator check (one run with every change, guards every 64th call, store-cards and push-constants verify on): 1.25 M
calls compared, 0 differences (the filter's hook 5,535 compared, every new copy 0 differences); 2,600-3,700 fetch copies
(18-31 MB) per 10 s kept across kicks and none stale under verify; no renderer warning. The router's own guard never
compares (it calls the kernel), so a test build made the channel mix its own hook (the router as plain code): 109,117
calls compared with the original, 0 differences. Package builds/2026-10-23_1745_batch6 (NRO 5176f2666ac3, ELF beside it).

### 35b. Batch 6b: the recording compare, and a census before the next big step (2026-10-23)

The user asked for a frame-to-frame draw cache (#1), cheaper command recording (#2) and whether static shadow maps
could be skipped exactly (#5). Re-reading the draw path changed the estimate for #1: nearly every per-draw piece
(textures, pipelines, streams, indices, viewport, export answers) is memoised already, so a whole-draw cache mostly
replaces a dozen probes with one larger one (5-10 %, not 30-40 %). Measured first instead:

- `ChangedChunks` (the recording threads' NewOp, about 8 ms of core time a frame): the fetch constants' and the tail's
  whole chunks four at a time, all loads of the four before any test, no branch per chunk, the copies after; checked
  once at the first op against `ChangedChunksReference` (the chunk-at-a-time form) on made-up devices (a difference
  falls back to keeping every chunk).
- `banjo_renut_diag_repeat_census` (on in this package's renut.toml): frames 0 and 1 of every 32 hash each draw's state
  (the fetch constants and the tail but for the unread 10880-11775) with its call, and its shaders' constants when its
  guest data is unwritten since (store cards). The 10 s line gives the share of draws repeating their state, and per
  target the share that repeat completely, shadow maps (depth targets alone) apart, with the maps that repeat whole.

Static shadow maps (#5) are exact to skip: a depth-only map does not depend on the draw order, so a map whose draws all
repeat (state, constants, data) comes out the same, and the casters that did not change could be kept in a copy with
only the changed ones drawn over it. Whether it pays depends on the maps' views staying put (the census's shadow line).

City census (emulator, Showdown Town, the scripted route then `input_script_sticks`: both sticks wandering at random,
the camera turning; a capture every 30 s confirmed it): about 3,900 draws a frame. Against the frame before (and the one
before that): draws whose whole device state repeats 0-3 % (8 % at most against N-2), constants 1-4 % (shadow maps
0.0 %), guest data unwritten 70-76 %, nothing repeating whole, no shadow map the same. The shadow maps follow the camera:
while it moves every caster's constants change, so skipping a map exactly never applies in play. Whole-state repeats are
rare too (fetch constants and other device words change between frames even where the draw does not read them), so a
frame-to-frame draw cache would have to key on the fields each piece reads, which is what the per-piece memos already
do. Not built. Kick reuse in the city: 4,808 fetch copies (41 MB) and 6,248 converted streams (82 MB) per 10 s not
made again. `input_script_sticks = <s>` (sdk input_system.cpp): the sticks' random wander for emulator routes.
Package builds/2026-10-23_2032_batch6b (NRO c581bdef39c1; census off by default).

## 36. Recording from the setters' marks, and incremental draw state (2026-10-26, the user's plan #1 and #2)

The user's plan, pixel-identical: #1 incremental draw state on the render thread, #2 recording from setter tags (#3, the
GPU reading guest memory, later). Both audited for the A57 before the emulator test (assembly of the touched files read
with `-S -fno-lto`, the switch-dist objects being LTO-only).

**#2, `banjo_renut_tags` (on) and `banjo_renut_tags_check` (32).** A recorded op keeps the device's 64-byte chunks that
changed since the device's previous op (NewOp). It found them by comparing all 53 chunks (the fetch constants' 12, the
tail's 41) with a shadow at every op: NewOp was 26 % of each recording thread in the batch-6 profile. Now the native
setters mark what they write: `native::Device` has a `slot` (in what was padding: the struct stays 16 bytes, two
registers in an AArch64 call), and `SetW/SetH/SetB/SetQ` OR the written byte's chunk bit (`TouchBit`, a table by 64-byte
line; a constant for a constant offset, nothing outside the chunks) into `g_touch[slot]` (one cache line a slot). Only
the recording (no-hardware) devices get a slot (`TrackDeviceTouches` in CreateDevice, every chunk marked); the title
devices, which two threads may use at once, mark a sink. The slot is found by a direct-mapped table keyed by the device
address (`g_touch_map`: one load, no call; a `thread_local` is a call to `__aarch64_read_tp` on the Switch). NewOp takes
the marks and compares only those chunks (`ChangedChunksMarked`: each marked chunk compared and stored into the shadow
from the same NEON loads, no branch on the compare); the op is the same (the same chunks, the same bytes). Writes
outside the setters mark by hand (BeginCommandBuffer's fetch-constant clear, SetVertexFetchConstant's clear, the fence
words of KickDevice and Swap). The first emulator check (verify form) found the game's own code writing device fields
too, about 1.2 % of ops: Direct3D's inline setters compiled into N&B (a ZEnable as the depth control word and the
private copy at 0x2E64, a sampler's address mode in its fetch constant, the vertex declaration, 0x29C0). Such a write
sets the field's dirty tag (`m_Pending` words 2-4, or the GPU would never get the state), so the tags count as marks
(`native::TagChunks`): a fetch constant's tag is its chunks, its texture word and the stream words; any other tag the
render state's chunks (0x2780-0x2A7F and 0x2E00-0x2EFF). Tags the native layer clears before an op (FlushStatePackets,
EndCommandBuffer) are turned into marks first (`TouchTags`). A write nothing marks is what the check finds: every 32nd op of a device also compares every chunk; a chunk changed without a mark goes into the op, is logged
with its device offset ("Recording from marks: device +0x..."), and the marks are not used again (whole compares from
then on). `banjo_renut_tags_check = 1` is the verify form: every op checked, the marks kept, each such chunk counted.
The 10 s line "[renut] recording from marks" gives the ops, the share compared from marks, the chunks marked and
changed an op, the checks and the unmarked chunks. In-run A/B target 21.

Also: `ForgetSnapshotDeltas(device)` resets the immediate draws' delta chain only for the chain's own device. Every
recorded op called it, took the chain's lock and made the chain's next draw a whole device copy, for any device.

**#1, incremental draw state: sa2-nx's chain reuse made cheap and turned on for the replayed draws**
(`banjo_renut_chain_reuse` and `banjo_renut_chain_reuse_replays` on; in-run A/B target 20 turns the reuse off for a
half). A replayed draw knows which chunks its ops changed since the replay's previous draw; it takes that draw's
targets, framebuffer, viewport, texture and sampler words, pipeline key and pipeline when nothing each is made from
changed. It was off since package 36r: on the console the checks cost more than they saved (DrawStateLocked 9.5 ->
12.6 %). Now: the chunk tests are masks made at compile time (`ChainChanged<First, Last>`, one AND a word; a loop with a
branch a chunk before), the replays' hints are two shifts (`PlainToChain`), the targets' five words, the viewport's packet bytes (10432-10599, 0x29C0, the predication) and each bound
texture slot's fetch constant and texture word are compared themselves instead of their chunks (run 2 below), the
texture words and headers are copied only for a draw the next one can take them from, and the answers are counted in plain words (handed over every 1,024
draws; five calls and ten atomic updates a draw before).

Emulator checks (the city route with the sticks wandering, every op checked whole, `banjo_renut_chain_reuse_verify`
and `banjo_renut_snapshot_deltas_verify` on). Run 1 (marks alone): about 3,000 unmarked chunks per 10 s (1.2 % of ops,
six chunks: the inline writers above); reuse 0 differences but low hits (targets 6-10 %, viewport 23 %: the target words
share a chunk with the index and stream words, the viewport's packets one with the depth and blend words). Run 2 (marks
and tags; targets, viewport and texture slots compared word by word, not by chunk; stopped at 11 minutes as the user
asked): 0 unmarked chunks in 70 reports (285-425 thousand ops each, every one checked), 20 chunks compared an op of 53
(4.3 changed); reuse 0 differences, targets 99 %, viewport 95 %, texture words 52-57 %, pipeline key and pipeline 36-37 %;
delta chain 0 differences. Package builds/2026-10-26_1251_batch7 (NRO c1cb76969750, ELF beside it): renut.toml as batch
6b's, renut_ab_batch7.toml with the in-run A/B of targets 20 and 21 (30 s halves), renut_all_off.toml.

## 37. Unleashed-style draw path, steps 1-3 (2026-10-26, the user's request after the batch-7 console log)

The batch-7 console run (stock clocks, the city, about 5,200 draws a frame) had the render thread at 63-71 ms a frame,
12.2-13.4 us a draw, about 10 % better than batch 5; the three cores the game gets were at 282 % of 300 %. The user
asked how Unleashed and Marathon reach their CPU figures, then for the three steps. Unleashed's renderer
(UnleashedRecomp/gpu/video.cpp) keeps host state with dirty bits: each draw sends the game thread's changed state to
the render thread as small commands, and the render thread redoes only what is dirty (FlushRenderStateForRenderThread:
the pipeline looked up only when its state changed, textures and constants bound only when dirty; g_sharedConstants
kept from draw to draw). Renut's renderer had most of the pieces memoised already (texture, stream, index and pipeline
memos; float constants pushed as the groups that changed, PushFloatConstants' model of the GPU's buffer), but every
draw still walked every piece, and the console profile of batch 7 was flat: DrawStateLocked itself 24 %, the constant
pushes 13 %, the geometry 11 %, the replay 7 %, no single cost above 8 %. So the steps make whole sections of the draw
skip when their inputs did not change.

**Step 1: what changed, found at record time, and the render thread's dirty state.**
- `ChainHint::fields` (draw.h `DrawField`, 17 bits): the device fields that changed since the previous draw: fetch
  constants, bool and integer constants, the destination packet, the window/values/control packets, the other
  packets, 0x29C0, the private render state, the declaration, the index buffer, the targets, the streams, the texture
  words, the scissor RECT, the shaders, the predication, and the unread private words. The recording threads find them
  in NewOp, word by word in the chunks that hold more than one field (`MixedChunkFields`: the targets share a chunk
  with the index buffer and the streams; NEON compare-and-narrow, about 25 instructions a chunk), the other changed
  chunks whole; each op keeps them (`Op::fields`) and Replay ORs them up to each draw. The immediate draws' chain gives
  them by chunk (`ChainFields`).
- The reuse tests read fields: the targets' words and the viewport's 168 packet bytes are no longer compared (the
  viewport's was a C library memcmp at every draw: 3 % of the render thread in the batch-7 console profile), the
  texture slots' fetch constants and words only when they changed, the pipeline key by field instead of by chunk.
- `banjo_renut_persistent_shared` (on): the draws' SharedConstants kept from one draw to the next (Unleashed's
  g_sharedConstants): each draw writes the words it sets, puts back what the draw before set and it does not (texture
  slots: all three dimension words and the sampler of a set slot, the defaults for a slot no longer set; vertex fetch
  slots; bound fetch entries; export windows; point sprites; tessellation; the skin mode), and no longer zeroes 848
  bytes and copies 640 bytes of defaults (memset and the defaults' copy were 6.9 % of the render thread). A reused
  draw's texture words are in the block already. `banjo_renut_persistent_shared_verify` checks at every draw that every
  word the draw did not set is what a new block holds.
- `banjo_renut_keep_fetches` (on): the bound slots' converted fetch constants kept for the next draw while no fetch
  constant changed, and their header and memo prefetches skipped then; Replay prefetches the next op's headers only
  when it changed a header word.

**Step 2: vertex and index data.** The streams and indices were already host copies kept across frames, proven
unchanged by the store cards (Unleashed's host buffers kept in step). What was left is per-draw overhead:
- A draw that fetches nothing by address (most draws) makes no fetch-data allocation (it took 16 bytes at a 256-byte
  alignment of the upload ring at every such draw, about 1.3 MB of ring a frame, and wrote a fetch record nothing read).
- `banjo_deko3d_vtx_dedupe` (on): plume's setVertexBuffers sends only the slots whose range differs from what the
  command list bound there (every draw re-sent each stream and the zero stream).

**Step 3: constants.** The float constants were already pushed as the groups that changed (the GPU buffer's model).
PushFloatConstants walked every group the shader reads at every draw; now the groups with a register the buffer does
not hold are found word by word (`RegisterGroups`, a 256-entry table; a compile-time test proves it equal to
UsedGroups) and only those and the dirty ones are looked at.

Emulator checks (the city route, sticks wandering): `banjo_renut_persistent_shared_verify`,
`banjo_renut_chain_reuse_verify` (which also checks the kept fetch constants) and `banjo_deko3d_push_constants_verify`
on: 0 words of the kept block different from a new block's, 0 reused pieces different from the made ones, 0 draws with
a register the push model got wrong, over about 350,000 replayed draws per 10 s in the city; captures right. Reuse in
the city: targets 99 %, viewport 94 %, texture words 56 %, pipeline key and pipeline 39 % (36-37 % with chunk tests).
One earlier run of the step-1 build stopped drawing in the intro (between "appMainDraw tick 5" and 6, no guest fault in
Ryujinx's log); the two runs after it, with the same code and more, went through: not reproduced, noted. Package
builds/2026-10-26_1435_steps123 (NRO b878bd540f61, ELF beside it): renut.toml, renut_steps_off.toml (batch 7's and
these steps' switches off: the same NRO for a before/after), renut_ab_steps123.toml (in-run A/B of targets 20 and 21).
The emulator gives no console timing; the console run decides what these are worth.

## 38. Toward an API-level draw path: census, then stages 1-4 (2026-10-26, the user's requests)

**Census first (`banjo_renut_diag_unleashed_census`, off by default).** The user asked for (A) Unleashed's resource
model (host buffers and textures made at Create and updated at Unlock) and (B) deko3d command lists recorded on the
recording threads. One emulator run in the city counted what each would find: vertex streams from buffers our
CreateVertexBuffer hook made 1 % (those are the dynamic ones, written every frame), index buffers 0.1 %, textures 28 %
(Create or D3D_SetTextureHeader); Lock calls 0-1 per 10 s; the game-made streams' and indices' data written since the
frame before about 0.03 %; RunCommandBuffer recordings run again: 0 of about 1,000-1,400 per 10 s. N&B builds its own
resource headers on its own memory and writes it without locking, so (A) has nothing to hook; its data is static,
which the converted copies proven by the store cards already use. Every recording runs once, so (B) would only move
work to the recording threads (the three cores at 282 % of 300 %). The user chose to rewrite the draw path in stages
toward Unleashed's model instead, each checked in the emulator.

**Stage 1: shared constants and texture slots as dirty state.**
- Unleashed's SetDirtyValue for the persistent SharedConstants: every write goes through `SetShared` (or marks its
  region with `MarkShared`), which marks the 64-byte line only when the value changes (`g_sc_dirty`, bit 21 the tail);
  PushSharedConstants pushes exactly those lines, comparing nothing (it compared all 21 lines at every draw: 5.4 % of the
  render thread in the batch-7 console profile). `banjo_renut_persistent_shared_verify` compares anyway and logs a line
  changed without a mark.
- Each fetch slot's last answer (`g_slot_last`): a slot whose inputs are the memo hit's word for word takes it there
  and does the memo hit's work without reading the memo's entry (256 KB of them; with the fetch constants unchanged
  DrawStateLocked no longer prefetches it). The verify checks the kept words against the memo's answer.

**Stage 2: whole buffers (`banjo_renut_whole_buffers`, on).** Unleashed binds a buffer whole and draws with the game's
first index and base vertex. A draw whose every stream and whose index buffer have a converted copy of the whole buffer
binds those and draws that way (the host vertex index is then the guest's: tessellation[3] 0); the copies are made once
the store cards show the whole range unwritten for 3 kicks and are proven at each kick's first use like the range
copies (dropped and the draw takes the ranges when written). Before, every draw converted, cached and bound the range
its indices reach, with its indices rebased: a copy and a memo lookup per range, a binding per draw. Dynamic, Draw*UP,
fan and quad, tessellated and fur skin-once draws keep the ranges, and so does a draw that reads a pending export's
window. `banjo_renut_whole_buffers_verify` compares each whole draw's indices with the guest's and its vertices' rows
with the range converted again. The 10 s line "[renut] whole buffers" gives the share of draws drawn whole.

**Stage 3: constants.** Nothing further: the float constants were already pushed as the groups that changed (with
step 3's `RegisterGroups` the groups looked at are only the dirty and the unheld ones), and the shared constants are
stage 1's dirty lines.

**Stage 4: sections skipped when their inputs did not change.**
- The memexport read check (DrawNeedsExportFlush, 2.4 % of the render thread; it ran at every draw while exports were
  pending): its "none, and no read met a window" answer is kept for the next draw while the pending exports
  (`g_export_generation`), the fetch constants, texture words, streams, index buffer, shaders and declaration are the
  same and the draw has no exports of its own (`banjo_renut_chain_reuse_verify` checks it).
- Whole buffers while exports are pending, for a draw none of whose buffers a pending export writes.
- The index cache prefetch skipped for a draw that will most likely be drawn whole.

- Whole buffers took 0 % of draws in the stage-2 run: the whole path gave up whenever any export was pending, which
  in the city is always. Stage 4's per-buffer test fixed it; the 10 s line now also gives why a draw took the ranges
  (by the first reason met) and the cache's size.

**Emulator checks (city route, banjo2).**
- api4 (every verify mode on: chain reuse, persistent shared, push constants, whole buffers): 0 warnings; 96-97 % of
  draws whole; the rest: kind (fans, quads, tessellated, skinned once) about 80 %, a stream in a pending export's
  window about 18 %, written lately the rest.
- api4ab (in-run A/B, target 22 = banjo_renut_whole_buffers): render thread 133 ms a frame on average with whole
  buffers against 173 off over 12 pairs (the emulator, noisy; console to confirm). The A/B's off halves made range
  copies again each minute and filled banjo_renut_stream_cache_mb (128): up to 180,000 draws a 10 s fell back for a
  full cache. Without the A/B (run gpu1) 0-600.
- Whole copies made lately drop the range copies of their buffer no draw used for 4 frames (DropReplacedRanges, one
  pass over the caches 4 frames after the copy): in practice those ranges are still in use then and go with the idle
  pass, so it drops little.

**GPU, from the 2026-10-23 table (the user's request).** The console's city frame (batch 7, 42.9 ms of GPU): shadow
maps 4.4 ms (2,380 depth-only draws), depth resolves 3.0, colour resolves 1.6, exported stream conversion 1.6, PS
3277104C 5.0 (four blended draws with 17 texture fetches). The deko3d counters give about 32 full barriers and 31
copy-engine buffer copies a frame: all memexport (a seed copy before each memexport draw, a readback copy after it,
the fetch data put together on the GPU), each copy between full barriers.
- `banjo_renut_batched_readbacks` (on, A/B target 24): a memexport draw's readback copy (read by the CPU only after
  the slot's fence) waits for the list's end (FinishRecordingLocked), all of them in one group between two barriers.
  Nothing writes a window after its draw (a slot's windows only grow), and every later reader waits for the draw with
  its own barrier (deko3d's Primitives completes all earlier 3D work, fragments included). City: about 9 memexport
  draws a frame, one readback group a frame instead of nine (about 16 full barriers fewer).
- `banjo_renut_convert_reuse` (on, A/B target 23): a stream conversion (stream_convert_cs between two full barriers)
  already made in the frame from the same export slot, offset, vertices, strides and elements, with no export recorded
  since (g_export_generation), is used again. City: half of them (about 14 a frame).
  **Beneficial (console A/B, 2026-10-30, da8ea3599258, 3-core stock, city):** GPU -1.06 ms a frame, render thread
  -0.54 ms, +0.41 fps (on minus off). It won in all 3 city pairs, against control noise of ±0.26 ms GPU and ±0.45 fps.
  Keep it on.
- Not done, measured or reasoned: colour hand-over at the end of the main pass (its resolve and clear come in two
  halves, 0-384 and 384-720; one 648p copy, 0.2-0.3 ms; parked); the mid-frame colour and depth copies for the water
  (the same target keeps being drawn: needed); seeds through the 3D engine's inline data (about 600 KB a frame);
  shadow draws sorted (61 % of draws bind a pipeline, 34 % change shaders, but a bind is tens of front-end methods:
  the console has to show it matters first); the alpha-test early-out is already in 3,936 shaders.

Package builds/2026-10-26_1937_stages_gpu1 (NRO f102648be906): renut.toml, renut_steps_off.toml (every switch of
batch 7, steps 1-3, stages 1-4 and the two GPU items off), renut_ab_stages.toml (targets 20-24 in turn, 30 s off and
on each).

## 39. plume's deko3d defaults (2026-10-30, found and fixed by sasrt-nx)

The SASRT port (sasrt-nx, `docs/enhancements.md`, "plume's deko3d defaults") found four places where banjo-nx's plume
deko3d back end left deko3d's defaults in, or had a bug. It fixed them in this tree.

- **The Zcull invalidate after copies had no braces** (`invalidateZcullAfterCopy`, section 13). Only `flushBarriers()`
  was conditional. `dkCmdBufBarrier(..., DkInvalidateFlags_Zcull)` was recorded after every texture upload, copy,
  resolve and blit, into any texture, with Zcull on or off.
  - With Zcull on (banjo's default), every such copy left the bound depth target's Zcull invalid until its next clear.
    For the rest of the frame after the first upload or resolve, Zcull culled little or nothing. The fix keeps the
    invalidate for copies into a depth target only, so more of the frame is culled. Pixels are the same; only the
    console shows the gain, because Ryujinx emulates no Zcull.
  - With Zcull off, SASRT's console stopped twice during the boot (an invalidate on a queue with no Zcull context).
    `banjo_deko3d_zcull = false` is now safe to use as a fallback.
  - Watch for geometry missing or flickering on the console. That would mean something writes a depth target's memory
    without going through a copy, such as a colour view of the depth words. Zcull would then cull draws the depth lets
    through, and the invalidate after every copy had hidden that until now.
- **MSAA sample positions** (`banjo_deko3d_msaa_positions`, default on): `dkMultisampleStateDefaults` puts every sample
  at the pixel's centre, and `dkCmdBufBindMultisampleState` always writes the positions. Multisampled pipelines now get
  the standard positions for their sample count (`dkMultisampleStateSetLocations(&ms, nullptr, 0)`). Banjo has MSAA
  off, so the report's "multisampled pipelines" should stay 0 here.
- **Alpha-to-coverage dither** (`banjo_deko3d_a2c_dither`, default off): deko3d turns it on by default, giving a fixed
  screen-space grid. Banjo already turns pipeline alpha to coverage off on its single-sampled targets
  (`banjo_renut_alpha_to_mask_test`, section 12), so this changes nothing here.
- **Primitive restart for indexed strips** (`banjo_deko3d_strip_restart`, default on): deko3d never turns restart on
  (only `dkCmdBufSetPrimitiveRestart` writes it). An all-ones index in a strip drew triangles to and from vertex 65535,
  where the Vulkan back end restarts. `drawIndexedInstanced` now sets the restart index for triangle and line strips
  (0xFFFF, 0xFFFFFFFF, 0xFF by index size) and turns it off for other draws, only when it changes. The report counts
  "indexed strips drawn with primitive restart".

The `Deko3DOptions` fields default to the fixed behaviour. The deko3d report line gives the new counts.

## 40. The console's out-of-memory crash: GLSL unpacked on demand (2026-11-01)

The user's shader-gathering session (a save that visits many areas) crashed after 9 minutes and about 7 level loads:
`std::bad_alloc` in a command buffer arena's growth (`ReserveArena`, guest/command_buffers.cpp).

- The Switch's heap is 3,084 MB in two pools. newlib had handed out 1,713 MB: the game's committed memory (684 MB,
  the profiler's "guest backing" figure) and the renderer's host data. The GPU region at its top was 1,298 MB, with
  1,007 MB in blocks and 291 MB free in 12 ranges.
- newlib had 74 MB left and its free pieces were small. The region's free ranges are of no use to newlib, and the region
  only gives back free space at its bottom. Its peak (1,261 MB in blocks, at a level change) set its size.
- Earlier sessions show the same pattern with more room: 154 to 267 MB left after 6 to 8 level loads.

What grows with every new area in newlib: each deko3d shader object kept its whole GLSL text (35 KB on average; the
installed package unpacks to 161 MB, 142 MB of it pixel shaders), and shader objects are never destroyed.

- Now a shader's text comes from `plume::Deko3DGlslSource`. A shader made from a package in memory (the installer's
  package with its dictionary, and the NRO's) gets a loader (`Deko3DShaderSetGlslLoader`, Renut's `UnpackGlsl`) and
  its text is dropped. `text()` unpacks it again when a new variant key or a compile needs it. A weak pointer keeps
  it while a lookup or a queued compile holds it, and the last 32 texts are held a little longer.
- The variant keys are unchanged: the same text, so the DKSH cache keeps every entry.
- A package read from the card (no dictionary) keeps its texts, as before.
- The memory report has a new line: "Memory: heap users: guest memory N MB committed in N chunks; deko3d N shader
  objects, N MB of GLSL alive, N graphics pipeline objects".

Considered and not taken: the game's committed memory from the GPU region. The game commits its 684 MB in the first
two minutes and keeps it, while the region's free ranges appear later, at level changes, so the two would rarely meet.
deko3d may also leave a freed block's pages with another memory attribute, and the game's memory is mapped as process
code memory, which could fail on such pages.

## 41. renut-nx 1.0.0, the public build (2026-11-01)

- **Names.** The project is renut-nx (GitHub `ChanseyIsTheBest/renut-nx`, was `renut_nx`), the NRO `renut-nx.nro` in
  `sdmc:/switch/renut-nx/` (REX_SWITCH_FALLBACK_EXE), the installer renut-nx-webinstaller. NACP: title "Banjo-Kazooie
  Nuts and Bolts", author ChanseyIsTheBest, version 1.0.0; icon `tools/switch/icon/banjo_icon.jpg` (the square cover,
  colour-matched in Lab to the poster).
- **Logs off but crash logs.** `log_level` "off" on the Switch: no log file, no `rex_stderr.log`, no `rex_profile.log`,
  no `deko3d.log`; the stall watchdog off; the diagnostics off. `rex_crash.log` is written by the crash handler as
  before. `log_level = "info"` in renut.toml turns them on.
- **Four cores from any player's Sphaira forwarder**: docs/four-core-mode.md.
- **The web installer's GLSL package, compressed at the first start** (`CompressInstalledPackage`,
  banjo_renut_deko3d.cpp; the user's idea). The page writes `deko3d_glsl.bin` with raw zstd frames (it has no
  compressor): read from the card shader by shader, its texts kept (section 40 needs a package in memory). The
  installer ships the native package's dictionary as `deko3d_glsl.dict` (128 KB). At the first start renut-nx compresses
  every shader with it (zstd level 3, one CDict: 161 MB of GLSL to about 23 MB), writes `deko3d_glsl.bin.tmp`, then
  replaces the file; from then on it is the in-memory form with a dictionary, as on the user's console. The texts do not
  change, so the variant keys and the DKSH cache stay valid. A stop between removing the old file and renaming the new
  one is finished at the next start.
- **The installer's reference run** (test/full_node.mjs on the user's game files, 2026-11-01): 4,731 shaders, shader
  table numbers all native, 4,726 GLSL texts the user's own (the other 5: the fetchspec vertex shaders, the same
  constants in another order of passes); 28,570 variants from the user's pipeline list (68,590 pipelines; 97 of the 100
  run-time compiles in the console logs among them, the other 3 added) compiled with 0 failures, and the 21,076 that the
  user's deko3d_shaders.bin also has byte for byte the console's.
