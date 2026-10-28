# From GPU emulation to an API-level renderer: a conversion guide for ReXGlue ports (2026-10-16)

> Status 2026-10-28: this guide is older than the current renderer. Start at `docs/porting-start-here.md` (the reading order, what is current, the method rules) and see `docs/porting-guide-renderer.md` section 6b for the items measured slower on the console. The MSAA notes here are history: MSAA is off (`disable_msaa`).

For every port in this family that renders through the masseffect-derived native renderer (Banjo-Kazooie: Nuts &
Bolts, SA2, Futari, SART, Peggle 2, Sonic Jump Fever, Mass Effect): what UnleashedRecomp and MarathonRecomp do
instead, what our renderer still emulates, and the order in which a port can move to their model with the picture
kept exact at every step. Written from the upstream PC sources (UnleashedRecomp `5e8695a`, 2026-02-01, the last
hedge-dev commit before the Switch fork; MarathonRecomp `bd9c0bb`, 2026-05-25), Banjo's code and console logs, and the
Switch forks in `C:\devkitPro\UnleashedRecomp-NX` and `C:\devkitPro\MarathonRecomp-NX`. File references are to those
trees unless they start with `app/` (banjo-nx).

## 0. The short version

- **Unleashed and Marathon never run the Xbox 360's Direct3D.** They replace `CreateDevice` and every Direct3D function
  the game calls with native code (`GUEST_FUNCTION_HOOK`). Textures, buffers, surfaces, shaders and vertex
  declarations are host objects; draws become commands for a render thread that names those objects. There is no
  PM4, no Xenos register file, no EDRAM, no shader identification, no scan of guest memory for changes.
- **Our renderer starts one level lower.** The game's own recompiled Direct3D runs and writes Xenos PM4; the renderer
  (the "ring") reads the packets back, rebuilds the register file, identifies shaders from microcode, textures from
  fetch constants, vertices from guest memory, and models EDRAM tile by tile. Every one of those is emulation: work
  the GPU did in hardware, done again per draw on the CPU, or EDRAM behaviour done again on the GPU.
- **Banjo has already moved the transport up** (records instead of PM4 for every Direct3D writer, recorded command
  buffers as host lists, kicks through a host queue: `docs/d3d-full-replacement.md` steps 4-6). That alone did not
  make the per-draw work small, because the work is not the parsing: it is re-deriving objects from values. The rest
  of this guide is about making the objects come from the API, as Unleashed does.
- **The order that works:** census first, then (A) targets as native surfaces with Clear and Resolve as surface
  operations and predicated tiling off, (B) shaders by object, (C) textures by object, (D) buffers by object, (E) draws
  straight from the device to render commands, (F) delete the emulation paths. Each step has a key, an in-run A/B and
  a verify mode that compares it with the emulated path, so "exact" stays measurable.

## 1. The two models side by side

| | Emulation model (our renderer today) | API model (Unleashed / Marathon) |
|---|---|---|
| Where the game's GPU work enters | Xenos PM4 written by the recompiled XDK Direct3D (or, in Banjo since package 24, records of the same packets) | the Direct3D call itself: `SetRenderTarget(device, i, surface)`, `DrawIndexedPrimitive(...)` |
| State | a 0x10000-word register file rebuilt from packets | a `GuestDevice` the game writes (dirty masks, constants, sampler words) plus host-side state set by the replaced setters |
| Shaders | identified per draw from the microcode (fingerprint, library lookup, sometimes ambiguous) | `CreateVertexShader(function)`: XXH3 of the container, one lookup in the precompiled cache, once per object (`video.cpp` `CreateShader`) |
| Textures | per draw from fetch constants: address + format + size, cache lookup, fingerprints of guest memory to see changes, untiling on upload | `CreateTexture` makes a host texture; `LockTextureRect` returns linear staging memory, `UnlockTextureRect` uploads; the engine's file loader is hooked (`MakePictureData`) so most textures come straight from DDS files |
| Vertex / index data | read from guest memory at draw time, fingerprinted or copied, byte-swapped, indices converted | `CreateVertexBuffer` makes a host buffer; `Lock` returns staging memory; `Unlock` byte-swaps once and uploads (`UnlockBuffer<uint32_t>` / `<uint16_t>`) |
| Render targets | EDRAM views keyed by (base, pitch, format, MSAA), tile ownership, imports and exports between views that alias, pitch-sized images | `CreateSurface(width, height, format, multisample)` makes a host image of that size; `SetRenderTarget` binds it; surfaces never alias |
| Clear | rectangle draws of Direct3D's clear shaders through EDRAM aliases, proofs to take tiles whole | `Clear(flags, color, z)`: `clearColor` / `clearDepth` on the bound images |
| Resolve | an EDRAM-to-texture copy into guest memory, the texture found again by address later | `StretchRect(flags, texture)`: the texture remembers its source surface; while the surface is unchanged the texture samples the surface itself; the copy is made when the surface is about to change (`ExecutePendingStretchRectCommands`) |
| Predicated tiling | emulated (tiles once: draw once, replay the second tile's resolves) | Marathon: `BeginTiling` = one Clear, `EndTiling` = one `StretchRect`; Unleashed's game does not tile |
| Threads | game threads write PM4; one ring thread (Banjo: decoder + executor + recording worker) | game threads enqueue commands; one render thread records and submits |
| Exactness reference | the 360 (Xenia's semantics), pixel for pixel | "looks like the 360": Unleashed maps D24S8 / D24FS8 to D32_FLOAT with no stencil, uses real MSAA, drops conditional rendering |

The last row matters: Unleashed and Marathon accept small differences our ports do not. Where this guide says "exact",
it means identical to our current emulated output, which is the reference every port verifies against.

## 2. How Unleashed and Marathon do it (upstream PC sources)

### 2.1 The device

`CreateDevice` (`sub_82BD99B0` in Unleashed, `sub_8253EC98` in Marathon) allocates a `GuestDevice` in guest memory
(`g_userHeap.AllocPhysical<GuestDevice>()`) and fills it so the game's own inlined Direct3D code keeps working against
it. Only the fields the game touches directly are modelled (`video.h`):

```cpp
struct GuestDevice {                     // Unleashed's XDK layout, 0x5E00 bytes
    be<uint64_t> dirtyFlags[8];          // the game's inlined setters set bits here
    be<uint32_t> setRenderStateFunctions[0x65];  // D3D's per-state setter table: filled with host functions
    uint32_t setSamplerStateFunctions[0x14];
    uint8_t padding224[0x25C];
    GuestSamplerState samplerStates[0x20];
    uint32_t vertexShaderFloatConstants[0x400];  // written by the game's inlined SetVertexShaderConstantF
    uint32_t pixelShaderFloatConstants[0x400];
    be<uint32_t> vertexShaderBoolConstants[0x4];
    be<uint32_t> pixelShaderBoolConstants[0x4];
    uint8_t padding27A0[0x68C];
    be<uint32_t> vertexDeclaration;
    uint8_t padding2E30[0x338];
    struct { be<float> x, y, width, height, minZ, maxZ; } viewport;
    uint8_t padding3180[0x2C80];
};
```

The render-state setter table is the XDK's dispatch for `SetRenderState`: Unleashed writes host functions into it
(`g_memory.InsertFunction` at addresses after the code, `HostToGuestFunction<SetRenderState<D3DRS_ZENABLE>>` and so on),
so a guest `SetRenderState` call lands in native code. Marathon's XDK calls per-state functions, so it hooks each by
address instead (`GUEST_FUNCTION_HOOK(sub_82541A78, SetRenderState<D3DRS_ZENABLE>)`, 31 of them, stencil included).

### 2.2 Resources are host objects in guest memory

Every resource is a C++ object allocated in guest memory whose first bytes are what the game reads (`GuestResource`:
an unused word, a big-endian reference count, the type), with host pointers behind (`GuestTexture`: `RenderTexture`,
its view, a descriptor index, the staging memory of a lock; `GuestSurface`: the image, its framebuffers by partner,
the textures waiting for its resolve). The game passes these pointers back into the replaced functions; it never sees
Xbox memory layouts for them, so nothing has to be decoded from guest memory at draw time.

- `CreateTexture` → `g_device->createTexture(desc)`, a view with the component swizzle for the guest format, a slot
  in one bindless descriptor heap.
- `LockTextureRect` → linear staging memory in guest space (`g_userHeap.AllocPhysical(pitch * height)`);
  `UnlockTextureRect` → a render command that copies it into the texture.
- `CreateVertexBuffer` / `CreateIndexBuffer` → host buffers; `Lock` → staging memory; `Unlock` → swap every 32-bit
  (vertex) or 16-bit (index) word once and upload. The translated vertex shaders read the swapped words.
- `MakePictureData` (a game-engine function, not Direct3D) is replaced so the engine's texture files are loaded as DDS
  straight into host textures: no Xbox tiled formats at run time.

### 2.3 Shaders by object

`CreateVertexShader` / `CreatePixelShader` hash the microcode container (`XXH3_64bits(function, function[1] +
function[2])`), find the entry in the precompiled XenosRecomp cache (a sorted table, `FindShaderCacheEntry`), and keep
it on the `GuestShader`. A draw names the shader object; the pipeline key is built from objects and state, never
from microcode. Specialised variants (alpha test, bools) are linked on demand per object (`GetOrLinkShader`).

### 2.4 Draws

`DrawIndexedPrimitive` (and `DrawPrimitive`, `DrawPrimitiveUP`) first runs `FlushRenderStateForMainThread`: it reads
the device's dirty masks the game's inlined setters left, and turns them into commands (sampler state words, the
dirty range of float constants copied into an intermediary upload buffer, the bool words), then enqueues the draw.
The render thread applies the commands to its pipeline state, looks the pipeline up by hash (precompiled at load from
the engine's mesh data, in the background), binds what changed and draws.

### 2.5 Targets, Clear and resolves

- `CreateSurface(width, height, format, multiSample)` → a host image of exactly that size and format, with real MSAA
  when asked. `SetRenderTarget` / `SetDepthStencilSurface` set the framebuffer (one per pair, cached on the surface)
  and the default viewport.
- `Clear(flags, color, z)` clears the bound images (`clearColor`, `clearDepth`), after any copies still pending from
  them.
- `StretchRect(flags, texture)` (the XDK's Resolve) does not copy. The texture records its source surface; a texture
  slot holding it samples the surface itself (`SetSurface`), MSAA surfaces excepted. The copy, or the MSAA resolve
  shader, runs only when the surface is about to change: at the next Clear or framebuffer change
  (`PopulateBarriersForStretchRect` + `ExecutePendingStretchRectCommands`). The Switch fork goes further: a copy whose
  texture is overwritten before any read is dropped, a texture cleared after its resolve takes the surface's image
  ("hand-over").
- Marathon replaces predicated tiling outright: `D3DDevice_BeginTiling` clears with the tiling clear values and
  returns 0; `D3DDevice_EndTiling` does one `StretchRect` into the destination texture. The scene is drawn once,
  full size, into a surface that is not shared with anything.

### 2.6 What is left to the game-specific layer

Hooks on the engine, not on Direct3D: resolution and aspect fixes, pipeline precompilation from the engine's own mesh
and material data (Unleashed's `CompileMeshPipeline`), texture file loading, a few mid-function hooks. Conditional
rendering (`BeginConditionalSurvey` / `BeginConditionalRendering`) is stubbed in Marathon.

## 3. What our renderer still emulates (Banjo, package 25, console, stock clocks)

Banjo is the furthest along in the family, so its numbers show what is left after the transport is gone.

**CPU, per draw (about 1,900-2,000 draws a frame):** the executor 19 µs a draw (57 % of a core), the decoder 21 % of a
core, the recording worker 11 %. From the console stage timers (package 16-21, before deferred recording moved the
driver calls to the worker):

| Stage | µs a draw | Emulation? | What removes it |
|---|---|---|---|
| PM4 parsing, register decode, identification | 4.4-5.35 | yes | records (done in Banjo) then draws from the device (step E) |
| Textures and samplers (fetch constants → descriptors, cache, fingerprints) | 2.9-4.2 | yes | textures by object (step C) |
| Indices (conversion, upload or the persistent arena's proof) | 3.1-3.7 | yes | index buffers by object (step D) |
| Vertex data (copies, fingerprints of guest memory) | part of "targets, vertices" ~3.2 | yes | vertex buffers by object (step D) |
| Pipeline lookup, shared constants | ~1.6 | partly | pipelines keyed by objects |
| Vulkan driver calls (pipeline bind 2.9-3.5, draw 2.4-2.9, UBO set 1.3-1.6) | ~8.4 | no | stays (Unleashed pays it too); fewer binds and shader objects reduce it |

**GPU, per frame:**

| Category | Banjo now | Emulation? | What removes it |
|---|---|---|---|
| EDRAM stencil rebuilds (`edram_import9`) | 4-8.6 ms in levels, 28-46 ms on the screen after the title | yes | targets as surfaces (step A); package 26 already limits them (`banjo_gpu_stencil_clear_partial`) |
| Resolves (`copies`) | 6.4-7.0 ms | mostly no (the game asks for them) | the pending-copy model: copies only when the surface changes, dropped when nothing reads them (about 0.5-1 ms) |
| Depth format conversions | in `copies` | no | Vulkan has no 24-bit float depth; the game samples converted values |
| Translator overhead | heavy pixel shaders 725-778 NAK instructions (Unleashed's median 554) | partly | translator work (`docs/shader-translator-ideas.md`) |

## 4. Why a game can be harder than Unleashed: the Xbox features to look for

Run the census (section 5, step 0) and answer each of these for your game. Banjo's answers are given as an example.

| Feature | What it means for the API model | Banjo |
|---|---|---|
| **Inlined setters** | The XDK compiled `SetTexture`, `SetVertexShaderConstantF`, render states into the game as stores into the device's shadows and dirty masks. There is no function to hook; the replacement reads the device at draw time (as Unleashed's `FlushRenderStateForMainThread` does). The `GuestDevice` must keep this XDK build's offsets | yes: 24,064-byte device, the layout in `docs/d3d-native.md` section 2 (dirty masks +0..+40, fetch constants +1152, float constants +1920 / +6016, bools +10112, register-group shadows +10368.., targets +12440 / +12456, shaders +12692 / +12696). The shadows are Xenos register words, not D3D render states |
| **Recording devices / command buffers** | Draws recorded on worker threads into command buffers that the main device runs later (per tile under tiling). In the API model a recording device appends render commands to the command buffer's host list; running it replays the list | yes: two worker devices (CreateDevice type 2, flag 0x80 at +10940); every `DrawIndexedVertices` of a level is recorded. Banjo's records already do this (`docs/d3d-full-replacement.md` step 5) |
| **Predicated tiling** | `BeginTiling` / `EndTiling`. Replace as Marathon does once targets are native surfaces | yes: 2 tiles at 2x MSAA. Forcing one tile at the Direct3D level put the post-process surfaces over EDRAM base 1440 and gave bands, **but only because EDRAM was emulated**: with native surfaces nothing overlaps |
| **Surfaces sharing EDRAM** | On the 360 two surfaces at the same EDRAM base share their bytes. Native surfaces do not. Find every case where a surface reads what another wrote (draw with a depth/stencil/blend read, or a resolve) without a Clear or full overwrite first | the 1x 1280x720 depth surface and the 2x scene depth both live at base 0. The renderer rebuilt the scene's stencil into the 1x surface every frame, for Direct3D's Clear of a 96x96 (levels) or 512x512 (screen after the title) rectangle, which writes stencil and reads none of it (2026-10-16 trace). No real reliance found yet |
| **Resolves the CPU reads** | A resolve into memory the game then reads on the CPU (screenshots, readbacks, CPU-side effects): the API model must write the copy back to guest memory when the CPU reads it | yes: hundreds of read-backs per 10 s at loading (the reason deferred recording once hung the loading screen) |
| **Textures from memory headers** | `XGSetTextureHeader` / XPR resources instead of `CreateTexture`: no API event when a texture is made or changed. Hook the engine's loader (Unleashed's `MakePictureData`) or keep a validated texture cache | to be measured: `D3DDevice_CreateTexture` (`82655EC8`) exists; the bulk of the textures arrive as fetch constants pointing at loaded data |
| **Vertex memory written without Lock** | The engine fills buffers by file reads or decompression straight into their memory. Upload-on-Unlock misses those writes: hook the engine's model loader, or keep the persistent arena with its proof | yes: found on the PC (`banjo_ring_vertex_lock_rule`, not exact: a 640 KB block rewritten with no Lock 29 s after start) |
| **Memory export** | Vertex shaders that write memory (eA / eM). Unchanged in the API model: a storage buffer and a write-back | yes: water ripples, animated meshes |
| **Tessellation** | Xenos patches (`kTrianglePatch`) | yes: the water (`banjoTessDomain`) |
| **24-bit depth, stencil** | Unleashed drops stencil (D32_FLOAT). A game that uses stencil needs D32_S8 and the exact depth encoding where depth is sampled | Banjo uses stencil masks and samples depth (soft particles, water) |
| **Occlusion queries / conditional rendering** | Marathon stubs them; our renderer answers them itself | Banjo's ring answers them itself (`banjo_gpu_skip_dead_draws`) |

## 5. The conversion, step by step

Each step keeps the old path behind its key, has an in-run A/B target (`banjo_ab_target` style) so one recording
compares both, and a verify mode that runs both paths and compares the result. A step goes on by default only after a
whole verify route shows no difference and the console A/B is at least neutral.

### Step 0. The census (diagnostic, no behaviour change)

1. **Direct3D's writers.** `tools/d3d/d3d_writers.py` finds every recompiled function that writes after the device's
   write pointer or calls the space functions; `tools/d3d/d3d_census_build.py` hooks them and counts calls and packets
   per frame, device and thread (`docs/d3d-native.md` section 1). That is the list of functions the replacement must
   cover.
2. **The device layout.** `tools/d3d/ppcfn.py <address>` prints a function's PowerPC from the generated code. The
   setters show the device offsets: Banjo's `SetRenderTarget` is `8227AB68` (`stwx r5` at device + (index + 3110) * 4 =
   +12440 + 4 * index; the surface's word +28 copied to the colour-info shadow), `SetDepthStencilSurface` `8227AF40`
   (+12456; words +28 and +32 to the depth-info shadows); a reset function (`8264E1A8`) calls both for every slot,
   which is how they were found (`grep "stw r[0-9]*,12456(r"` in the generated sources).
3. **Surfaces.** Hook `CreateSurface` / the setters: every surface object, its EDRAM base, pitch, MSAA, format and size
   (Banjo: the size is the surface's word +36, width - 1 in bits 18-31, height - 1 in bits 3-17). Log every case where
   a surface is read (a draw that tests depth or stencil, blends, or a resolve) over pixels another surface wrote since
   its last Clear or full overwrite. That list is what step A must reproduce; everything else becomes independent
   images.
4. **The features of section 4.** For each, count it per frame.

### Step A. Targets as native surfaces

1. One host image per Direct3D surface, sized to the surface (not to the pitch), bound when the device binds it.
2. `Clear` clears the bound images (honouring the rectangle and the flags, stencil included where the game uses it).
3. `Resolve` becomes the pending-copy model of 2.5: the texture remembers its surface, samples it while it is unchanged,
   and the copy is made when the surface changes. Resolves the CPU reads are written back when the CPU reads them.
4. Predicated tiling off as in Marathon: `BeginTiling` → one Clear, `EndTiling` → one resolve.
5. Only the reinterpretations step 0 found (if any) become explicit surface-to-surface conversions at bind time.
6. **Verify:** run the EDRAM model in parallel for the first N frames of each new surface pairing and compare a read-back
   of the bound images after each pass (or a checksum computed on the GPU into a host-visible buffer). Any difference
   names the surface pair and turns the step off for the session.

This step removes the EDRAM imports and exports, the tile bookkeeping on the CPU (about 3.5 % of Banjo's ring), the
pitch-sized images, and the reason 1x depth views lost ZCULL (`TRANSFER_DST` for stencil copies).

### Step B. Shaders by object

Hook `CreateVertexShader` / `CreatePixelShader` (or, where the XDK builds shader objects inline, the draw-time shader
load: Banjo's `8222BF10`, which reads the bound objects at device +12696 / +12692). Map each object to its library
entry once; the draw names the object. Banjo already notes the objects there; what remains is to stop identifying
from microcode at all. Verify: in verify mode, identify from microcode as well and compare.

### Step C. Textures by object

Where the game uses `CreateTexture` / `LockRect` / `UnlockRect`, do as Unleashed. Where textures are memory headers,
hook the engine's resource loader (find it from the callers of the header setup) and convert each texture once when it
is loaded; keep the fingerprinted cache only as the fallback for textures that appear without a load event. Sampler
state comes from the device's sampler words. Verify: convert from guest memory as today and compare the bytes, for the
first uses.

### Step D. Buffers by object

Vertex and index buffers created through `CreateVertexBuffer` / `CreateIndexBuffer` and filled through `Lock` /
`Unlock`: swap once at `Unlock`. Buffers the engine fills without a Lock (Banjo's loading): hook the loader that fills
them and upload at its end. Indices converted once per buffer and version, not per frame. Verify: the persistent arena's
byte-for-byte check, which Banjo already has (`banjo_diag_vertex_persistent_check`).

### Step E. Draws straight from the device

The draw functions read the device (dirty masks, shadows, bound objects) and enqueue a render command naming objects:
no PM4, no records of packets, no register file. The executor becomes the render thread. In Banjo this replaces the
native draw path's record writing (`banjo_d3d_records_native`) with object commands. Verify: for the first N draws of
each kind, also write the old record and compare the state the two produce.

### Step F. Remove the emulation

Once every step's A/B has decided on the console: drop the PM4 parser, the register file, the EDRAM model and the
identification code from the shipped build (keep them in a diagnostic build for future comparisons). Every old form
also costs shader-library size and compile time while it is kept (`docs/shader-translator-ideas.md` item 117).

## 6. What we learned the hard way (do not repeat)

- **The transport is not the cost.** Records instead of PM4 lost two console A/Bs in Banjo (`banjo_direct_state`
  47.2 against 45.0 µs a draw); the per-draw work is re-deriving objects, not parsing words. Go for objects.
- **Direct3D object versions are not a proof for memory.** Banjo's loading writes vertex memory without a Lock
  (`banjo_ring_vertex_lock_rule`, not exact). Use the engine's loaders, not Lock counts.
- **No guest page faults or write-watches on the Switch:** 1-4 % of fault round trips corrupt NEON registers.
- **EDRAM-layout hacks fail while EDRAM is emulated:** forcing one predicated tile gave bands in Banjo because the
  engine placed post-process surfaces for two tiles. Turn tiling off only together with native surfaces.
- **NVK's per-call costs on the A57:** pipeline bind about 3 µs, draw 2.4-2.9 µs, descriptor/UBO set 1.3-1.6 µs a
  draw. Fewer binds pay on every draw; shader objects (`VK_EXT_shader_object`) are worth measuring.
- **ZCULL and depth-clear changes can lose the GPU:** UnleashedRecomp lost the GPU context (`VK_ERROR_DEVICE_LOST`
  after 15-20 minutes) with depth-clear changes while ZCULL planes were in use; its four round-10 renderer changes are
  off in its final build for that reason. Test such changes on long console runs.
- **Exactness needs a reference you can run.** Keep the emulated path runnable behind its key until the console A/B
  and the verify route have both decided; every Banjo enhancement has a key, a master switch group
  (`app/src/cpu/banjo_enhancements.cpp`), a verify check and an in-run A/B.
- **Measure what the GPU really spends.** Banjo's once-a-frame stencil rebuild was 8.6 ms for about 100 tiles; the
  trace showed it existed only because Direct3D's Clear of a small rectangle went through a 1x view over the 2x scene
  depth. A frame trace (`banjo_trace_frame_s`) before optimising saved a week of guessing.

### Bringing an API-level renderer from another port (Banjo took Renut's, 2026-10-17)

Banjo now carries Renut upstream's Direct3D-level renderer next to its own, chosen at boot
(`docs/renut-renderer-port.md`). What any port doing the same will meet:

- **Check the licence first.** Unleashed and Marathon are GPL-3.0: their renderers can be reused under its terms, with
  credit. Renut has no licence file; Banjo uses its renderer because banjo-nx started from Renut and with the
  maintainer's decision, crediting Renut and masterspike52. Another port should ask masterspike52 before copying
  Renut's code; Unleashed's and Marathon's code and banjo-nx's own Switch and tuning work are the safe base.

- **Hooks are named in one project and addressed in the other.** Rewrite them by address with a tool, and make every
  hook a dispatcher that runs the chosen renderer's function; where both projects hook the same address, one hook calls
  the other (Banjo: 361 hooks, 34 shared). Keep your changes to the imported files as one patch the import re-applies.
- **Two Vulkan users in one Switch executable.** The SDK calls Mesa's static NVK entry points directly; volk's function
  pointers have the same names, and plume's VMA collides with the SDK's (a different version). Rename both in plume
  (`plume_volk_names.h`, `plume_vma_names.h`). Same for Dear ImGui: one static copy.
- **plume for the Switch** is UnleashedRecomp-NX's patches (`plume.patch`, `plume-switch-perf.patch`, `volk.patch`),
  written against an older plume: merge them three-way onto the version the renderer needs, stub LSFG.
- **The translator is a host tool.** Cross builds take the shader cache a PC build generated; embed the compressed
  SPIR-V with `.incbin` (GCC needs minutes and gigabytes for a 4 MB initializer list).
- **Port the measured items, not the architecture.** Most of a PM4 renderer's optimisations are about work an
  API-level renderer never does. What carried over in Banjo: constants in uniform buffers (NVK's constant banks
  instead of global loads), `select()` for `?:`, clamp bounds from the constant bank, thread placement (plain
  `std::thread`s start on the process's default core on libnx), a persistent pipeline cache, precise barriers, cached
  upload memory with explicit flushes, CRC/PMULL content hashes on ARM.

## 7. Where Banjo is (2026-10-16) and what it does next

| Step | State |
|---|---|
| 0 census | Direct3D writers done (`docs/d3d-native.md`); the surface setters found (`8227AB68`, `8227AF40`); the surface census next |
| transport | records for every writer, recorded command buffers as host lists, host kick queue (on, package 24) |
| A targets | package 26: stencil-replacing draws fetch only their partly covered tiles (`banjo_gpu_stencil_clear_partial`, A/B 57), the first step; native surfaces and tiling off next |
| B shaders | objects noted at the draw-time shader load; identification still from microcode |
| C textures | fetch constants, fingerprinted cache |
| D buffers | persistent arena with proofs (vertices, indices) |
| E draws | native draw path writes records (not objects yet) |

## 8. References

- UnleashedRecomp upstream: `UnleashedRecomp/gpu/video.cpp`, `video.h` at `5e8695a` (read with `git show
  5e8695a:UnleashedRecomp/gpu/video.cpp` in `C:\devkitPro\UnleashedRecomp-NX`, which carries the upstream history).
  The functions named above: `CreateDevice`, `CreateTexture`, `LockTextureRect` / `UnlockTextureRect`, `UnlockBuffer`,
  `CreateSurface`, `StretchRect` / `ProcStretchRect`, `ExecutePendingStretchRectCommands`, `Clear` / `ProcClear`,
  `FlushRenderStateForMainThread`, `CreateShader` / `FindShaderCacheEntry`, the hook list at the end of the file.
- MarathonRecomp upstream: `MarathonRecomp/gpu/video.cpp` at `bd9c0bb` (same method in `C:\devkitPro\MarathonRecomp-NX`):
  per-state `SetRenderState` hooks, `D3DDevice_BeginTiling` / `D3DDevice_EndTiling`, `BeginConditionalSurvey` stubs,
  `D3DDevice_BeginShaderConstantF4`.
- The Switch forks' own docs: `UnleashedRecomp-NX/docs/SWITCH-PERFORMANCE.md` (every Switch change with its switch),
  `SWITCH-PERFORMANCE-AUDIT.md` (why each keeps the image), `MarathonRecomp-NX/docs/SWITCH-OPTIMISATIONS.md`.
- banjo-nx: `docs/d3d-native.md` (census, device layout, the API layer plan), `docs/d3d-full-replacement.md`
  (records, command buffers, host submission), `docs/edram-removal-plan.md` (what the EDRAM model costs),
  `docs/enhancements-reference.md` (every key), `app/src/me/banjo_d3d_native.h` (the native draw path reading the
  device), `app/src/me/banjo_d3d_clear.h` (the Clear and Resolve markers, the surface size word).
