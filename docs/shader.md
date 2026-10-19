# Shaders

How Banjo's shaders get from the Xbox 360 to the Switch's GPU, how big they are next to UnleashedRecomp-NX's, what made
them big, what has been done about it, and how to measure them. Kept up to date with every shader change.

Last update: 2026-10-16, package 25 (six exact NAK-level changes; before it the twenty-first package: 16-byte bone rows,
predicated blocks without branches, NAK assembly capture). Related pages: [enhancements-reference.md](enhancements-reference.md)
(every key; the shader keys are under "Shader package"), [enhancements.md](enhancements.md).

## Where they come from

1. **The game's shaders.** Nuts & Bolts ships Xenos microcode in shader containers. The PC build dumps them
   (`dump_guest_shaders = true`, and `banjo_volcar_shaders_en_sitio = true` for the in-place ones); the Switch build
   writes the ones its library lacks to the SD card (`banjo_shader_misses`), which `tools/wrap_discovered.py` wraps
   into containers. All of them live in `out/shaders_prev/in` (1,054 containers, 852 distinct shaders in the twentieth
   package).
2. **Translation.** `tools/build_shader_library.sh` runs, per container:
   - XenosRecomp (`shaders/XenosRecomp/shader_recompiler.cpp`, built with `MASSEFFECT_RECOMP`), which writes HLSL with
     `shaders/XenosRecomp/shader_common.h` inside it;
   - `tools/early_alpha.py` (the early alpha copies, `banjo_gpu_early_alpha`);
   - DXC: shader model 6.6, HLSL 2021, SPIR-V for Vulkan 1.2;
   - `me_pack_shaders`, giving `out/shaders/banjo_shaders.mesp` + `.idx`.

   Copy both files next to the NRO.
3. **On the console.** NVK turns each pipeline's SPIR-V into NIR and NAK compiles it when the pipeline is created:
   ~1 s for the heavy material shaders, ~80 ms for a typical one. The prewarm list (`masseffect_native_pipelines_prewarm`,
   in `cache/`) recreates last session's pipelines at start-up; the Mesa cache keeps the compiled code.

**Shader numbers.** "PS n536" in a log is the shader's position in the library. The library is sorted by the XXH3-64 of
each container, so adding shaders shifts the numbers after them: package 19's PS n536 is package 20's n538. Use
`python tools/shader_numbers.py` to map a number to a container (with `--without` for the containers added after the
log's package). The prewarm list names shaders by fingerprint, so it survives the shift.

## Specialization constants

A pipeline's shaders are specialised when it is created; the driver then folds every test on these constants away.

| Constant | Bits | Meaning | Set by / key |
|---|---|---|---|
| 0 | 0 | R11G11B10 normals | renderer |
| 0 | 1 | alpha test | renderer |
| 0 | 2 | vertex shaders' predicated blocks as branches (old form) | set when `banjo_gpu_pred_select` is off (A/B 37) |
| 0 | 3 | old short-circuit `?:` / `&&` forms | set when `banjo_gpu_branchless_select` is off (A/B 30) |
| 0 | 4 | pixel shaders' predicated blocks as branches (old form) | set when `banjo_gpu_pred_select_ps` is off (A/B 38) |
| 0 | 5 | texcoords never swapped | `banjo_gpu_texcoords_unswapped` |
| 0 | 6 | no texture of the draw has signs | `banjo_gpu_no_texture_signs` |
| 0 | 7 | old word-by-word bone fetches | set when `banjo_gpu_vfetch_block` is off (A/B 29), on vertex shaders with indexed fetches, and on a draw whose bone buffer the renderer cannot place 16-byte aligned |
| 0 | 8 | constants from UBOs (constant banks) | renderer, always |
| 0 | 9 | 1/texture size from the shared constants | renderer, always |
| 0 | 10, 11 | blend skip (alpha, zero) | `banjo_gpu_blend_skip` |
| 0 | 12 | structured control flow | `banjo_gpu_structured_cf` |
| 0 | 13 | vertex inputs in their own order | `banjo_gpu_identity_remap` |
| 0 | 14 | pixel shader without colour writes | renderer |
| 0 | 15 | Mass Effect's cheap PCF | renderer; **no Banjo shader tests it** (the console's heavy draws have it set, with no effect) |
| 0 | 16-18 | alpha test function | renderer |
| 0 | 19 | vertex index unused | `banjo_gpu_index_base_held` |
| 0 | 23 | early alpha | `banjo_gpu_early_alpha` |
| 0 | 20, 21, 22, 24-31 | renderer-only variants (early Z copy, float24 depth, raster grid, 7e3 targets, rectangles) | renderer |
| 60 | one per location | input locations the pipeline's pixel shader reads (TEXCOORD0-15 = 0-15, COLOR0-5 = 16-21; 0 without one) | `masseffect_native_vs_pruned_outputs` (Banjo's shaders read it from package 20) |
| 61 | one per fetch constant | fetch constants whose texture has an effective packed sign (2 or 3) in this draw | `banjo_gpu_texture_signs_per_slot` (on; off in package 20e only); kept in the pipeline key's `no_signs` word |
| 1000 + N | value 0 / 1, or 2 | bool constant N (0-255): its value in this pipeline, or 2 = read at run time | `banjo_gpu_bool_spec` (A/B 31; on: off in package 20e only); the values in the pipeline key's `bools` word, the list of N from the module's SpecIds |

With `banjo_gpu_spec_per_stage` (2026-10-15) each stage is given only what its module reads: constant 0 reduced to
the bits the module tests (the package's vertex shaders use 3, 5, 7, 13, 19; its pixel shaders 1, 3, 6, 10, 11, 16-18,
23), 60 / 61 / the bools only if declared. NVK caches each stage on its own, so a vertex shader is no longer compiled
again for every pixel shader's light combination and alpha function.

Bits 3 and 7 are "old form" bits: clear (the default) means the new code. That way, with the keys on, pipelines keep the
keys recorded in older prewarm lists, and the prewarm thread re-applies the current rule to old records.

## Size

Unleashed is the yardstick (its session's measurements). Its numbers are NAK instructions as the driver reports them.

| | Unleashed | Banjo, package 19 (console) | Banjo, package 20 |
|---|---|---|---|
| Pixel shaders, median | 554 | | |
| Pixel shaders, heaviest | 820-963 (56-80 GPRs) | 2,300-3,100 (PS536 family; 40-48 GPRs, no spills) | package 21b: 824 (n538), 1,065 the most of any specialised pipeline (see below) |
| Vertex shaders, median | ~270 | | |
| Vertex shaders, heavy | 274-344 (main material) | 1,346-1,420 (skinning: n58, n620, n690, n768; 16,500 static cycles, 48-56 GPRs) | package 21b: 478-508 (skinning n58, n692; 56 GPRs) |
| Translated statement -> NAK instructions | ~1-2 | ~7 | |

Offline counts (`tools/shader_size.sh`: specialised and optimised SPIR-V, with the console's constant 0 = `0x0028A320`):

| Shader | Package 19 shape | Package 20 | Change |
|---|---|---|---|
| PS536 (`ps_d93d4dbdf1bb78aa9`) | 3,325 instructions, 85 branches | 1,980, 37 | -40 % |
| PS447 (`ps_d4db869530f82d99b`) | 3,283, 85 | 1,944, 37 | -41 % |
| VS n768 (`vs_d31c562b4e5fd5f9c`) | 2,511, 100 | 1,281, 4 | -49 % |
| VS n620 (`vs_b814d7237e615990`) | 2,434, 100 | 1,212, 4 | -50 % |
| VS n58 (`vs_12399ee21a0440ae`) | 2,341, 100 | 1,119, 4 | -52 % |
| VS n690 (`vs_d784784b3f956adbf`) | 2,478, 100 | 1,249, 4 | -50 % |

Twenty-first package (2026-10-16), same method: the skinning shaders with the 16-byte bone rows, then with
`banjo_gpu_pred_select`. Package 20e's numbers are n770 = package 19's n768, n692 = n690.

| Shader | Package 20 | 16-byte rows | + predicated blocks flattened |
|---|---|---|---|
| VS n770 (`vs_d31c562b4e5fd5f9c`) | 1,281, 4 branches | 851, 4 | 824, 1 |
| VS n620 (`vs_b814d7237e615990`) | 1,212, 4 | 782, 4 | 760, 1 |
| VS n692 (`vs_d784784b3f956adbf`) | 1,249, 4 | 819, 4 | 790, 1 |
| VS n58 (`vs_12399ee21a0440ae`) | 1,119, 4 | 689, 4 | 671, 1 |
| VS n414 (`vs_d40448f8fe22b27f7`, procedural, 50,688 vertices) | | 731, 15 | 708, 10 |
| PS536 (`ps_d93d4dbdf1bb78aa9`, unspecialised) | 1,980, 37 | | 1,950, 19 (`banjo_gpu_pred_select_ps`) |

The pixel shaders' figures assume no fetch constant has signs. In package 19, the console ran ~0.8 NAK instructions per
counted SPIR-V instruction in these pixel shaders and ~0.57 in the vertex shaders. Those ratios project PS536 to
~1,550 and the skinning shaders to ~700. Both are still above Unleashed: see "What remains".

PC, NVIDIA's own compiler (`banjo_diag_shader_stats` on an RTX 3060; run A = package 19's library with the keys off,
run B = package 20's, the console's `renut.toml`, the intro route into Spiral Mountain, 2026-10-15). Binary size is
the machine code (16 bytes an instruction on this GPU):

| | Run A | Run B | Change |
|---|---|---|---|
| PS536 | 32,768 bytes, 48 registers | 18,432, 45 | -44 % |
| PS447, PS555, PS765 | 31,488 each, 47-48 registers | 17,920-18,048, 48 | -43 % |
| All 420 pixel shaders seen in both | 9.69 MB | 5.85 MB | -39.7 %, registers +0.2 % |
| All vertex shaders seen in both | 2.55 MB | 2.58 MB | +0.9 %, registers +5.2 % |
| Skinning VS n620, n58, n690 | 9,088-10,240 bytes, 46-48 registers | 9,216-10,368, 50-52 | about the same size, +3-4 registers |

NVIDIA's compiler already turns short branches into predication, which NAK does not; so the branch work (bone
fetches, selects) shows no gain here and should show more on the console. The extra vertex shader registers (loads now
in flight together, both arms of a select live) are the cost to watch there: the console A/B configs of targets 29 and
30 decide those two keys.

In run B the heavy material draws still kept the sign code in fetch constant 15: the 2048x2048 k_16_FLOAT shadow atlas,
whose four components have sign 1 ("signed"). The sign code returns a sign-1 component as sampled, so only signs 2
(biased) and 3 (gamma) count now (`kSignsEffective`), for `banjo_gpu_no_texture_signs` too: in run D every draw got the
whole-draw bit (367,840 a 10 s report) and the per-slot form had nothing left to do.

**Bool constants per pipeline** (`banjo_gpu_bool_spec`, run D). PS536 tests 14 bools (twelve light blocks, two output
paths). Offline: 1,980 instructions with the bools read at run time, 879 with every bool off, 1,813 with every bool on,
1,446 and 14 branches with its real values in the level (`0x2BF4`: 9 of 14 on). NVIDIA's compiler, the first
specialised pipeline of each pixel shader against its generic one (348 shaders): -33.1 % machine code, -7.4 %
registers; specialised pixel shaders median 640 instructions, 90th percentile 760, max 872 (generic: 864 / 976 / 1,184).
PS536: 15,104 -> 11,904 bytes. A run makes about 1,100 specialised pipelines over ~470 shader pairs (2.25 a pair, 9 at
most), compiled by a background thread while their draws use the generic pipeline; the next session prewarms them.
In-run A/B on the PC (target 31): scene GPU 5.4 -> 5.2 ms a frame (the PC's GPU is mostly idle; the console decides).

So far, PS536 on NVIDIA: 32,768 bytes (package 19) -> 11,904 (-64 %).

The user's goal (2026-10-15): sizes "generally similar to Unleashed".

## What made them big, and what was done

| Source | Where | Cost | Fix | Key (A/B target) | Package |
|---|---|---|---|---|---|
| `max(a, a)` for every Xenos move (NIR makes it a multiply on NAK) | translator | ~1,000 operations over the library | written as `a` | (translator, always) | earlier |
| Alpha function as a 7-case runtime switch | `shader_common.h` | ~8 branches per alpha-tested PS | specialised, bits 16-18 | renderer | earlier |
| Per-fetch texture sign / gamma code, kept whenever one texture of the draw had signs | `tfetch*` | PS536: 31 fetches with a runtime test plus the gamma / bias code each | constant 61, a bit per fetch constant; only signs 2 and 3 count (sign 1 is fetched as sampled) | `banjo_gpu_texture_signs_per_slot` (27) | 20 |
| HLSL 2021 short-circuit `?:` / `&&` in predicate updates and kill tests (a branch each; NAK flattens nothing) | translator, `early_alpha.py` | PS536: 17 of 54 branches | `select()` / `and()` / `or()` (`banjoSel`, `banjoSel0`, `banjoAnd`, `banjoOr`) | `banjo_gpu_branchless_select` (30) | 20 |
| Bone palette fetches: a range test and a branch around every 32-bit load | `banjoVfetch` | 48 branches per vertex in the skinning shaders | clamped index + select, no branch; an empty buffer gets a readable address from the renderer | `banjo_gpu_vfetch_block` (29) | 20 |
| All 22 vertex outputs computed whatever the pixel shader reads (constant 60 passed but never declared) | translator | every vertex shader; depth-only pipelines the most | unread outputs set to 0 at the exit, their math folds away | `masseffect_native_vs_pruned_outputs` | 20 |
| Alpha tested only at the end of the shader | translator | whole shader for pixels that fail | early alpha copy | `banjo_gpu_early_alpha` (26) | 17 |
| Per-vertex / per-pixel tests constant for a draw | `shader_common.h` | small | bits 5, 6, 13, 19, 10-12 | sa2-nx GPU rounds 1-2 | earlier |
| Bool constants read at run time: every light block in the binary behind a uniform branch, registers for the largest path | `BANJO_BOOL` | PS536: 14 bools, 1,980 instructions | specialization constants 1000 + N with the pipeline's values; a new combination draws with the generic pipeline while a background thread compiles it | `banjo_gpu_bool_spec` (31) | 20 |
| Bone rows read a word at a time: four loads, four 64-bit addresses per row | `banjoVfetch` | 12 rows per vertex in the skinning shaders | one 16-byte load per row (`banjoVfetchAligned4`) where stride and offset are multiples of 4 words; the renderer makes the buffers 16-byte aligned and readable to the next 16 bytes, or keys the draw word by word | `banjo_gpu_vfetch_block` (29) | 21 |
| Predicated blocks (`if (p0)`) kept as branches: NVK flattens nothing with a load or more than 8 ALU operations, so each bone's loads waited for the previous bone | translator | skinning shaders: 4 branches, four memory latencies in a row per vertex; heavy pixel shaders: 37 branches | blocks of register arithmetic, static constants and bone fetches run unconditionally, results kept with `select()` (`banjoFlattenPredicated`) | `banjo_gpu_pred_select` (37), `banjo_gpu_pred_select_ps` (38) | 21 |

## What remains

Candidates, largest first. Each must stay exact. **The full list (110 candidates, with the console's evidence for each) is
[shader-translator-ideas.md](shader-translator-ideas.md) (2026-10-16 evening)**; the notes below are the older ones.

- **The CTAB-named bools** (`g_Booleans & NAME`): none left in the library (0 uses in the 1,054 containers translated
  on 2026-10-16); Banjo's shaders all use `BANJO_BOOL`, which is specialised.
- **The work itself.** With the bools specialised, PS536 is mostly the material's real work: three cascades of 8 PCF
  taps (only one runs per pixel), the lights that are on, the specular. Translation overhead left is small.
- **Bindless handle reloads.** Each sample rebuilds its texture and sampler handle (~10 NAK instructions per distinct
  handle per block; NIR merges them within a block only). The 24 shadow taps now sit in three blocks of 8 with
  nothing between them, so most merge now. Hoisting the handles to the top of `main` is the next step if the console
  numbers still show it.
- **Register pressure.** 40-48 GPRs in the heavy pixel shaders (48 warps per SM at most). Expected to drop with the
  branches gone; to be read from the console's statistics.
- **Bone fetch address math.** Done for the bone rows (one 16-byte load, package 21). Other indexed fetches
  (`k_32_32_32_FLOAT` at stride 9 in VS n414) still load word by word.
- **The water's tessellation.** Done (`banjo_gpu_tess_grid`, package 21): one invocation per grid point instead of
  per triangle corner, 25,344 -> 5,049 a draw.
- **Skinning repeated per pass.** The level skins the same meshes in the scene, the small pass and the shadow passes
  (PS n538's VS n770: 55,185 vertices in each). Skinning once per frame and reusing the result would need the vertex
  shader split at the bone blend and a buffer for its results; not started.
- **`clamp(rsqrt(x), FLT_MIN, FLT_MAX)`** (the Xenos RSQ clamp): two min/max per use, 55 in PS536. Exact as it is.

Found while reading, not changed yet (correctness, not size):

- `tfetch1D` does not strip the packed signs from the descriptor index. A 1D fetch of a texture with signs would index
  the wrong descriptor. Not seen in Banjo so far.
- `banjoVfetch` reads 3 words for `k_32_32_32_32` (format 35: `count = format - 32`), so `w` stays 1.0. It only matters
  for an indexed fetch of that integer format; bones are `k_32_32_32_32_FLOAT`.

## Measuring

- **Console, the driver's numbers.** `configs/banjo_diagnostics.toml` logs `[native] shader statistics
  (banjo_diag_shader_stats) PS nX with VS nY` for each pixel shader's first pipeline. Each line gives instruction
  count, static cycles, GPRs, max warps per SM and spills for both stages, plus the fetch constants that kept sign
  code. The same config gives the per-draw GPU time and fragments (`per-draw GPU time`), and the GPU time by pass and
  pixel shader.
- **Console, the driver's code.** `banjo_diag_shader_asm` (in `banjo_diagnostics.toml`: the heaviest pixel shaders of
  package 20e's run) writes NVK's final NIR and NAK assembly (after register allocation and scheduling) of each listed
  shader's statistics copy to `shader_asm/` next to the NRO, one file per pipeline. Use it to see which translator
  constructs the instructions come from, not only how many there are.
- **Offline, before a console run.** `tools/shader_size.sh <tag> <constant 0> <constant 61> <containers...>`
  (environment `PSIN` = constant 60) translates with the current translator and header, then counts the specialised
  SPIR-V. Example: `tools/shader_size.sh now 0x0028A320 0 ps_d93d4dbdf1bb78aa9`. Compare tags before and after a
  change. It is a proxy: NAK's count needs the console.
- **Which shader is it.** `python tools/shader_numbers.py 536` (or `--name ps_d93d4dbdf1bb78aa9`).
- **A/B on the console.** `configs/banjo_texture_signs_ab.toml`, `banjo_vfetch_block_ab.toml`,
  `banjo_branchless_ab.toml`, `banjo_bool_spec_ab.toml` (in-run, every 4 s, with the GPU A/B line).
  `banjo_shaders_old.toml` turns this round off for a whole run.
- **Bool values of a pipeline.** The statistics line says `bools 80002BF4`: bit 31 = specialised, bit i = the i-th
  bool (ascending) the pipeline's shaders test. To count it offline, give `tools/shader_size.sh` the values through
  `EXTRA_SPEC="1129:0 1130:0 1243:1 ..."`.

## Package 28 (2026-10-17): UnleashedRecomp-NX / MarathonRecomp-NX's smaller draws in Renut's translator

Two items of theirs, ported to Renut's translator (`thirdparty/reblue-XenosRecomp`; `tools/renut_patches/xenosrecomp.patch`
regenerated). The shader cache layout is 7 (`kShaderCacheLayout`, `g_shaderCacheLayout`): every entry has
`shaderFlags`.

| Item | In the translator | Count (4,881 shaders) | Off |
|---|---|---|---|
| Depth-only draws without a pixel shader (`banjo_gpu_depth_only`) | `shaderFlags`: `SHADER_FLAG_PIXEL_KILL` (a vector `Kill*` or scalar `Kills*`, the only `clip()` the translator writes), `SHADER_FLAG_PIXEL_DEPTH` (`SV_Depth` declared). Self-check in `main.cpp`: each flag must match the shader's own text (`clip(`, `SV_Depth`); a mismatch fails the build | 96 pixel shaders kill (4 of them also write depth), 17 write depth, 4,772 neither | key `banjo_gpu_depth_only` (A/B 11) |
| Trimmed pixel outputs (`banjo_gpu_output_trim`) | `oCN = banjoTrim(oCN, Nu);` for each declared colour output at every exit, after `alphaTest` and `banjoBlendSkip` (which read the outputs as made); specialization constant 901, 4 bits a target, set by the renderer from the pipeline key: the channels the write mask or the target's format leaves out, every channel of an output without a target, the alpha only when no written channel blends with it and alpha to coverage is off. NAK drops what computed only those channels. 902 is the test (`banjo_renut_diag_trim_poison`: 60000 instead of 0) | 11,085 exits | key `banjo_gpu_output_trim` (A/B 12) |

`tools/renut_early_alpha.py` reads the trims as comments (they come after the alpha test it copies): the same 3,663
copies as before (197 too little after the alpha, 76 unsupported).

What the routes played showed (PC, the console's settings, intro and Spiral Mountain): every depth-only draw without
an alpha test (about 42,000 per 10 s) uses one of 27 shadow-caster pixel shaders that kill: the game's alpha cut-out
(one fetch of a mask, a compare with `c0.x`, the kill, then a constant colour). They keep their pixel shader, and it is
already about the smallest a kill allows; `banjo_gpu_output_trim` removes nothing from them either (their colour is a
constant). So `banjo_gpu_depth_only` drew 0 draws without their shader there; it stays on for any draw elsewhere that
qualifies. The trim's draws are the 26,000-30,000 per 10 s with part of a target's channels written.

## Package 27 (2026-10-17): banjo-nx's translator items in Renut's translator

The user's mandate: every shader translator optimisation of banjo-nx's carried over to Renut's translator
(`thirdparty/reblue-XenosRecomp`, its changes in `tools/renut_patches/xenosrecomp.patch`). Counts over Renut's library
(4,881 shaders: 3,936 pixel, 945 vertex); all exact.

| banjo-nx item | In Renut's translator | Count | Off |
|---|---|---|---|
| moves (`MAX a, a`) | written as `a`, vector and scalar | 38,482 | (always) |
| `banjo_gpu_bool_spec` (+ `_direct`) | `banjo_bool_spec.h`: each `BOOL_BIT(N)` a shader tests is specialization constant 1000 + N (default 2: read at run time, the generic pipeline is the old shader); the renderer's pipeline key holds the bool words, prebuild workers make the specialised pipeline, the generic one draws meanwhile | 34,660 constants in 3,627 shaders; PC route about 110,000 of 226,000 draws in a 10 s window specialised | `BANJO_RENUT_BOOL_SPEC=0`; key `banjo_gpu_bool_spec` (A/B 7) |
| `banjo_gpu_vfetch_block` (bone rows) | a memory fetch at whole rows (offset and stride multiples of four words) as one 16-byte load (every fetch copy is 16-byte aligned) | the skinning vertex shaders: 12 aligned 128-bit loads, no word loads | `BANJO_VFETCH_ROWS 0` |
| `banjo_gpu_bone_padding` (fetch without a branch) | the predicated-block pass converts blocks with memory fetches too, as `vfetchMemoryClamped` (the address kept inside the draw's fetch data, SharedConstants word 972) | 2,844 fetches | `BANJO_RENUT_PRED_FETCH=0` |
| `banjo_gpu_literals_cbuf` | a literal component NAK cannot encode (low 12 mantissa bits) read from the shader's own constant register; the renderer writes each shader's literal values into those registers (`g_shaderLiterals`), as the console's Direct3D does at SetShader, so no per-draw check | 53,190 components in 3,954 shaders | `BANJO_RENUT_LITERALS_CBUF=0` |
| `banjo_gpu_blend_skip` | `banjoBlendSkip(oC0)` at each exit, mode from specialization constant 900 (one UNORM target, no depth or stencil write, a blend that keeps the destination for a zero source; never for NaN) | every pixel shader | key `banjo_gpu_blend_skip` (A/B 8) |
| `banjo_gpu_early_alpha` | `tools/renut_early_alpha.py`: banjo-nx's `tools/early_alpha.py` on Renut's HLSL between two translator runs (dump, then `BANJO_HLSL_OVERRIDE`; a file that does not compile keeps its own text); self-checked against the original on random inputs | 3,663 of 3,936 pixel shaders (197 too little after the alpha, 76 forms the tool does not read, 0 differed) | CMake `BANJO_RENUT_EARLY_ALPHA=OFF` |
| `banjo_gpu_tess_grid` | `renutVertexRegisters` kinds 3 and 4: the tessellated draw indexed by grid vertex | PC route: 3.0 M grid vertices for 15.2 M corners per 10 s | key `banjo_gpu_tess_grid` (A/B 9) |
| `banjo_gpu_robustness_off` | Renut's pipelines without buffer and image robustness (`VK_EXT_pipeline_robustness` in plume) | all | key |

Nothing to do in Renut's translator (checked over the whole library): `banjo_gpu_structured_cf` (no shader in the
`switch (pc)` loop form), `banjo_gpu_texcoords_unswapped` (no swap), `banjo_gpu_no_texture_signs`,
`banjo_gpu_texture_signs_per_slot` and `banjo_gpu_sign_mask_skip` (no sign or gamma code in the fetches: the host
formats do it), `banjo_gpu_identity_remap` (the input assembler), `banjo_gpu_index_base_held` (938 of 945 vertex shaders
read the index), `banjo_gpu_spec_per_stage` (Renut compiles its stages linked), unwritten and pruned vertex outputs (the
linked compile drops them). NAK's sizes of Renut's shaders need the console.

## Package 26 (2026-10-16/17)

**banjo-nx's translator.** `banjo_gpu_literals_cbuf` (A/B 56): a shader's literal constants that NAK cannot encode as
immediates (any of the low 12 mantissa bits set: the shadow taps' Poisson offsets, 107-117 moves in the in-place
material shaders) are read from the constant registers Direct3D loaded them into, a free constant-buffer operand. The
container's literal table is parsed (`ParseLiterals`, `masseffect_native_shaders.cpp`); the renderer compares those
registers with the definitions at every draw and takes the old form (`SPEC2_LITERALS_IMMEDIATE`, bit 4 of spec2) for a
draw where one differs. PC, intro route: 207,956 draws in 10 s, 13,552 in the old form.

**Renut's translator** (`thirdparty/reblue-XenosRecomp`, used by Renut's renderer, `docs/renut-renderer-port.md`): three
of banjo-nx's shader items carried over, all exact.

| Change | What |
|---|---|
| Constants through uniform buffers | the float constants and SharedConstants come from dynamic UBOs (sets 5-7) instead of `vk::RawBufferLoad` through 64-bit pointers: NVK's constant banks instead of NAK global loads (banjo-nx's renderer has had this since MASSEFFECT_UBO) |
| Branchless select | the scalar predicate updates and the kill tests as `select()`, not HLSL 2021's short-circuit `?:` |
| Clamp bounds from the constant bank | FLT_MAX from SharedConstants' word at 1340 (FLT_MIN its negation) for the RCP / RSQ / LOG clamps |
| Branchless vertex fetch swap | `vfetchSwap` selects among the three endian swaps instead of an if-chain per word (skinning vertex shaders 178 -> 25 conditional branches with the row below, SPIR-V -32 %) |
| Branchless helpers | the alpha test's function chosen with `select()` instead of a `switch` (one `OpSwitch` a test); `rcpf`/`rsqf` and the texture scale decode without `?:` |
| Predicated blocks without a branch | `banjo_pred_select.h`: `if (p0)` blocks of register arithmetic become `select()`s under the predicate taken at entry (244,644 blocks; heavy pixel shaders 231 -> 80 and 415 -> 93 conditional branches, -17 to -20 % SPIR-V) |

Renut's translator dumps its HLSL with a fourth argument or `XENOS_RECOMP_HLSL_DUMP` (`XenosRecomp <in> <out.cpp>
<shader_common.h> <dump dir>`); the containers are game data (scratch only).

## Package 25 (2026-10-16 night): six exact changes from the console's NAK assembly

Built from items 3, 4, 5, 15, 16 and 61 of [shader-translator-ideas.md](shader-translator-ideas.md), each with its key
and A/B (49-54, all together 55; `docs/enhancements-reference.md`, "Package 25's shader round"). The translator's old
forms are bits of a new specialization constant 2 (`SPEC2_*`), in the pipeline key's `spec2` word.

| Change | NAK instructions it removes (from package 24b's assembly) |
|---|---|
| Clamp bounds from the shared block (`banjo_gpu_clamp_cbuf`) | the two `mov` of ±FLT_MAX per RCP / RSQ / LOG clamp: 19 in PS n538, 12-19 in the n112 family, 27 in PS n173 |
| Robustness off (`banjo_gpu_robustness_off`) | NVK's two `imnmx` index clamps per texture handle: 20 in n538 / n112, 18 in n173 |
| No sign mask (`banjo_gpu_sign_mask_skip`) | our `lop2.and 0xffffff` per handle: 10 in n538 / n112, 9 in n173 |
| Unread outputs not written (`banjo_gpu_vs_outputs_unwritten`) | the zero stores: 16 of VS n58's 23 `ast`, 21 of the pass-through VS n645's 22 |
| Bone padding (`banjo_gpu_bone_padding`) | the bounds selects of indexed fetches: 48 `sel` a vertex in the skinning VS, 34 in the water VS n414 |
| EDRAM import checks on the CPU (`banjo_gpu_edram_checks_cpu`) | four texture queries a pixel a pass in the multisample stencil imports |

Expected: PS n538 824 -> ~775 (-6 %), the n112 family ~-5 %, the water PS n173 472 -> ~420 (-11 %), the skinning VS
n58 441 -> ~375 (-15 %). The console's `banjo_diag_shader_asm` / `banjo_diag_shader_stats` of package 25 will say.

Also in this package: the flattening pass allows literal registers (`cN`), which flattened the in-place material
shaders' light blocks (`ps_i1a1a53fc5f5efc71` offline: 25 -> 19 conditional branches, 12 -> 6 phis). Offline SPIR-V
counts barely move for the six changes (they are operand forms NAK handles, not SPIR-V instructions). The library is
57.0 MB (package 21's: 49.2 MB): every module carries both forms of each change until the pipeline folds one away.
PC checks: `banjo_cpu_verify` 240 s with the console settings, 0 differences; the six forms active in every draw
(robustness available on the PC); in-run A/B 55 recorded for the picture.

## Package 24b on the console (2026-10-16 evening: play, diagnostics and fragments runs)

The shaders are package 21's (unchanged since); what is new is the NAK assembly of the 15 heaviest pixel shaders
(`banjo_diag_shader_asm`, the fragments run's `shader_asm/`) and NAK's statistics of what this run drew:

- Pixel shaders drawn (105 pipelines): median 737 instructions, 90th percentile 894, max 1,401 (PS n139); no spills;
  59 run at 64 warps an SM, 30 at 48, 16 at 40 (the 48-GPR ones). Vertex shaders (66): median 120, 90th 478, the
  outlier VS n335 still 3,261.
- **Where the heavy pixel shaders' instructions go** (fragment NAK assembly, the material family n538 / n449 / n557 /
  n767 at 793-824 instructions): 27-31 texture fetches; floating-point math 44-51 %; **moves 14-25 %** (113-198 a
  shader: the four coordinates of each fetch put into consecutive registers, and constants that two operands of one
  instruction both need); integer 6-15 % (the bindless texture handle: an index from the constant buffer, a 24-bit
  mask, a clamp, a shift, a load from the sampler and texture tables, a mask and an or, shared between fetches of the
  same texture); selects 5-7 % (the flattened branches); 11-23 transcendental operations.
- GPU time by pass in the diagnostics run (10 s, about 17 fps): resolves and copies 596 ms (3.5 ms a frame), then the
  scene passes led by PS n538 (362 ms, 2.1 ms a frame) and **PS n292** (354 ms: a 59-instruction shader, so a fill
  cost: a large area, not its code), n201 275, n743 210, n252 208, n569 141.

What follows for the size work: the moves are NAK's register allocation for vector operands and are not in our
hands directly, but every texture fetch removed takes its four moves and its handle arithmetic with it; PS n292 is a
fill-rate item (how many pixels it covers), not a size item.

## Package 21b on the console (2026-10-16 diagnostics run, title screen and menus)

NAK's statistics of every pipeline the run compiled (`banjo_diag_shader_stats`), the shaders of package 21 (16-byte bone
rows, predicated blocks flattened in vertex and pixel shaders, the water as a grid):

| | Unleashed | Banjo 21b |
|---|---|---|
| Pixel shaders, specialised pipelines (492) | median 554 | median 775, 90th percentile 940, max 1,065 |
| Pixel shaders, generic pipelines (65) | | median 43 (the small utility shaders), max 1,401 |
| Vertex shaders (174) | median ~270 | median 146, 90th percentile 520 |
| PS n538 (the material family) | | 824 instructions, 48 GPRs (package 19: 2,300-3,100) |
| PS n449, n557, n201, n401 | | 798 / 793 / 803 / 787, 32-48 GPRs |
| Skinning VS n58, n692 | 274-344 (main material) | 478 / 508, 56 GPRs (package 19: 1,346-1,420) |
| Water VS n414 (now a grid) | | 401, 32 GPRs |

So the pixel shaders are within about 40 % of Unleashed's median (they were 4-5 times it in package 19) and the
skinning vertex shaders within 1.5-2 times its heavy material shaders. The register counts of the skinning shaders (56)
are the next thing to watch: both arms of the flattened selects and the bone loads are live together.

One outlier: **VS n335** (with PS n183, a rarely drawn effect: not among the 25 costliest pixel shaders by GPU time),
3,261 instructions, 199,913 static cycles, 40 spills to registers and 320 bytes of local memory (an indexed register
array: the translator's dynamically indexed temporaries go to local memory). Nothing to gain in frame time while it is
this rare; a candidate if a level draws it often.

The NAK assembly of the listed shaders (`banjo_diag_shader_asm`, `shader_asm/` next to the NRO) came back with package
24b's runs: see the section above.

## Package 20e on the console (level, 2026-10-16 diagnostics run)

Per-draw GPU time (serialized frames, so small draws are overstated) of the heaviest pixel shaders, with the vertex
shader of their costliest draw. The material family is a quarter of the level's GPU time; a third of it is the
skinning vertex shader, in the small pass (cat 5) where almost no pixels are shaded.

| PS | Scene pass (cat 2) | Small pass (cat 5) | VS |
|---|---|---|---|
| n538 (PS536 family) | 10.5-18.2 ms, 26 draws, 0.8-2.0 Mfrag, 9-13 ns/frag | 3.8-4.4 ms, 26 draws, 0.01-0.04 Mfrag | n770 (skinning) |
| n449 (PS447) | 2.6-7.4 ms, 13 draws | 1.3-1.6 ms, 0-0.02 Mfrag | n692 (skinning) |

The generic material pipelines of 20e (bools not specialised there) had 1,022-1,288 NAK instructions and 32 registers
(64 warps per SM, the most there is): occupancy is not what limits them, instructions and samples are.

## The heavy shaders (package 19, console, level)

| Shader | Container | GPU ms per frame | What it is |
|---|---|---|---|
| PS536 | `ps_d93d4dbdf1bb78aa9` | 41.8 scene + 6.3 small pass (per-draw frames) | material with three shadow cascades (8 taps each on s13-s15), 31 fetches, 12 nested light bools |
| PS447, PS555, PS765 | `ps_d4db869530f82d99b`, `ps_d031edaeddf7b5ebe`, `ps_d2bed25ae43917d33` | 8.2, 4.6, 5.4 | the same family |
| VS n768, n620, n690, n58 | `vs_d31c562b4e5fd5f9c`, `vs_b814d7237e615990`, `vs_d784784b3f956adbf`, `vs_12399ee21a0440ae` | 5.7-6.3 ms per frame for 55k vertices in the small pass, almost no pixels | 4-bone skinning (12 bone-row fetches) |

The level's small pass draws the same objects as the scene and shades almost no pixels (0.036 Mfrag), so its time is
the skinning vertex shaders.

## Notes for other ports

- With `-HV 2021`, never let the translator print a scalar `?:`, `&&` or `||` where `select()`, `and()` or `or()` will
  do: on NAK each is a branch.
- Per-fetch work that depends on per-draw data (signs, formats) is cheapest as a specialization constant per fetch
  constant, not per draw.
- Anything that reads raw buffers per word with a bounds check: clamp and select instead of branching.
- Check that every specialization constant the renderer passes is declared by the shaders; Vulkan silently ignores
  the ones that are not.

## deko3d GLSL: runtime component reads (2026-10-18)

uam (deko3d's compiler) loads `UBO.v[a][b]` with a non-constant `b` as component `.x` of the vector at that byte offset.
`tools/deko3d_glsl.py` rewrites those reads to `dk_pick_<type>(UBO.v[a], uint(b))`, which loads the vector and selects
the component (10 vertex shaders, 70 reads: the generic vertex fetch's slot lookup). The SPIR-V (Vulkan) is unchanged.
`depth_resolve.hlsli` now writes the stencil into the words' low byte (`banjo_renut_depth_words_stencil`): the depth of
field's character mask. See docs/deko3d-renderer.md section 12.

## deko3d / uam (package 33, 2026-10-18)

From package 33 the console renderer is Renut's on deko3d, whose shaders uam compiles (not NAK). The uam-specific work
(GLSL passes in tools/deko3d_glsl_opt.py, the alpha / tessellation / vertex-output constants 903-905, and uam's own PRMT
and shift passes) and its measurements are in docs/deko3d-shader-optimisations.md. The NAK items above apply to the
NVK back end only.
