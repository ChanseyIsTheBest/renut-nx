# Shaders

How the game's shaders get from the Xbox 360 to the Switch's GPU on the deko3d renderer, what the translator does to them,
and how to measure them. Kept up to date with every shader change.

Related pages: [deko3d-shader-optimisations.md](deko3d-shader-optimisations.md) (the GLSL passes and uam's own work),
[deko3d-renderer.md](deko3d-renderer.md) section 5 (the chain in detail) and
[enhancements-reference.md](enhancements-reference.md) (every key).

## Where they come from

1. **The game's shaders.** Nuts & Bolts ships Xenos microcode in shader containers. reNut's `tools/dump_shaders.py` reads them
   from `default.xex` and the bundles (4,731 containers). The ones the game makes at run time (about 150, in no game file)
   come from a capture and ship inside the NRO.
2. **Translation.** Renut's XenosRecomp (`thirdparty/reblue-XenosRecomp`, its changes in
   `tools/renut_patches/xenosrecomp.patch`) writes HLSL with `shader_common.h` inside it, `tools/renut_early_alpha.py` adds
   the early alpha copies, and DXC compiles it to SPIR-V with `-DBANJO_DEKO3D`.
3. **GLSL for uam.** SPIRV-Cross turns the SPIR-V into GLSL, and `tools/deko3d_glsl.py` and `tools/deko3d_glsl_opt.py` rewrite
   it for uam and optimise it.
4. **DKSH.** uam compiles each variant. The web installer does steps 1 to 4 in the browser from the player's own copy
   (`docs/deko3d-renderer.md`, section 30), giving `deko3d_glsl.bin` and `deko3d_shaders.bin`.
5. **On the console.** The NRO reads a compiled variant from `deko3d_shaders.bin`. A variant the file lacks is compiled by uam
   in the background while the draw uses the generic one (`docs/deko3d-renderer.md`, section 28).

## Specialization constants

uam has no specialization constants, so they are `#define`s: each pipeline's values make their own text and their own DKSH,
keyed by its FNV-1a 64.

| Constant | Meaning | Where |
|---|---|---|
| 900 | blend skip mode (off on this renderer) | [deko3d-renderer.md](deko3d-renderer.md) section 11 |
| 901, 902 | trimmed outputs, and their poison test | "Package 28" below |
| 903 | the alpha test's function | deko3d-shader-optimisations.md 2.3 |
| 904 | the tessellation kind | 2.5 |
| 905 | which vertex outputs the pixel shader reads | 2.6 |
| 906 | inputs the vertex shader writes as zero | 2.19 |
| 907 | the main body's texture region ahead of the early alpha test | 2.28 |
| 908 | texture sizes from a buffer | 2.32 |
| 909 | depth tested before a discarding shader | 2.33 |
| 910 | 32 registers for the listed shaders (off: an A/B) | 2.34 |
| 911 | a light's work in a branch on its reach (off: an A/B) | 2.39 |
| 912 | a measurement of the fur's vertex share | 2.40 |
| 913 | the fur skinned once a frame (an A/B) | 2.42 |
| 920-923 | a tessellated draw's fetch types | "The water's vertex fetch" below |
| 1000 + N | bool constant N: 0 or 1 in this pipeline, or 2 = read at run time | `banjo_gpu_bool_spec` |

## Package 28 (2026-10-17): UnleashedRecomp-NX / MarathonRecomp-NX's smaller draws in Renut's translator

Two items of theirs, ported to Renut's translator (`thirdparty/reblue-XenosRecomp`; `tools/renut_patches/xenosrecomp.patch`
regenerated). The shader cache layout is 7 (`kShaderCacheLayout`, `g_shaderCacheLayout`): every entry has
`shaderFlags`.

| Item | In the translator | Count (4,881 shaders) | Off |
|---|---|---|---|
| Depth-only draws without a pixel shader (`banjo_gpu_depth_only`) | `shaderFlags`: `SHADER_FLAG_PIXEL_KILL` (a vector `Kill*` or scalar `Kills*`, the only `clip()` the translator writes), `SHADER_FLAG_PIXEL_DEPTH` (`SV_Depth` declared). Self-check in `main.cpp`: each flag must match the shader's own text (`clip(`, `SV_Depth`); a mismatch fails the build | 96 pixel shaders kill (4 of them also write depth), 17 write depth, 4,772 neither | key `banjo_gpu_depth_only` (A/B 11) |
| Trimmed pixel outputs (`banjo_gpu_output_trim`) | `oCN = banjoTrim(oCN, Nu);` for each declared colour output at every exit, after `alphaTest` and `banjoBlendSkip` (which read the outputs as made); specialization constant 901, 4 bits a target, set by the renderer from the pipeline key: the channels the write mask or the target's format leaves out, every channel of an output without a target, the alpha only when no written channel blends with it and alpha to coverage is off. The compiler drops what computed only those channels. 902 is the test (`banjo_renut_diag_trim_poison`: 60000 instead of 0) | 11,085 exits | key `banjo_gpu_output_trim` (A/B 12) |

`tools/renut_early_alpha.py` reads the trims as comments (they come after the alpha test it copies): the same 3,663
copies as before (197 too little after the alpha, 76 unsupported).

What the routes played showed (PC, the console's settings, intro and Spiral Mountain): every depth-only draw without
an alpha test (about 42,000 per 10 s) uses one of 27 shadow-caster pixel shaders that kill: the game's alpha cut-out
(one fetch of a mask, a compare with `c0.x`, the kill, then a constant colour). They keep their pixel shader, and it is
already about the smallest a kill allows; `banjo_gpu_output_trim` removes nothing from them either (their colour is a
constant). So `banjo_gpu_depth_only` drew 0 draws without their shader there; it stays on for any draw elsewhere that
qualifies. The trim's draws are the 26,000-30,000 per 10 s with part of a target's channels written.

## Package 27 (2026-10-17): the translator's optimisations

The user's mandate: every shader translator optimisation of banjo-nx's carried over to Renut's translator
(`thirdparty/reblue-XenosRecomp`, its changes in `tools/renut_patches/xenosrecomp.patch`). Counts over Renut's library
(4,881 shaders: 3,936 pixel, 945 vertex); all exact.

| banjo-nx item | In Renut's translator | Count | Off |
|---|---|---|---|
| moves (`MAX a, a`) | written as `a`, vector and scalar | 38,482 | (always) |
| `banjo_gpu_bool_spec` (+ `_direct`) | `banjo_bool_spec.h`: each `BOOL_BIT(N)` a shader tests is specialization constant 1000 + N (default 2: read at run time, the generic pipeline is the old shader); the renderer's pipeline key holds the bool words, prebuild workers make the specialised pipeline, the generic one draws meanwhile | 34,660 constants in 3,627 shaders; PC route about 110,000 of 226,000 draws in a 10 s window specialised | `BANJO_RENUT_BOOL_SPEC=0`; key `banjo_gpu_bool_spec` (A/B 7) |
| `banjo_gpu_vfetch_block` (bone rows) | a memory fetch at whole rows (offset and stride multiples of four words) as one 16-byte load (every fetch copy is 16-byte aligned) | the skinning vertex shaders: 12 aligned 128-bit loads, no word loads | `BANJO_VFETCH_ROWS 0` |
| `banjo_gpu_bone_padding` (fetch without a branch) | the predicated-block pass converts blocks with memory fetches too, as `vfetchMemoryClamped` (the address kept inside the draw's fetch data, SharedConstants word 972) | 2,844 fetches | `BANJO_RENUT_PRED_FETCH=0` |
| `banjo_gpu_literals_cbuf` | a literal component the GPU cannot encode as a short immediate (low 12 mantissa bits) read from the shader's own constant register; the renderer writes each shader's literal values into those registers (`g_shaderLiterals`), as the console's Direct3D does at SetShader, so no per-draw check | 53,190 components in 3,954 shaders | `BANJO_RENUT_LITERALS_CBUF=0` |
| `banjo_gpu_blend_skip` | `banjoBlendSkip(oC0)` at each exit, mode from specialization constant 900 (one UNORM target, no depth or stencil write, a blend that keeps the destination for a zero source; never for NaN) | every pixel shader | key `banjo_gpu_blend_skip` (A/B 8) |
| `banjo_gpu_early_alpha` | `tools/renut_early_alpha.py`: banjo-nx's `tools/early_alpha.py` on Renut's HLSL between two translator runs (dump, then `BANJO_HLSL_OVERRIDE`; a file that does not compile keeps its own text); self-checked against the original on random inputs | 3,663 of 3,936 pixel shaders (197 too little after the alpha, 76 forms the tool does not read, 0 differed) | CMake `BANJO_RENUT_EARLY_ALPHA=OFF` |
| `banjo_gpu_tess_grid` | `renutVertexRegisters` kinds 3 and 4: the tessellated draw indexed by grid vertex | PC route: 3.0 M grid vertices for 15.2 M corners per 10 s | key `banjo_gpu_tess_grid` (A/B 9) |

Nothing to do in Renut's translator (checked over the whole library): `banjo_gpu_structured_cf` (no shader in the
`switch (pc)` loop form), `banjo_gpu_texcoords_unswapped` (no swap), `banjo_gpu_no_texture_signs`,
`banjo_gpu_texture_signs_per_slot` and `banjo_gpu_sign_mask_skip` (no sign or gamma code in the fetches: the host
formats do it), `banjo_gpu_identity_remap` (the input assembler), `banjo_gpu_index_base_held` (938 of 945 vertex shaders
read the index), `banjo_gpu_spec_per_stage` (constants are per variant here), unwritten and pruned vertex outputs (`vsout`,
`docs/deko3d-shader-optimisations.md` 2.6).

## Package 26 (2026-10-16/17): carried over first

| Change | What |
|---|---|
| Constants through uniform buffers | the float constants and SharedConstants come from uniform buffers instead of raw loads through 64-bit pointers: Maxwell's constant banks instead of global loads |
| Branchless select | the scalar predicate updates and the kill tests as `select()`, not HLSL 2021's short-circuit `?:` |
| Clamp bounds from the constant bank | FLT_MAX from SharedConstants' word at 1340 (FLT_MIN its negation) for the RCP / RSQ / LOG clamps |
| Branchless vertex fetch swap | `vfetchSwap` selects among the three endian swaps instead of an if-chain per word (skinning vertex shaders 178 -> 25 conditional branches with the row below, SPIR-V -32 %) |
| Branchless helpers | the alpha test's function chosen with `select()` instead of a `switch` (one `OpSwitch` a test); `rcpf`/`rsqf` and the texture scale decode without `?:` |
| Predicated blocks without a branch | `banjo_pred_select.h`: `if (p0)` blocks of register arithmetic become `select()`s under the predicate taken at entry (244,644 blocks; heavy pixel shaders 231 -> 80 and 415 -> 93 conditional branches, -17 to -20 % SPIR-V) |

Renut's translator dumps its HLSL with a fourth argument or `XENOS_RECOMP_HLSL_DUMP` (`XenosRecomp <in> <out.cpp>
<shader_common.h> <dump dir>`); the containers are game data (scratch only).

## The water's vertex fetch (2026-10-31)

`banjo_gpu_fetch_spec`, A/B 69. Nutty Acres' sea (PS
`9ED9E1A8E6A44361`, VS `F09B139EE9188255`) cost 18-19 ms of GPU a frame at 540p on the console (logs of 2026-10-30),
and the cost does not follow the resolution: it is the vertex shader.
- The console ran 94.7 M grid vertices per 10 s there (about 660k a frame at 14 fps).
- Each grid vertex ran 2,494 instructions: the generic bound fetch switches over fifteen Xenos formats per stream
  (rows 66-69 of the shared constants, `bound_fetch[k][3]`), with the patch-kind code (904) read at run time too.
- That is about 1.6 G instructions a frame, roughly 14 ms at 460 MHz.
- The pixel shader is 530 instructions and 13-16 texture reads at 31-32 GPRs, full occupancy. Its 4 blended draws
  are not what costs.
- Pass `fetchspec` (`tools/deko3d_glsl_opt.py`) reads the type words through constants 920-923. A tessellated
  draw's key carries the draw's own words and patch kind (`PipelineKey::fetch_types`, `tess_kind`; pipeline list
  version 5).
- Result: the water vertex shader drops 2,494 -> 411 instructions (25 -> 16 GPRs). The 8 vertex shaders with the
  generic fetch are the only ones it touches: 3 in the NRO's package, 5 in the installed one
  (`tools/deko3d_glsl_patch.py` patches an installed package in place).
- Exact: the constants hold the values the shader would read. The pipeline caches that skip `MakeKey` compare them
  (`FetchSpecDiffers`).
- First time a key is seen, the draw uses the generic pipeline while the specialised one is built. The first
  emulator run kept the generic water for 6 minutes: the job's vertex variant was queued as a background compile,
  behind the pipeline list's compiles and their CPU-share rests. Since then, a job a draw asked for
  (`Job::for_draw`) goes to the front of the prebuild queue. Its variants are queued as a draw's
  (`plume::Deko3DSetDrawVariants`). A variant already in the shader cache file is a read, ahead of every compile.
- 2026-11-01, the user's first console run with it (NRO b37ed58bb667): the sea still cost 18.4 ms a frame. The
  specialised water variant never compiled in 6 minutes. The uam thread compiled 19 variants in the whole session
  (about 5 s each, then rests of up to 30 s: 210 s of rests), behind a backlog, and a draw's request still waits its
  turn there. Now a pipeline with fetch constants (`FetchSpecialised`) is built with its own variants compiled on
  the prebuild worker itself (`plume::Deko3DSetUrgentVariants`): a compile that thread waits for, which goes ahead of
  every background one and ends their rest. The draws use the generic pipeline meanwhile. The log says
  "fetch spec (banjo_gpu_fetch_spec): VS ...'s pipeline ... built in N ms".

## Measuring

- **Where the GPU's time goes.** `renut_gpu_profile = 10` gives the `GPU profile` lines, and `banjo_renut_diag_gpu_shaders`
  splits them by pixel shader.
- **What uam makes of a shader.** `tools/deko3d_shader_stats.py` builds the variants the pipeline lists use, compiles them with
  the PC uam and reads the DKSH: instructions, GPRs, scratch and opcodes by class
  ([deko3d-shader-optimisations.md](deko3d-shader-optimisations.md), sections 1 and 5).
- **That a pass is exact.** `tools/difftest` proves GLSL passes bit for bit.
- **A/B on the console.** The in-run A/B targets and `configs/banjo_renut_*_ab.toml`
  ([enhancements-reference.md](enhancements-reference.md), "How to measure").

## deko3d GLSL: runtime component reads (2026-10-18)

uam (deko3d's compiler) loads `UBO.v[a][b]` with a non-constant `b` as component `.x` of the vector at that byte offset.
`tools/deko3d_glsl.py` rewrites those reads to `dk_pick_<type>(UBO.v[a], uint(b))`, which loads the vector and selects
the component (10 vertex shaders, 70 reads: the generic vertex fetch's slot lookup).
`depth_resolve.hlsli` now writes the stencil into the words' low byte (`banjo_renut_depth_words_stencil`): the depth of
field's character mask. See docs/deko3d-renderer.md section 12.

## deko3d / uam (package 33, 2026-10-18)

From package 33 the console renderer is Renut's on deko3d, whose shaders uam compiles. The uam-specific work
(GLSL passes in tools/deko3d_glsl_opt.py, the alpha / tessellation / vertex-output constants 903-905, and uam's own PRMT
and shift passes) and its measurements are in docs/deko3d-shader-optimisations.md.

2026-10-19/20 night (packages 36 and 36b): texture sizes from a buffer (txq), early depth tests (earlyz), the 32-GPR
A/B (gpr32), and three exact select / skinning passes (bonesel, aw4, guardsel): deko3d-shader-optimisations.md 2.32-2.37.

2026-10-20 (packages 36c-36g): the light-branch A/B (lightbranch, 911), the fur-only diagnostic (nullps, 912), 128-bit
bone rows (uam flag 1024, 36d) and the fur skinned once a frame (skinonce, 913, an A/B: the shells' vertex shader about
half as long, checked bit for bit by tools/difftest/difftest_vs_skin.py): deko3d-shader-optimisations.md 2.38-2.42.

## Host shaders added 2026-10-21: SMAA (banjo_renut_smaa)

`shaders/host/smaa_vs.hlsl`, `smaa_edge_ps.hlsl`, `smaa_weights_ps.hlsl` (medium preset), `smaa_weights_high_ps.hlsl`
(high preset), `smaa_blend_ps.hlsl` and `smaa.hlsli` (the bindings for app/thirdparty/smaa/SMAA.hlsl, MIT). Built like
the other host shaders (DXIL, SPIR-V, and DXC -> SPIRV-Cross -> uam for deko3d; tools/deko3d_host_shader.py); not
game shaders, so the DKSH cache and the variant tools do not see them. deko3d blob sizes (GLSL + DKSH header): vs 7 KB,
edges 28 KB, weights 66 KB (high 152 KB), blending 34 KB. docs/deko3d-renderer.md section 26.

## The package for scaled targets (2026-10-21, package 36z: 648p / 900p)

tools/deko3d_glsl_opt.py's default passes no longer include scale1 and fragscale (both made for 1x only) and start with
scalecodes (codes 4 and 6 of the scale-code chain become 0.9 and 1.25, render_scale.h's deko3d table). 2271 of 4881
shaders change: 2209 pixel shaders divide gl_FragCoord.xy by the render scale (shared word v[83].z) again instead of by
1.0, and the 168 with scale-code reads keep them. Package out/deko3d_shaders/glsl_opt_scaled, DKSH cache 57; the 36y
package is in out/deko3d_shaders/package_backup_36y. Host shaders added: `shaders/host/ui_upscale_ps.hlsl` (the 3D image
to the display's resolution, bilinear: one sample, the default) and `ui_upscale_bicubic_ps.hlsl` (Catmull-Rom, five), and `ui_depth_upscale_ps.hlsl` (the scene's depth
to the display's resolution for the menus' 3D icons drawn after the composite: nearest, SV_Depth). docs/deko3d-renderer.md section 27.
36z2: scalecodes reads the heights instead of constants: codes 4 and 6 are `float(v[84].w & 65535) / 720` and
`float(v[84].w >> 16) / 720` (SharedConstants pad_1352[1]: handheld | docked << 16), so any internal height needs no
new package. Package out/deko3d_shaders/glsl_opt_any, DKSH cache 58 (the 168 chain shaders' variants; the rest merged).
