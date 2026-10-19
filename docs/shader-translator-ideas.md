# Shader translator improvements: 150 candidates (2026-10-16)

Candidates for the translator (`shaders/XenosRecomp`), `shader_common.h` and the renderer's side of the shaders,
found from the console's NAK assembly (package 24b's `shader_asm/`, 15 heaviest pixel shaders), NVK's source
(`src/nouveau/vulkan/nvk_nir_lower_descriptors.c` of the nfsmw Mesa) and the whole library translated offline (1,054
containers: 686 pixel and 368 vertex shaders). Every item keeps the picture exact unless it says otherwise; none
changes the driver. Each would get its own key, A/B target, check and docs like the others. Related: [shader.md](shader.md).

## Where the heavy pixel shaders' instructions go now

Counts from the NAK assembly (instructions as NAK prints them):

| | PS n538 family (n538, n449, n557, n767) | PS n112 family (n112, n116, n159, n167, n201, n252, n401, n658, n37) |
|---|---|---|
| Instructions | 790-824 | 659-818 |
| Texture fetches | 31 | 27-31 |
| Texture handles built (each 12 instructions) | 10 | 6-10 |
| Literal constants moved into a register | 8 | 107-117 |
| ±FLT_MAX moved into a register (the RSQ / RCP / LOG clamps) | 19 | 12-19 |
| The three shadow cascades (8 taps each, one runs per pixel) | 88 + 82 + 81 | 106-109 + 98-100 + 98-100 |

**A texture handle** is 12 instructions: our index word (`mov c[0x4][..]`), our `& 0xFFFFFF` (the packed signs),
the sampler index word, NVK's two bounds clamps (`imnmx`), two shifts, two descriptor loads (`ldc`), NVK's two masks
and an `or` that joins the image and sampler halves. NIR already builds each slot's handle once (no slot is built
twice in any of the 15 shaders), so hoisting does not help. Making each handle cheaper does.

**On Maxwell an instruction takes one constant-buffer operand for free.** An immediate is free only in some
positions, and only if it fits 20 bits (a float with its low 12 mantissa bits 0). Anything else costs a `mov`: a
multiply-add's addend, 32-bit values like `0xbf41d085`, and `±FLT_MAX`.

Items 1-8 together would take the n538 family to about 560 instructions and the n112 family to about 400 (the n112
family, the `ps_i...` in-place containers, is the same material with the shadow taps' Poisson offsets `c19-c22` written
as literals: that is where its 107-117 moves come from). Unleashed's median is 554. GPU time will not fall by the same share (fetches and bandwidth stay), so the console decides.

## The first thirty

| # | Change | Evidence | Expected | Exact | Effort |
|---|---|---|---|---|---|
| **Constants** | | | | | |
| 1 | The shader's literal constants (`float4 cN = asfloat(...)`) that NAK cannot encode (low 12 bits set) read from the constant registers instead of written as immediates. Direct3D loads the definition table into those registers when it binds the shader. The translator already reads them there for relocated definitions. Check in verify mode that register N equals the literal at every draw; the fallback is for the renderer to write the shader's literals into its block | 342 of 686 PS use them (12,109 uses, up to 57 a shader); n112 family 107-117 `mov`s a shader | **−13-15 %** in the n112 family | yes (same values, check proves it) | small |
| 2 | Literals used as a multiply-add's addend (any value but 0, e.g. `1 - x*y`) from the constant registers too | the `mov 0x3f800000` / `0xbf800000` before `ffma` | −4-10 a shader | yes | with 1 |
| 3 | `-FLT_MAX` and `FLT_MAX` of the Xenos RSQ / RCP / LOG clamps from two words of the shared block | 444 PS, 6,593 clamps; 12-27 `mov`s in each heavy PS | **−2-3 %** (ps173: −6 %) | yes | **built in package 25** (`banjo_gpu_clamp_cbuf`) |
| **Texture handles** | | | | | |
| 4 | Pipeline robustness off for the game's pipelines (`VkPipelineRobustnessCreateInfoEXT`: storage buffers, uniform buffers, images DISABLED; vertex input left as it is). NVK's `clamp_desc_array_bounds` then drops the two index clamps. NVK exposes `EXT_pipeline_robustness` | 2 of the 12 handle instructions | −20-28 a heavy PS (**−3 %**) | yes, as long as every index is in range (the renderer writes them) | **built in package 25** (`banjo_gpu_robustness_off`) |
| 5 | No `& 0x00FFFFFF` on slots whose constant 61 bit is clear: their packed signs are 0, so the top byte already is | 1 of the 12 | −6-10 (−1 %) | yes | **built in package 25** (`banjo_gpu_sign_mask_skip`) |
| 6 | Combined image-sampler descriptors: one heap of (texture, sampler) pairs. NVK's `texture == sampler` path loads one word and skips the second index, the masks and the `or` | 6 more of the 12 (12 -> 3 with 4 and 5) | −40-60 a heavy PS (**−6-8 %**) | yes | medium (renderer: pair cache) |
| 7 | The exact packed signs of the slots that keep sign code, specialised (2 bits a component): the runtime tests and both conversions fold to the one that applies | rare since only signs 2 and 3 count | small | yes | small |
| **Fetch work** | | | | | |
| 8 | **Merge sibling fetch blocks**: the three shadow cascades are the same 8-tap block with other constant rows, another slot (s13 / s14 / s15) and one extra scale. One block with the rows and the slot chosen by `select` | blocks of 88 / 82 / 81 instructions (n538 family) and 106-109 / 98-100 / 98-100 (n112 family): 13 of the 15 heaviest PS | **−150 to −190 (−18-24 %)** in both families; 16 fewer fetches in the binary | yes (same operations on the selected values) | medium (translator pass) |
| 9 | If 8 is not done: the PCF offset rotation (`c19-c22 × r10`, the same in all three blocks) computed once before them | 4 rows × 2 `ffma` × 3 blocks | −16-30 | yes | small |
| 10 | Explicit LOD 0 (`tex.lz`) for slots whose texture has one mip level and no anisotropy (a bit per slot): the full-screen and post passes | most render-target reads | small per fetch; measure | yes (one level: the LOD does not matter) | small |
| 11 | A slot bound to the renderer's dummy texture folded to the dummy's value (a bit per slot) | needs a counter first: how many fetches read the dummy | unknown | yes (a constant texture filters to itself) | small |
| 12 | `cube()` + `cubeMapDirections[index]`: the direction goes through a 2-element array indexed at run time; pass it straight to the fetch that follows | 48 PS | small | yes | tiny |
| **Control flow and registers** | | | | | |
| 13 | Adaptive predicated form: when the flattened form (`banjo_gpu_pred_select_ps`) of a pipeline crosses an occupancy step (40 -> 48 GPRs = 48 -> 40 warps an SM), create its branch form instead. NAK's statistics are known when the pipeline is made | 16 pipelines at 48 GPRs (40 warps) in package 24b | occupancy for those 16 | yes (both forms exact) | small |
| 14 | Skinning: blend each bone as its rows arrive instead of loading all 12 rows first | VS n58 / n692: 56 GPRs | fewer registers; measure (latency against occupancy) | yes | small |
| **Vertex shaders** | | | | | |
| 15 | **Zero-padded bone buffers**: the renderer zero-fills 16 bytes past each indexed-fetch buffer, so a row is one clamp and one load. No compare-and-select per word | n58: 84 `sel`, 48 of them the bounds selects | **−48-60 (−11-13 %)** of the skinning VS | yes (past the end still reads 0) | **built in package 25** (`banjo_gpu_bone_padding`) |
| 16 | **Outputs the pixel shader does not read are not written**, instead of written as 0. NIR then drops the store and NAK the attribute | VS n58 stores 23 attributes, 16 of them the same zero register; the PS reads 6 | −16 stores a vertex; vertex output 368 -> ~112 bytes | yes (constant 60: never read) | **built in package 25** (`banjo_gpu_vs_outputs_unwritten`) |
| 17 | Output pruning per component (constant 60 with a bit per component: 88 bits in three constants) | a TEXCOORD read as `.xy` still computes `.zw` | some VS math | yes | small |
| 18 | Bones through a texel buffer (RGBA32UI view over the upload buffer + a base row in the shared block): one `tld` per row, no 64-bit address arithmetic | n58: 89 integer instructions (the 64-bit carries `shf` / `iadd2.x` per row) | up to −25 % of the skinning VS; measure (texture path against L1) | yes (with 15's zero rows) | medium |
| 19 | Skin once a frame: split the skinning VS at the bone blend; the scene, small and shadow passes reuse the skinned vertices | the n58 family skins 55k vertices in each of several passes | the small pass's VS time (3.8-6.3 ms a frame in package 19-20 levels) | yes (same operations once) | large |
| 20 | VS n335's indexed temporaries without local memory: a select chain when the index range is small, or only the indexed registers in the array | 3,261 instructions, 320 bytes local memory, 40 spills | that shader only (rare) | yes | small |
| 21 | Identity position transform specialised away (`g_NdcScale` 1, `g_NdcOffset` 0, no half-pixel offset): one bit | every VS, a few instructions | small | yes, but check −0 in clip space | tiny |
| 22 | Front-facing specialised from the cull mode: with back faces culled `iFace` is always true (`iFace ? 1 : -1` folds) | 310 PS read it | small | yes | tiny |
| **Pixel output and fill** | | | | | |
| 23 | Colour write mask specialised: components the pipeline does not write (and alpha when neither the blend nor the alpha test uses it) set to 0 at the exit, so their math folds | per pipeline | some PS math | yes | small |
| 24 | Early blend skip: a `banjo_gpu_blend_skip` pipeline discards 2x2 quads whose alpha is ≤ 0 for all four pixels at the early-alpha point, with the same margin rule as `banjo_gpu_early_alpha` | blended particles and fog | the shader's tail for invisible pixels | yes (same proof as early alpha) | small |
| 25 | No fragment shader for depth-only draws: no colour writes (bit 14), no alpha test, no discard, no depth export | 92 of the 1,721 pipelines in the console's own pipeline cache (2026-10-16) | their PS invocations | yes (depth comes from the rasterizer) | small |
| 26 | **Correction: early tests already exist** (`kSpecZEarly`, bit 20: an `EarlyFragmentTests` copy of the module for draws that discard but write no depth; 37 of the 1,721 pipelines in your cache). What is left: it skips every draw with stencil on (`counts_z_[kZStencil]`), also those that never write stencil (write mask 0 or all KEEP), for which early tests are just as exact | `masseffect_native_draws.cpp` ~5180 | early-Z for stencil-tested, non-writing draws | yes | tiny |
| 27 | The 18 depth-exporting PS: find their draws (a counter) and keep them off the costly paths | depth export forces late Z | unknown | yes | tiny (counter) |
| **Correctness found while reading** | | | | | |
| 28 | Loops ignore the loop constant's start and step: `for (aL = 0; aL < i.x; aL++)` | 3 shaders | correctness | | tiny |
| 29 | `tfetch1D` does not strip the packed signs from the descriptor index | no Banjo case seen | correctness | | tiny |
| 30 | `banjoVfetch` reads 3 words for `k_32_32_32_32` (`count = format - 32`), so `w` stays 1.0 | integer indexed fetches only | correctness | | tiny |

## Second batch: 31-70 (2026-10-16 evening)

Sources: the whole library translated with an annotating copy of the translator (each RCP / RSQ / LOG variant and
each fetch instruction's fields counted), UnleashedRecomp-NX's and MarathonRecomp-NX's XenosRecomp forks, NAK's
Maxwell legalisation (`nak/sm50.rs`) and the console's own pipeline cache (1,721 pipelines, 404 pixel shaders, sent by
the user). What NAK does with an operand: a multiply-add takes a constant-buffer addend for free when its multiplicand
is a register, but moves an immediate addend and any 32-bit immediate into a register first (`sm50.rs` 535-550; NAK
never uses Maxwell's 32-bit-immediate FFMA form, line 573); min/max, compares and selects take a 20-bit immediate or
a constant-buffer operand, and move any other immediate (`OpFMnMx`, `OpFSetP`, `OpFSet`, `OpSel`). NIR then
rematerialises each constant next to each use (`nak_nir_rematerialize_load_const.c`), which is why one literal costs
one move per use.

| # | Change | Evidence | Expected | Exact | Effort |
|---|---|---|---|---|---|
| **Constants and the Xenos math** | | | | | |
| 31 | **The IEEE forms unclamped.** The translator clamps every reciprocal, reciprocal square root and log to ±FLT_MAX. The Xenos clamps only the `_CLAMP` forms (`RECIP_CLAMP`, `RECIPSQ_CLAMP`, `LOG_CLAMP`); the `_IEEE` forms return ±inf. Writing the IEEE ones as plain `rcp` / `rsqrt` / `log2` removes 2 min/max (and the two moves of item 3) each | pixel shaders: `RECIP_IEEE` 2,278, `LOG_IEEE` 2,084, `RECIPSQ_IEEE` 77 (4,439 of the 6,593 clamps); `RECIP_CLAMP` 1,102 and `RECIPSQ_CLAMP` 1,040 keep theirs | −2-4 instructions a use: about −50 in the heavy material shaders | **no: pictures change where a value reaches 0 or inf, toward what the 360 does. Your call** | tiny |
| 32 | `RECIP_FF` (12 pixel shader uses) has its own Xenos rule (a fixed-function reciprocal, not the clamped one); check it against Xenia's definition | 12 uses | correctness | | tiny |
| 33 | 32-bit literals in compares, min/max and selects read from the constant registers too (item 1 covers multiply-adds) | NAK moves them (`OpFSetP` / `OpFMnMx` / `OpSel` legalisation) | a few a shader | yes | with 1 |
| 34 | Item 1 for vertex shaders: 104 VS use 32-bit literals | 104 of 368 VS | small | yes | with 1 |
| 35 | Item 3 for vertex shaders: 137 VS have 349 clamps | | small | yes | with 3 |
| **Alpha test (from UnleashedRecomp-NX)** | | | | | |
| 36 | **Alpha-test early-out at the alpha's last write** (Unleashed `SPEC_CONSTANT_ALPHA_TEST_EARLY_OUT`): right after the last write of `oC0.w`, `if (alpha passes)` around the rest; a failing pixel skips it and is discarded at the end as before. No copy of the alpha code, no margin | 334 of your 1,721 pipelines are alpha-tested | the colour math of every discarded pixel | yes (the same comparison) | small |
| 37 | **Sink** (Unleashed `ALPHA_TEST_SINK`): arithmetic before the early-out that only feeds the colour moves below it, so discarded pixels skip it too | Unleashed, Marathon | more of the same | yes | medium |
| 38 | **Quad sink** (Unleashed `ALPHA_TEST_QUAD_SINK`): fetches with an implicit level of detail move too, run by every 2x2 quad with at least one kept pixel (derivatives unchanged) | | the alpha-tested foliage's fetches | yes | medium |
| 39 | Keep the early-out only where the skipped region is long enough (Marathon undoes short ones) | Marathon `XENOS_RECOMP_SWITCH_PERF` | avoids a branch for nothing | yes | tiny |
| 40 | Adaptive early alpha: count per pipeline how many quads `banjo_gpu_early_alpha` discards; where it never pays, clear its bit (both forms are exact) | 304 pipelines carry the bit | the copy's cost where it does not pay | yes | small |
| 41 | The 30 alpha-tested pipelines without an early-alpha copy get item 36 (it needs no copy) | 30 in your cache | | yes | with 36 |
| **Blending** | | | | | |
| 42 | Blend skip for modulate blends (`DST_COLOR × src`, `ZERO` destination factor... with a source of 1): the target is unchanged, so the pixel is discarded before the blend | `banjo_gpu_blend_skip` covers only alpha ≤ 0 and source 0 (38 pipelines) | ROP work of those draws | yes (dst × 1 = dst) | small |
| 43 | Blend skip on float targets for the multiply forms (exact there too); the add-a-zero forms are not exact on float targets (−0 + 0 = +0) | | | yes, multiply forms only | small |
| **Fetches** | | | | | |
| 44 | Gather (Unleashed `SHADOW_GATHER`): four point fetches at (±0.5, ±0.5) texels or nine at (−1..1) of one coordinate read as one or four `GatherRed` where the slot is point-filtered, single-mip and power-of-two | 4 pixel shaders in the library have the pattern | 3 or 5 fetches fewer each | yes (same texels) | small |
| 45 | **Explicit level of detail where the instruction asks for it**: 64 fetches have `use computed LOD` off (their level is the bias: base level), 16 take the level from a register. The renderer logs them as "fetch with its own level of detail (not applied)". `SampleLevel` gives the console's level, and NAK's `tex.lz` (explicit 0) skips the derivative work | 64 + 16 fetches (PS n737 and VS n414 among them) | cheaper fetches, the 360's mip choice | **no: the level changes where the texture has mips (toward the 360)** | small |
| 46 | **Unnormalised coordinates**: 34 fetches (34 pixel shaders, all point-sampled) set `texCoordDenorm`; the translator ignores it and treats texel coordinates as 0-1 | `shader_code.h` decodes it, nothing reads it | correctness: those fetches read the wrong texel | **no (a fix)** | tiny |
| 47 | Copy shaders recognised (Unleashed / Marathon `SHADER_FLAG_PIXEL_COPY` / `DEPTH_COPY`: `oC0` = the fetch of one slot at TEXCOORD0, nothing else): the renderer runs them as an image copy or a fixed minimal pipeline | Unleashed's renderer uses it for its full-screen copies | the copies' shader work | yes | medium |
| 48 | Pass-through vertex shaders recognised (`SHADER_FLAG_POSITION_PASS_THROUGH`, `TEXCOORD_PASS_THROUGH`): the full-screen passes get a procedural triangle, no vertex fetch | Unleashed, Marathon | small per pass | yes | small |
| **Vertex shaders** | | | | | |
| 49 | **a0-indexed constant arrays through memory** (Unleashed / Marathon `SPEC_CONSTANT_RELATIVE_FROM_MEMORY`): a bone palette in constants indexed per vertex goes through the L1 instead of the constant bank, which serialises different addresses inside a warp | 28 VS, 660 relative reads | those VS's constant stalls | yes (the same bytes of the same block) | small |
| 50 | Rigid meshes: when every vertex of a draw has bone weights 2-4 equal to 0 (known once per persistent vertex block), a variant without bones 2-4 (their loads and blends fold) | the skinning VS loads 12 rows a vertex whatever the weights | up to −75 % of a rigid draw's skinning | yes | medium |
| 51 | Water VS: `id / points` (an integer division by the draw's grid size) as a multiply by a magic number the renderer computes for that divisor | VS n414: 52 `iadd2`, 17 `imul`, 5 `imul.hi`, 3 `mufu.rcp`, 10 `i2f` | −10-20 a vertex | yes (exact for every 32-bit numerator) | small |
| 52 | Item 15 for the word-by-word indexed fetches (water `k_32_32_32_FLOAT`): zero padding instead of a clamp and a select per word | VS n414: 9 loads, 20 clamps, 34 selects | −30-50 a vertex | yes | small |
| 53 | Non-identity vertex input remaps specialised (12 bits a location: the select chains fold to a swizzle) | 339 VS call `remapInput`; only the identity is specialised now | a few a vertex where used | yes | small |
| 54 | Memory export format specialised per pipeline (`banjoMemexportStore` switches on the format at run time) | 12 VS | small | yes | small |
| **Control flow and variants** | | | | | |
| 55 | **Selective flattening**: pixel shader blocks with transcendental operations (`log2` / `exp2` / `rcp`) or more than ~6 statements stay branches; short arithmetic is flattened. The light blocks (the light's range is their predicate) now run for every pixel | 4,823 flattened blocks in 369 PS; n538's lighting block holds 55 selects of the flattened form | the light math of warps outside a light | yes (both forms exact) | small |
| 56 | Light blocks as a loop: the 4 light blocks of the material shaders are the same code with other constant rows | n538 family | code size (instruction cache); measure | yes | medium |
| 57 | Specialise only the bools that gate real code; leave the ones guarding a few instructions at run time | up to 64 pipelines per PS in your cache | fewer variants: fewer compiles, binds and generic-pipeline draws | yes | small |
| 58 | Pipelines whose specialised code and state are identical share one pipeline object | | fewer binds (3.0-3.5 µs each on the executor) | yes | small |
| **Your pipeline cache** | | | | | |
| 59 | Ship your console's pipeline list (1,721 pipelines) as the package's prewarm list (the list part only, as `make_prewarm_list.py` does) | your `masseffect_native_pipelines.bin`, list v3 | no first-boot compiles for those | yes | tiny |
| 60 | Strip the SPIR-V debug information (names, lines) from the library | the package is 49 MB for 852 shaders | load and parse time | yes | tiny |
| **The renderer's own shaders (the stencil rebuild of the 5 fps screen)** | | | | | |
| 61 | **Validation on the CPU, not per pixel**: every EDRAM import fragment shader checks ~30 push-constant values and, in five of them, queries the texture's size and sample count, in every pixel of every pass; the values are the CPU's own | `me_edram_depth_msaa2_to_stencil_1x.frag`: 6 `discard`s, 2 `textureSize`, 1 `textureSamples`; 12 import shaders with 3-7 discards each | −4 texture queries and ~35 instructions a pixel a pass (8 bit passes over 655k pixels a frame on that screen) | yes (a pass with bad metadata is refused on the CPU instead) | **partly built in package 25** (`banjo_gpu_edram_checks_cpu`: the four texture queries; the ~30 push-constant compares stay) |
| 62 | Read the source stencil once: one pass copies it from the 2x depth-stencil image into an R8UI image, and the 8 bit passes read that instead of the multisampled depth-stencil view | 8 `texelFetch`es of a 2x D32S8 stencil per pixel today | most of the import's texture traffic | yes | small |
| 63 | The stencil-bits tracking tightened: no bit pass has ever been skipped in any console log ("stencil bit passes skipped (bit never set): 0") | every log | up to 7 of the 8 passes where the game uses few bits | yes | small |
| 64 | Rectangles per tile-row run instead of one full-width scissor (a multi-row run's scissor covers tiles outside the run, which the shader then rejects pixel by pixel) | the clear already does it per run (`masseffect_native_targets.cpp` ~7790) | the rejected pixels | yes | small |
| 65 | Skip the import when the source stencil has not changed since the last import into that target (a generation counter on stencil writes) | once a frame, 512 tiles, on the Banjo screen | the whole 46 ms when nothing wrote stencil | yes | small |
| 66 | Ship `banjo_stencil_fetch_3d_ab.toml` (A/B 32: the 3D passes against the copy engine): never run on the console | not in `configs/` | decides 61-65's baseline | | tiny |
| 67 | The same validation move (61) for the other import shaders and the compute conversions | 12 fragment shaders | `edram_import` 0-1.7 ms a frame | yes | small |
| **Binding (the CPU side of shaders)** | | | | | |
| 68 | **Shader objects** (`VK_EXT_shader_object`, which NVK has) instead of pipelines: bind only the stage that changed; NVK's pipeline bind copies the whole dynamic state (`vk_dynamic_graphics_state_copy`, 4 % of the executor) | BindPipeline 3.03-3.46 µs a draw, the largest C6 substage | part of the executor's 3 µs a draw; measure | yes | large |
| 69 | Descriptor buffers (`VK_EXT_descriptor_buffer`, NVK on Maxwell) for the texture and sampler heaps: descriptor writes become stores, which makes item 6's pair heap cheap to keep | | CPU for item 6 | yes | medium |
| 70 | Vertex input as dynamic state (`VK_EXT_vertex_input_dynamic_state`): the vertex layout leaves the pipeline key | layouts are part of every key | fewer pipelines; measure the extra `vkCmdSetVertexInputEXT` | yes | medium |

## Third batch: 71-110

32 changes, then 8 ideas checked and ruled out (103-110), listed so they are not tried again.

| # | Change | Evidence | Expected | Exact | Effort |
| **More from the driver source** | | | | | |
| 71 | Vertex attributes the specialised VS no longer reads (their outputs pruned by constant 60) dropped from the pipeline's vertex input state: NVK programs every listed attribute `SOURCE_ACTIVE` (`nvk_cmd_draw.c` 2777-2812) whatever the shader reads; check whether Maxwell then fetches them | the skinning VS has 7 inputs; most pipelines read fewer outputs than it writes | vertex fetch bandwidth, if fetched | yes | small |
| 72 | Pipeline robustness off also for vertex input where the renderer guarantees in-range vertex buffers (the robust vertex fetch path clamps) | `VkPipelineRobustnessCreateInfoEXT::vertexInputs` | small | yes, only with in-range buffers | small |
| 73 | `tex.lz` for every fetch of a single-mip texture without anisotropy (a bit per slot): NAK picks `LOD_MODE_ZERO` for an explicit level 0 (`nak_nir_lower_tex.c` 258-270), no derivatives or quad work | render-target reads (post passes, the 640 / 320 passes) | small per fetch | yes (one level: the level does not matter) | small |
| 74 | No texture handle at all for slots whose texture is the renderer's 1x1 dummy (a bit per slot, the fetch folded to the dummy's value): shadows off in menus and cutscenes make the 24 shadow taps constants | needs a counter of dummy fetches | the cascades in those scenes | yes | small |
| 75 | After 8 (merged cascades): the cascade index computed once from the two depth tests, instead of the three `banjoSel` counter chains (`p0 = r5.w == 1.0`...) | 3 chains a shader | −10-15 | yes | with 8 |
| 76 | The four Poisson offset rows (`c19`-`c22`) multiplied by the per-pixel rotation once for all cascades (they are the same rows in all three) | n538 and n112 families | −16-24 | yes | with 8 |
| 77 | Shadow compare and average (`r11 >= r2.wwww`, `dot(r11, C75.wwww)`): one `fset` per tap (NAK's 1.0 / 0.0 compare) instead of compare + select, if NAK does not already | check the asm | −8 a shader | yes | tiny |
| 78 | Skinning: one 64-bit base for all bone rows of a draw and 32-bit offsets (`ld.global` with a register offset still needs the 64-bit add; NAK has no 32-bit-offset form on SM50) — measure against item 18 | n58: two `shf` and an `iadd2.x` per row | −24-36 | yes | small |
| 79 | `int(floor(r6.x)) * 16` per bone: NAK folds floor into `f2i.floor`? check; if not, `f2i` with floor rounding | 4 a vertex | tiny | yes | tiny |
| 80 | **Correction:** the passes labelled "VS n645 PS n267" in the GPU-by-pass lines are labelled by their first draw only; VS n645 is a pass-through (a position and TEXCOORD0) that NAK still counts at 254 instructions, because all 22 outputs are stored (21 of them the defaults or zeros nobody reads). Item 16 (`banjo_gpu_vs_outputs_unwritten`, package 25) removes those stores; what the rest of those passes cost needs the per-draw GPU times | VS n645: 254 instructions for two outputs | its 21 stores | yes | done in package 25 |
| 81 | Position-only vertex shaders for depth-only pipelines (92 in your cache, item 25): with item 16 every output except the position disappears | 92 pipelines | their VS outputs | yes | with 16 |
| 82 | Pre-specialised SPIR-V in the package for the variants in your cache (bools and bits folded offline by spirv-opt, as `tools/shader_size.sh` does): NVK parses and folds less when it compiles | ~1 s per heavy pipeline on the console | shorter compile hitches | yes | medium |
| 83 | One module per (shader, constant 0) instead of one per shader with every variant inside: DXC's output keeps both arms of every spec test until NVK folds them | the 2.25 pipelines a pair average | compile time | yes | medium |
| 84 | Generic pipelines (bools at run time) only for shaders that are drawn before their specialised variant exists; the prewarm list makes that rare | the generic heavy PS are up to 1,401 instructions | GPU time of generic draws | yes | small |
| 85 | The translator prints, per shader, what NAK will have to move (32-bit literals, literal addends, two-constant instructions, clamps) so items 1-3 can be checked offline | | | | small |
| 86 | `tools/shader_size.sh` with your cache's real keys (constant 0, bools, 60, 61 of each pipeline) instead of one fixed constant 0 | | offline numbers for the pipelines the console uses | | small |
| 87 | A per-package comparison of NAK's statistics (`banjo_diag_shader_stats`) against the previous package's, shader by shader | | regressions seen at once | | small |
| 88 | A console counter of draws per shader for the 34 unnormalised-coordinate fetches and the 64 own-LOD fetches, to know whether 45-46 show on screen | | | | tiny |
| 89 | Per-fetch filter overrides: already applied by the renderer per fetch constant (`banjo_native_tfetch_filters`); a shader whose fetches of one constant disagree gets one of them (`filters_conflict`): count those | 8,088 of 11,279 fetches set point / point / point, aniso off | correctness for the conflicting ones | | tiny |
| 90 | The Xenos `SetpClr` writes `FLT_MAX` to the scalar result: a 32-bit immediate (a move) where it is used; constant register instead (with 33) | rare | tiny | yes | tiny |
| 91 | The import vertex shader's full-screen triangle with a scissor: a quad covering only the scissor (fewer helper pixels outside it) | `me_edram_import.vert` | small | yes | tiny |
| 92 | Stencil import: 8 bit passes then the colour draw in one render pass already; check that the depth-stencil image is not decompressed for the shader read every frame (a layout change of a compressed 2x depth target) | the 46 ms is ~50x what the passes' pixels need | possibly most of the 46 ms | yes | small (check) |
| 93 | The copies category (5-7 ms a frame): per-resolve counters by size and path (copy engine, compute, 3D) to find the costly ones | `copies` 5.1-7.0 ms a frame | | | small |
| 94 | Resolve shaders: the compute resolves (`me_depth_resolve_guestspace*.comp`) query sizes per invocation like the imports (item 61) | 2 `textureSize` + 1 `textureSamples` in the 2x one | small | yes | tiny |
| 95 | Bone fetch limits from the renderer: `last = (max(words, 1) - 1) & ~3` and the 64-bit base are recomputed per vertex from the shared block (Maxwell has no uniform datapath); the renderer writes them ready (with 15: the padded limit) | n58 / n692 | −4-6 a vertex | yes | tiny |
| 96 | A 3D lookup texture read as a 2D slice when its z coordinate is a draw constant and the volume's z filter is point (a bit per slot): n538's `tfetch3D` of s1 takes z from `c0.z` | 25 PS with 47 3D fetches; check the filters first | a 2D fetch for a 3D one | yes, point in z only | small |
| 97 | Conservative depth (`DepthGreater` / `DepthLess`) for those of the 18 depth-exporting PS whose written depth is provably on one side of the interpolated one, so the hardware keeps early-Z for them | 18 PS export depth (late Z) | check each shader first | yes where provable | small |
| 98 | Skinning once a frame (item 19) with the skinned vertices in a buffer the scene, small and shadow passes read: the largest VS item | the n58 family in every pass | the small pass's VS time | yes | large |
| 99 | Zero-copy vertex buffers: import guest memory into the GPU (`VK_EXT_external_memory_host`, if Horizon's NVK has host-pointer import: `has_host_ptr_import`) and swap bytes in the VS (`prmt`) | uploads 7.4-9.9 µs a draw on the executor | CPU; check the KMD support first | yes | large |
| 100 | Vertex pulling for the small draws (indices fetched by the VS): no CPU index conversion | `IndicesFrom16` and the index persistence on the executor | CPU against GPU (no post-transform cache); measure | yes | large |
| 101 | Combined image samplers (6) plus descriptor buffers (69) together, so a draw's handles are one load each and the CPU writes them as plain stores | | 9 instructions a handle, CPU-cheap | yes | large |
| 102 | One shader per pass type for the EDRAM imports instead of a validation-heavy general one per format pair: specialisation constants for the formats and MSAA modes (no run-time branches on `source_format`) | 12 import shaders branch on push constants per pixel | the import passes | yes | medium |
| **Checked and ruled out** | | | | | |
| 103 | Flat interpolation for inputs that are constant over a draw: perspective interpolation of a constant is not bit-exact, so **ruled out** | | | no | |
| 104 | `pow` (`exp2(y * log2(x))`, 2,088 in the PS) as multiplies for small integer `y`: `y` is a constant register, not specialised, so **ruled out** | | | | |
| 105 | Bound (non-bindless) textures: NAK on SM50 uses bindless for all of them and has no constant-buffer texture form (`sm50.rs` 2185 panics on it), so **not possible** | | | | |
| 106 | Fast-math SPIR-V (`FPFastMathMode`) so NIR folds `x + 0` and `0 * x`: **not exact** | | | no | |
| 107 | FP16 arithmetic (Maxwell's double-rate `HFMA2`): **not exact** | | | no | |
| 108 | Reversed depth back to "less" so NVK's ZCULL ("less") works for the scene: `1 - z` rounds, so depth tests can change: **not exact** | | | no | |
| 109 | Constant-only subexpressions computed on the CPU or in the VS: different fusion and rounding (a lone multiply or add of two constants would be exact, but there are too few): **ruled out** | | | no | |
| 110 | Nothing to take from the other ports here: Marathon's clip-distance specialisation (Banjo's VS write no clip distance), its vertex half-swap masks (Banjo specialises its one swap already), Unleashed's size helpers for bicubic / weights fetches (no Banjo shader uses them); `MULPREV2` has 0 uses | | | | |

## Fourth batch: 111-150 (2026-10-16 night)

Found while implementing package 25 (items 3, 4, 5, 15, 16, 61 are built: see `docs/enhancements-reference.md`,
"Package 25's shader round"). What is left of the translator's own overhead is small: of the pixel shaders' 1,521
`if (p0)` blocks that are still branches, 1,427 hold a texture fetch (an implicit level of detail: flattening would
change derivatives), 49 are nested and 45 write an output; only 2 were held back by a reciprocal. So this batch leans
on structure, the renderer's side of the shaders and the GPU passes. 26 changes, 6 measurements that decide other
items, 8 ideas checked and ruled out.

| # | Change | Evidence | Expected | Exact | Effort |
|---|---|---|---|---|---|
| **Structure the translator could recover** | | | | | |
| 111 | Xenos predicate counters as booleans: the `SetpInv` / `SetpPop` / `SetpPush` chains (`p0 = r10.w == 1.0; ps = banjoSel(p0, 0, banjoSel(r10.w == 0, 1, r10.w))`) are the Xenos compiler's if / else-if stack; where the counter register is read only by those instructions, plain bools say the same | the cascade selection of 13 of the 15 heaviest PS; 4,515 `banjoSel0` in 350 PS | −10-20 a shader | yes (when the register has no other reader) | medium |
| 112 | Output writes in a predicated block (45 blocks): `if (p0) oC0 = x` as a select of the output | 45 blocks | small | yes (an output has no side effect before the exit) | tiny |
| 113 | Nested predicated blocks (49): flatten the inner block when the outer one is flattened | 49 blocks | small | yes | small |
| 114 | Blocks whose fetches are point-sampled from single-mip textures (no level of detail to get wrong) may be flattened; only worth it for short blocks the predicate usually takes (a flattened fetch runs for every pixel) | 1,427 fetch blocks; the shadow taps are point / single-mip | measure per block | yes for those slots | small |
| 115 | The tessellation domain's old path specialised away: `banjoTessDomain` tests bit 30 of the tess word at run time, so the water VS carries both the grid and the corner-list paths | VS n414 offline: 12 branches, 17 phis | −20-40 in VS n414 | yes | tiny |
| 116 | Vertex outputs packed per VS / PS pair: the renderer rewrites both modules' `Location` / `Component` decorations so the components a pair uses share vec4 slots (fewer attributes, fewer `ipa`) | PS n538 reads 7 inputs, 28 components | attribute traffic and IPA | yes | medium |
| 117 | Drop each translator change's old form from the shipped library once its A/B has decided (every old form doubles that code in the module until NVK folds it) | package 25's library: 57.0 MB against 49.2 MB (+16 %) for both forms of six changes | NVK parse time (compile hitches) and SD space | yes | small |
| 118 | Remove the `?:`-free remainder: `BANJO_FLT_LO` / `_HI` were first a `?:`, which HLSL 2021 makes a branch per use until specialisation (58.9 MB library); now `select`. Audit the header for other scalar `?:` on constants | `shader_common.h` | module size | yes | tiny |
| **The stencil import (the 5 fps screen), after item 61** | | | | | |
| 119 | Bit passes as indirect draws: one compute dispatch ORs the source's stencil bytes over the tile range and writes eight `VkDrawIndirectCommand`s (3 or 0 vertices); a pass for a bit that no pixel has draws nothing, with no CPU readback | "stencil bit passes skipped: 0" in every log (the CPU's bit tracking says all 8 may be set) | up to 7 of 8 passes where the game uses few bits | yes | medium |
| 120 | The source stencil copied once to an R8UI image (one pass), and the 8 bit passes read that single-sample byte instead of a 2x depth-stencil `texelFetch` each | 8 multisample stencil reads per pixel | most of the passes' texture traffic | yes | small |
| 121 | Check the source's layout path: a compressed 2x depth image read as a texture every frame may be decompressed each time (NVK); keep it in the layout the passes read, or measure with the GPU groups | the 46 ms is ~50x the passes' pixel work | possibly most of it | yes | small (check) |
| 122 | One rectangle per tile-row run for the bit passes (the clear already does it): a multi-row run's full-width scissor covers tiles outside the run, rejected pixel by pixel by the tile test | `masseffect_native_targets.cpp` ~7790 | the rejected pixels | yes | small |
| 123 | Skip the import when nothing has written the source's stencil since the last import into that target (a generation counter on stencil writes) | once a frame, 512 tiles, on that screen | the whole import when it repeats | yes | small |
| **GPU passes** | | | | | |
| 124 | **ZCULL for the scene**: `banjo_zcull_greater.toml` (the driver's ZCULL in the direction of the reversed-depth scene, `banjo_gpu_zcull_greater`) exists but has not been run since the EDRAM work; hierarchical Z would reject hidden tiles of the 20-23 ms scene pass. Risk: UnleashedRecomp lost the GPU with ZCULL planes and depth-clear changes; run it once, short | the scene's reversed depth uses no ZCULL today | a share of the scene pass | yes | tiny (run) |
| 125 | EDRAM plan step 4: skip the second tile pass (its effects replayed) | `docs/edram-removal-plan.md` | the second pass's draws | yes | large |
| 126 | EDRAM plan step 3's merged depth-resolve dispatch | same | part of `copies` (5-7 ms a frame) | yes | medium |
| 127 | Per-resolve counters by size and path (copy engine, compute, 3D) and by whether the result is ever sampled, to find the costly resolves in `copies` | `copies` 5.1-7.0 ms a frame | decides 126 | | small |
| 128 | `scene_no_z` (1.1-2.6 ms a frame): the draws without depth (UI, post) by shader, to see which shaders those are | GPU per Swap lines | decides the next shader items | | tiny |
| **Binding and pipelines (the CPU side of shaders)** | | | | | |
| 129 | One dynamic UBO binding for the three constant blocks (vertex, pixel, shared at fixed offsets in one range) instead of three dynamic offsets: NVK's descriptor-set bind with dynamic offsets costs per offset | set 4 bind 1.25-1.43 µs a draw (0.83 binds a draw) | part of that | yes | medium |
| 130 | Shader objects (`VK_EXT_shader_object`): bind only the stage that changed; NVK's pipeline bind copies the whole dynamic state (`vk_dynamic_graphics_state_copy`) | BindPipeline 3.0-3.5 µs a draw (on the worker with deferred recording on) | the worker's time | yes | large |
| 131 | Fewer bool variants: specialise only the bools that gate real code (up to 64 pipelines for one PS in your cache) | 1,721 pipelines for 404 PS | fewer compiles and binds | yes | small |
| 132 | The prewarm list from your console's cache (1,721 pipelines) shipped with the next package that keeps the shader library (its keys are the library's) | your `masseffect_native_pipelines.bin` | no first-boot compiles for those | yes | tiny |
| **Vertex shaders** | | | | | |
| 133 | VS n645 is a pass-through that NAK counts at 254 instructions (22 outputs stored); with item 16 it stores 2: measure the full-screen passes' VS time after package 25 | the first draw of the small / 640 / 320 passes | small per pass | yes | done (16) |
| 134 | Rigid meshes (item 50) need the bone weights of a draw: compute "weights 2-4 all zero" once per persistent vertex block when it is fingerprinted | the persistent blocks fingerprint those bytes anyway | makes 50 cheap | yes | small |
| 135 | The bone palettes' 64-bit base and padded limit written ready by the renderer (two words a buffer) instead of recomputed per vertex from three shared words | n58: `(max(words,1)-1)&~3` and 64-bit joins per vertex | −4-6 a vertex | yes | tiny |
| 136 | Skinning once a frame (item 19) with the skinned positions in a buffer: the largest VS item; needs the VS split at the bone blend | the n58 family in every pass | the small pass's VS time | yes | large |
| **Pixel shaders** | | | | | |
| 137 | The alpha test's early-out (item 36) also for the blend skip's test (Unleashed puts both in one branch), so a fully transparent particle pixel skips the shader's tail | 38 blend-skip pipelines in your cache | the tail for invisible pixels | yes | with 36 |
| 138 | Literal registers (`cN`) were not allowed in flattened blocks; now they are (package 25), which flattened the in-place material shaders' light blocks. The same for the vertex shaders' literal registers | `ps_i1a1a53fc5f5efc71`: 25 -> 19 branches | done for PS; VS to check | yes | tiny |
| **Measurements that decide other items** | | | | | |
| 139 | Console A/B 32 (3D stencil passes against the copy engine) on the Banjo screen: decides 119-123's baseline | `banjo_stencil_fetch_3d_ab.toml` | | | tiny (run) |
| 140 | Console A/B 55 (package 25's round together) and each of 49-54 with the GPU groups | `banjo_shaders25_ab.toml` | the round's real gain | | tiny (run) |
| 141 | `banjo_diag_shader_asm` on package 25 for the same 15 shaders: count what NAK still moves | `banjo_diagnostics.toml` | checks items 3-5, 16 | | tiny (run) |
| 142 | Draw counts of the 34 unnormalised-coordinate fetches and the 64 own-LOD fetches (items 45-46) | | whether those bugs show | | tiny |
| 143 | Per-draw GPU time of the material family after package 25 (`banjo_diag_gpu_groups = 2`) | | how much of 1-8 is worth doing next | | tiny (run) |
| 144 | Static instruction counts are not executed ones: the cascade merge (item 8) removes 2 of 3 blocks from the binary, but a pixel runs one cascade either way; its gain is instruction cache and divergent warps. Measure it before ranking it above 1 and 6 | | | | |
| **Checked today and ruled out** | | | | | |
| 145 | `rcp` in the flattening whitelist: only 2 blocks in the library were held back by it | | nothing | | |
| 146 | Specialisation bit 21 (1,538 of your pipelines) looked like a depth export forcing late Z: it is the half-range depth representation, and the module transform only touches shaders that already read or write depth | `TransformDepthHalf` | nothing | | |
| 147 | "Fetch constants with sign code FFFFFFFF" on every statistics line: harmless, `SPEC_CONSTANT_NO_TEXTURE_SIGNS` (bit 6, set on all 1,721 pipelines) removes the sign code whatever constant 61 says | | nothing | | |
| 148 | Splitting the frame into several submits: the renderer submits once a frame with three frames in flight, so CPU and GPU already overlap; the GPU's 15-20 ms gap is the CPU-bound frame (55 ms) less the GPU's ~40 ms, which more submits do not change | 148-150 submissions per 10 s | nothing | | |
| 149 | A faster fingerprint: `CrcFingerprint` is already four interleaved hardware CRC chains; at 1.7 GB/s it is bound by reading guest memory, not by the CRC | the prover measured 3.2 GB in 1.87 s | little | | |
| 150 | The vertex lock rule (`banjo_ring_vertex_lock_rule`, skip fingerprints of blocks no Lock touched): not exact, the game's loading writes vertex memory without a Lock (caught on PC) | `docs/enhancements-reference.md` | | no | |

Not taken (not exact): dropping the lower clamp of `clamp(rsqrt(dot(v, v)))` (differs for NaN); folding `x + 0.0`
(differs for −0); FP16 math; moving constant-only subexpressions to the vertex shader (the two stages can fuse
multiply-adds differently); gather for the blur taps (bilinear, not point); texel offsets in the texture instruction
(rounds differently).

## Outside the translator: the Banjo screen at 5 fps

The screen after the title (Banjo jumping) is GPU-bound by **`edram_import9`**: the late stencil fetch of a 1280x1280
depth target, rebuilt every frame by 9 masked passes over 512 tiles (`D000:1280x1280:mx0my1->000:1280x1280:mx0my0`).

- Package 22: `edram_import9` 45.6-45.9 ms a frame, scene 17.6-18.2 ms, at 12.6-12.8 fps; one window had 249.5 ms of
  GPU per frame (a stall on top).
- Prover-off run: 28.3 ms a frame in its 13.7 fps window, 8.6 ms a frame in the level.

About 5 M stencil-pass fragments a frame (8 bit passes over the 1280x512 tile rows the scissor leaves) should take a
few milliseconds at 460 MHz, not 46. So the cost is in how the passes run, not in their pixels (items 61-67 and 92). It is the largest single GPU item on that screen. It has its own A/B already
(target 32, `banjo_stencil_fetch_3d_ab.toml`: the 3D passes against the copy engine), but that config is not in the
package's `configs/`.
