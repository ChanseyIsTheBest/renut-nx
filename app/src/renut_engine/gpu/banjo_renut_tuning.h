// banjo-nx (2026-10-17): banjo-nx's tuning of Renut's renderer for the Switch's A57 and NVK, called from a few places
// in Renut's files (tools/renut_patches/banjo.patch). Each part has its own key in the enhancement table
// (src/cpu/banjo_enhancements.cpp); docs/renut-renderer-port.md describes them.
//
//   - Thread placement: Renut's render thread and its pipeline prebuild workers are plain std::threads, which libnx
//     starts on the process's default core. The render thread goes to banjo_ring_core (the core banjo-nx's own ring
//     thread, the same job, is pinned to), the workers to banjo_background_compile_core at a lower priority, and
//     there are banjo_prewarm_threads of them. No effect on the PC.
//   - banjo_renut_pipeline_cache: the driver's compiled pipelines persist in cache/renut_pipelines.bin (plume's
//     persistent VkPipelineCache, from UnleashedRecomp-NX), so a start builds the listed pipelines from the file
//     instead of compiling them (banjo_mesa_disk_cache_off leaves Mesa's own disk cache off).
//   - banjo_renut_precise_barriers: plume's barrier access masks derived from the layouts and declared accesses
//     instead of MEMORY_READ | MEMORY_WRITE on both sides, which NVK turns into cache flushes and invalidations
//     (UnleashedRecomp-NX's SwitchPreciseBarriers).
//   - banjo_renut_pmull_hash (ARM only): the content hashes Renut takes of guest data every frame (vertex streams,
//     index ranges, texture samples and the CPU-written textures' contents) use banjo-nx's PMULL-folded 64-bit CRC
//     (me/masseffect/banjo_pmull_fingerprint.h, banjo_ring_pmull_fingerprint) instead of XXH3: half the instructions
//     per byte on the A57. The shader containers keep XXH3 (the shader cache is keyed by it).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <xxhash.h>

namespace plume {
struct RenderDevice;
struct RenderPipeline;
}

namespace banjo::renut_gpu {

// A relative path from a key (renut_pipeline_list) next to the executable rather than in the working directory, which
// the Switch's loader does not promise to be the NRO's folder (banjo-nx's own caches live there too).
std::filesystem::path NextToExecutable(const std::string &path);

// Starts 'fn' on a thread that is never joined: a std::thread elsewhere, a pthread with 'stack_bytes' of stack on the
// Switch (std::thread::detach throws there, and std::thread cannot choose its stack).
void StartLongLivedThread(void (*fn)(), size_t stack_bytes);

// On the render thread, as it starts (backend/render_thread.cpp).
void OnRenderThreadStart();
// On a pipeline prebuild worker, as it starts (render/pipeline.cpp).
void OnPrebuildWorkerStart();
// On any other background worker of the renderer, as it starts (the content verifier, render/draw.cpp): the
// background core and priority, as the prebuild workers.
void OnBackgroundThreadStart(const char *what);
// How many prebuild workers to start; Renut's own count off the Switch.
uint32_t PrebuildWorkers(uint32_t renut_count);
// banjo_renut_upload_cached (the Switch only): the upload ring in CPU-cached memory (backend/video.cpp).
bool UploadCached();
// banjo_renut_push_point_only: the draw's push constant only for point sprites (render/draw.cpp).
bool PushPointOnly();
// banjo_renut_capture_pool: draw captures from a pool (backend/queued.cpp).
bool CapturePoolOn();
// banjo_renut_neon_swap (ARM): the vertex conversion's byte swaps 16 bytes at a time (render/vertex_format.cpp).
bool NeonSwapOn();
// banjo_renut_queue_notify_idle: a queued item wakes the render thread only while it waits (backend/render_thread.cpp).
bool QueueNotifyIdleOn();
// banjo_renut_shared_dedupe: a draw whose SharedConstants equal the last upload's reuses it (render/draw.cpp), counted
// for the [renut] report.
bool SharedDedupeOn();
void CountSharedConstants(bool reused);

// banjo_renut_exact_streams (package 27): Renut keeps converted copies of vertex streams and index ranges whose guest
// bytes held for a few flushes, and checks them on a background thread once every 4 frames, so a range the game
// rewrites can draw from its old copy for about 5 frames. With the key a copy is hashed whole on its first use after
// each kick (the point where the game may learn the GPU is done and rewrite the buffer; render/draw.cpp), as banjo-nx's
// renderer proves its persistent vertex blocks: a changed range is converted again before the draw. Not an
// enhancement but the exact rule: no master switch. Counted for the [renut] report: checks, bytes hashed, stale copies
// caught (and, with the key off, the ones Renut's background check found after the fact).
bool ExactStreamsOn();
void CountExactCheck(uint64_t bytes, bool stale, bool repeat_in_frame);
void CountKick();        // a KickOff reached the render thread (FlushVertexFillsLocked)
void CountDrawKind(bool depth_only, bool depth_only_plain, bool writes_nothing, bool partial_mask);  // package 28
void CountExactMemoHit(uint64_t bytes);  // a check answered by the interval memo (no bytes read)
void CountFenceQuery();  // the game asked whether a fence is done (BlockOnFence / IsFencePending)
void CountLateStale(uint64_t copies);

// banjo_renut_memory_policy (package 27; on by default on the Switch): Renut's memory limits are sized for a PC's
// gigabytes; NVK on the console gives about 1,280 MB of GPU memory budget. Not an enhancement (no master switch): the
// same picture, made to fit. Its parameters, each with Renut's own number when the policy is off:
//   - cold host textures (not bound for 120 frames) are released, and made again from guest memory if bound later,
//     when less than banjo_renut_textures_reserve_mb of the budget is left or the bindless table is three quarters
//     full (render/textures.cpp); 0 when off. Never a texture bound since (the lists in flight are 2 frames), never
//     one the GPU wrote (banjo_renut_keep_gpu_written); sa2-nx's rule, after its console glitches came from too little
//     eviction (a full descriptor table) and stale resolve memory, not from evicting;
//   - the converted-stream cache stops taking copies at banjo_renut_stream_cache_mb (render/draw.cpp), and a copy no
//     draw read for banjo_renut_stream_cache_idle_s goes;
//   - upload ring chunks a heavy frame added (a loading frame's texture uploads) are released after
//     banjo_renut_upload_trim_frames frames without such a frame (render/upload_ring.cpp).
uint64_t TextureReserveBytes();
uint64_t StreamCacheMaxBytes(uint64_t renut_max);
uint64_t StreamCacheIdleFrames(uint64_t renut_frames);
uint32_t UploadTrimResets(uint32_t renut_resets, uint32_t frames_in_flight);
void CountReserveEviction(uint64_t textures);

// banjo_renut_keep_gpu_written (package 27): a host texture the GPU wrote (a resolve destination) is never released by
// an eviction rule. Renut's resolves stay on the GPU (nothing is written back to guest memory), so such a texture made
// again from guest memory would hold whatever that memory holds: Renut's own backstop released them after 3,600 idle
// frames. The game freeing or reusing their memory still replaces them. Not an enhancement: no master switch.
bool KeepGpuWritten();
// banjo_gpu_nvk_draw_contract under Renut's renderer (package 27; the Switch): NVK's per-draw paths (draw emission in
// one go, only the dirty cbuf slots and dynamic state groups, pipeline prefetch) and its dynamic descriptors by
// differences (masseffect_native_set4_differences: sets 5-7 are re-bound with new offsets every draw) are process-wide
// requests in NVK (nvk_switch_dibujo / nvk_switch_set4 in the linked Mesa, each exact and checked by its own guard).
// banjo-nx's renderer makes them; with Renut's renderer nobody did. Called once a frame from the render thread
// (requests, guards), every 10 s from the report (NVK's own figures), and before each pipeline bind (the prefetch hint).
void ControlNvkLocked();

// banjo_gpu_bool_spec under Renut's renderer (package 27; Vulkan): the bools a draw's shaders test as specialization
// constants with the draw's values (banjo_bool_spec.h in the translator, the key's bool words in render/pipeline.h). A
// combination not built yet draws with the generic pipeline while a prebuild worker builds it. In-run A/B target 7.
bool BoolSpecOn();
// banjo_gpu_blend_skip under Renut's renderer (package 27; Vulkan): pixels the blend would not change are discarded
// (BlendSkipMode in render/pipeline.cpp, banjoBlendSkip in shader_common.h). In-run A/B target 8.
bool BlendSkipOn();
// banjo_renut_alpha_to_mask_test (2026-10-18): alpha to coverage on a single-sample target drawn as an alpha test at 0.5.
bool AlphaToMaskTestOn();
// banjo_renut_depth_words_stencil (2026-10-18): the depth resolve's words carry the stencil.
bool DepthWordsStencilOn();
// banjo_renut_point_minmax_radius (2026-10-18): PA_SU_POINT_MINMAX read as radii (value / 8 is the diameter).
bool PointMinMaxRadiusOn();
// banjo_renut_half_pixel_offset (2026-10-18): the viewport moved half a pixel for D3D9 pixel centres.
bool HalfPixelOffsetOn();
// banjo_gpu_depth_only under Renut's renderer (package 28): depth-only draws without their pixel shader
// (DrawsWithoutPixelShader in render/draw.cpp). In-run A/B target 11. Counted for the [renut] report.
bool DepthOnlyOn();
void CountDepthOnly();
// banjo_gpu_output_trim under Renut's renderer (package 28; Vulkan): the colour channels a pipeline never uses are 0
// from the pixel shader's exits on (OutputTrimMask in render/pipeline.cpp, banjoTrim in shader_common.h). A/B 12.
bool OutputTrimOn();
bool DiagTrimPoison();  // banjo_renut_diag_trim_poison: its test
bool DiagGpuShadersOn();  // banjo_renut_diag_gpu_shaders (package 28b): the GPU profile split by pixel shader
// banjo_gpu_depth_words_view under Renut's renderer (package 28): textures over a depth resolve's memory sample its
// words texture through a view instead of a copy (WordsViewSlot in render/textures.cpp). In-run A/B target 13. Counted:
// binds of such a texture through the view or with the copy path.
bool DepthWordsViewOn();
// banjo_renut_record_worker's half of the in-run A/B (target 14), read when a command list is opened.
bool RecordWorkerAbOn();
void CountDepthWords(bool viewed);
// banjo_deko3d_draw_resolves (2026-10-18): colour resolves drawn instead of copied on deko3d.
bool DrawResolvesOn();
// 2026-10-18, a census: binds of a depth resolve's float (R32F) target, not its words.
void CountDepthFloatSampled();
// banjo_deko3d_depth_compression / banjo_deko3d_zcull (2026-10-18): read before the deko3d device is made.
bool Deko3DDepthCompressionOn();
bool Deko3DZcullOn();
// banjo_deko3d_alpha_spec (2026-10-18): the alpha test's function in the pipeline key (constant 903).
bool AlphaSpecOn();
// banjo_deko3d_tess_spec / banjo_deko3d_vs_outputs (2026-10-18): constants 904 and 905 of the deko3d shaders.
bool TessSpecOn();
bool Deko3DVsOutputsOn();
// banjo_deko3d_uam_prmt (2026-10-18): uam's PRMT pass, and the DKSH cache file that goes with it.
bool Deko3DUamPrmtOn();
// banjo_deko3d_early_hoist: constant 907 of the deko3d GLSL (tools/deko3d_glsl_opt.py earlyhoist) left at 1.
bool EarlyHoistOn();
// uam's banjo flags (banjo_deko3d_uam_prmt, banjo_deko3d_uam_gvn, banjo_deko3d_uam_uniform_branches); 447 by default.
unsigned Deko3DUamFlags();
// banjo_background_compile_core / banjo_deko3d_compile_priority (2026-10-18): the uam thread's core and priority.
int Deko3DCompileCore();
int Deko3DCompilePriority();
void CountDrawnResolve();
// banjo_renut_resolve_skip (2026-10-19): a resolve whose source and destination are as at the same resolve before is
// not done again (render/draw.cpp ResolveStateLocked); counted per kind.
bool ResolveSkipOn();
// banjo_renut_depth_words_lazy (2026-10-19): depth resolve words only where read; counted 0 a resolve without them,
// 1 written late, 2 read stale (the source had changed).
bool DepthWordsLazyOn();
void CountLazyWords(int what);
void CountSkippedResolve(bool depth);
// banjo_renut_cache_save_quiet (package 28): a pipeline was built (render/pipeline.cpp Build, any thread).
void NotePipelineBuilt();
// banjo_gpu_tess_grid under Renut's renderer (package 27): tessellated draws indexed by grid vertex, so the vertex
// shader runs once per grid vertex instead of once per triangle corner (TessellationGridFor in render/draw.cpp). In-run
// A/B target 9. Counted: corners drawn and grid vertices, for the [renut] report.
bool TessGridOn();
void CountTessGrid(uint64_t corners, uint64_t vertices);
enum class BoolSpecEvent { kDraw, kGeneric, kBuilt };
void CountBoolSpec(BoolSpecEvent event);
void ReportNvk();
void BeforePipelineBind(plume::RenderPipeline *pipeline);
// banjo_renut_diag_poison_slots (a test): a descriptor slot given back points at a magenta texture instead of the null
// one, so a draw that still samples a released texture shows magenta in the captures (backend/video.cpp).
bool PoisonSlotsOn();
int DiagBuildDelayMs();
void ResolveTraceFrameStart();  // banjo_renut_diag_resolve_trace: the render thread's frame boundary
bool ResolveTraceOn();          // this frame is traced  // banjo_renut_diag_build_delay_ms (a test): a wait before every pipeline build

// Around the plume device (backend/video.cpp): before it is created, after, and at shutdown once the prebuild has
// stopped and the queue is idle.
void BeforeDeviceCreated();
void OnDeviceCreated(plume::RenderDevice *device);
void BeforeDeviceDestroyed();

// The report and the in-run A/B (banjo_renut_ab_target, banjo_renut_ab_s), fed by renut_engine/perf/frame_stats.cpp:
// every 10 s one "[renut]" line (game frames, the render thread's busy time and the GPU's time per frame); with a
// target, its part switched off and on every banjo_renut_ab_s seconds and the two halves' figures in a
// "[renut A/B]" line. Targets: 1 precise barriers, 2 the render thread's pin to banjo_ring_core (the Switch),
// 3 SharedConstants reuse, 4 the push constant skip, 5 the capture pool, 6 banjo_renut_exact_streams (its cost).
void StatsGameFrame();                     // a D3DDevice_Swap (game thread)
void StatsRenderThreadFrame(double busy_ms);  // the render thread's frame (render thread)
void StatsGpuFrame(double ms);             // a frame's GPU time

// banjo_renut_pmull_hash: chosen once, before the renderer's first draw (BeforeDeviceCreated), after a self-test.
extern bool g_pmull_hash;
uint64_t PmullHash(const void *data, size_t size, uint64_t seed);

// The content hash of guest data: only ever compared with hashes this function made in the same session.
inline uint64_t ContentHash64(const void *data, size_t size, uint64_t seed = 0) {
  if (g_pmull_hash)
    return PmullHash(data, size, seed);
  return XXH3_64bits_withSeed(data, size, seed);
}

}  // namespace banjo::renut_gpu
