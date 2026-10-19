# Native renderer plan (Banjo-Kazooie: Nuts & Bolts on Switch)

## Decision: nfsmw-style PM4 renderer, not Plume

Two families of "native renderer" exist among the reference ports:

| | Plume (UnleashedRecomp-NX, MarathonRecomp-NX) | PM4 ring renderer (nfsmw-nx) |
|---|---|---|
| Recompiler | XenonRecomp | **ReXGlue v0.10.0 (same as reNut)** |
| How it draws | Replaces the game's D3D functions with its own device; hooks CreateTexture, Lock, Set*, Draw* | Lets the game's own D3D run and reads the PM4 command ring it writes; draws each packet with Vulkan |
| Needs to know | Every D3D entry point and how the engine uses it | Only a handful of D3D hooks (shader creation, Draw* for shader pairing, Swap) |
| Fits games whose data embeds D3D resource headers | No: textures never pass through CreateTexture, SetTexture copies fetch constants directly | Yes |

reNut is a ReXGlue project, Nuts & Bolts is a 2008 XDK game with D3D statically linked and resource
headers baked into its bundles, and the nfsmw renderer already runs on this exact SDK and on NVK. So the port follows
nfsmw-nx's renderer and its order of work (`nfsmw-nx-main/docs/porting-another-game.md`).

## What carries over unchanged (generic)

- `nfsmw_nativo_sistema.*`: IGraphicsSystem replacement, MMIO range 0x7FC80000, vblank thread, ring thread, PM4
  parser (INTERRUPT, XE_SWAP, INDIRECT_BUFFER, WAIT_REG_MEM, REG_RMW, MEM_WRITE, EVENT_WRITE*, SET_CONSTANT*,
  DRAW_INDX*, IM_LOAD*), SCRATCH_REG mirroring (without it the game hangs in ~1 s).
- `nfsmw_nativo_destinos.*`: render targets as Vulkan images (no EDRAM), resolves, clears, presentation through the
  SDK presenter, gamma ramp.
- `nfsmw_nativo_dibujos.*`: vertex input from patched fetches, NEON untiling, texture cache + pool, constants in a
  dynamic UBO, pipeline cache + prewarm.
- `nfsmw_nativo_texturas_pool.*`, `nfsmw_nativo_vertices_dedupe.h`, `nfsmw_nativo_captura.*`,
  `nfsmw_shader_library.*` (format), `nfsmw_nativo_shaders.*` (logic).
- SDK changes it depends on are already in `sdk/` (nfsmw's): `vulkan_native_shader_features`, extra
  `VulkanDevice::Properties`, the whole Switch layer.

## What must be replaced for Banjo

1. **D3D hooks** (Banjo addresses, from `config/renut_gpu_funcs.toml`, to be confirmed by a trace phase):
   - `D3DDevice_CreateVertexShader` 0x8264E8B0, `D3DDevice_CreatePixelShader` 0x8264E6A8 (container in r3)
   - `D3DDevice_DrawVertices` 0x8222C7A0, `D3DDevice_DrawIndexedVertices` 0x826567C8,
     `D3DDevice_DrawVerticesUP` 0x8222E520, `D3DDevice_BeginVertices` 0x8222CC50
   - `D3DDevice_Swap` 0x82287590 (frame counter)
   - Bound VS/PS: nfsmw reads them from device offsets of the 2005 XDK (dev+0x4FE8, dev+0x3290). For the 2008 XDK
     either re-derive the offsets or hook SetVertexShader/SetPixelShader.
2. **2008 shader containers** (`0x102A11xx`). nfsmw only parses 2005 (`0x102A0E0x`) and converts them; XenosRecomp
   already speaks 2008. Touch points: scanner signature, packer (skip `Convertir2005`), `Validar`,
   `IdentificarCreacion`, `Leer` (2008 layout: shader header at +24, elements at +0x24+field18*4).
   Shader source for development: reNut's `dump_guest_shaders` hooks (whole pFunction blob from CreateVS/PS).
   For players: bundles are XCompress LZX (`0FF512ED`); decompress, then scan.
3. **Formats nfsmw never needed**: render targets/resolves beyond 8888 (2_10_10_10, *_FLOAT, 16_16_16_16), rect
   lists and point lists, 10_11_11 normals, textures 5_6_5 / 1_5_5_5 / 4_4_4_4 / CTX1 / DXT3A / etc.
4. **Predicated tiling + MSAA**: Banjo renders 720p with tiling (D3DDevice_BeginTiling 0x822788A0). Force one tile and
   no MSAA at engine level (reNut's `disable_msaa` hooks are a start; tile count must also be forced).
5. **Heuristics to turn off** (they fire on any game): vegetation drop (`nfsmw_sombras_sin_vegetacion`), shadow =
   depth target pitch >= 1600, 30 FPS guard, occlusion scale (reads NFSMW global 0x82A2D1AC), shader-hash hacks
   (inert), road reflection address.

## Files that must NOT be compiled into Banjo

Everything that hooks NFSMW game functions: `nfsmw_render_targets.cpp`, `nfsmw_espera_fotograma.cpp`,
`nfsmw_pegamento_nativo.cpp`, `nfsmw_d3d_registros_nativo.cpp`, the material/eview/matrices/escenario/efecto natives,
`nfsmw_recortes_carrera.cpp`, `nfsmw_sombras_lod.cpp`. A `REX_HOOK_RAW(sub_X)` whose address happens to exist in
Banjo would silently replace an unrelated Banjo function.

## Bring-up order

1. Banjo on PC with the emulated GPU. **Done** (title screen, ~27 fps on an RTX 3060, D3D12).
2. Same, built against `sdk/` (nfsmw's SDK) with Vulkan enabled.
3. Switch NRO with the emulated GPU (expect a few fps; validates the platform layer).
4. Trace phase: confirm each Banjo D3D address (adapt `nfsmw_d3d_trace.cpp`).
5. Native renderer in sink mode (no shader library): game runs, resolves/clears/swaps processed.
6. Shader library from dumped 2008 containers; enable shader pairing; add missing formats per "Rechazar" logs.
7. Performance work on the console.
