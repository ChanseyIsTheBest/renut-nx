/**
 * @file    gpu/guest/command_buffers.cpp
 */
#include "renut_engine/gpu/guest/command_buffers.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <type_traits>
#include <utility>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#include <vector>

#include <fmt/format.h>
#include <rex/frame_latency.h>  // banjo-nx (2026-10-26): the Unleashed census's frames

#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/d3d/d3d_native.h"  // banjo-nx (2026-10-26): the setters' marks (banjo_renut_tags)
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/guest/callbacks.h"  // banjo-nx (from sa2-nx): HookFastPathsOn
#include "renut_engine/gpu/backend/queued.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/banjo_renut_tuning.h"
#include "renut_engine/gpu/render/draw.h"  // banjo-nx (2026-10-20): ChainHint
#include "renut_engine/gpu/guest/resources.h"  // banjo-nx (2026-10-23): PrefetchShaderLocked
#include "renut_engine/gpu/render/texture_layout.h"  // banjo-nx (2026-10-27): the slots' extents (GetTextureDataSizes)

#include <rex/cvar.h>

// banjo-nx (2026-10-26): recording from the setters' marks (d3d_native.h).
REXCVAR_DEFINE_BOOL(banjo_renut_tags, true, "Banjo/Enhancements",
                    "Renut's renderer (2026-10-26): a recorded op compares only the device chunks the native D3D "
                    "setters marked as written since the device's previous op, not all 53 (the same op, the same "
                    "bytes: the rest are unchanged). Off: every chunk compared, as before")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(banjo_renut_tags_check, 32, "Banjo/Enhancements",
                     "Renut's renderer (banjo_renut_tags): every Nth op of a recording device also compares every "
                     "chunk; one changed without a mark (a write the setters do not see) goes into the op, is logged, "
                     "and the marks are not used again. 1: the verify form, every op checked, the marks kept, every "
                     "such chunk counted and logged. 0: never checked")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// banjo-nx (2026-10-20): sa2-nx's chain reuse (render/draw.cpp, banjo_renut_chain_reuse) for the replayed draws too.
REXCVAR_DECLARE(bool, banjo_renut_diag_fast_census);  // banjo-nx (2026-10-26): render/draw.cpp
REXCVAR_DEFINE_BOOL(banjo_renut_chain_reuse_replays, true, "Banjo/Enhancements",
                    "OFF by default since package 36r, with banjo_renut_chain_reuse. Renut's renderer (banjo_renut_chain_reuse): the draws replayed from the game's recorded command "
                    "buffers (about three quarters of N&B's draws in play) tell the draw which device chunks their ops "
                    "changed since the replay's previous draw, so it takes that draw's pipeline key, targets, viewport, "
                    "texture words and pipeline when nothing they are made from changed (sa2-nx's chain reuse is for the "
                    "immediate draws only). banjo_renut_chain_reuse_verify checks these too. Off: replayed draws make "
                    "them all again")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// banjo-nx (2026-10-27, the shadow sort; the user's request after the render thread night).
// OFF by default since the console's city run of 2026-10-27 (builds/2026-10-27_1047_shadowsort, handheld, stock clocks,
// A/B 40 over 2 periods): render thread +1.13 ms a frame (56.72 sorted against 55.59 as recorded) and GPU +0.65 ms
// (42.06 against 41.42). Exact (verify: millions of devices compared, 0 differ), and it cut the shader changes inside
// the shadow runs by about half on the console (70 % on the emulator's title), but: the game's own order is close to
// front to back, which the depth test's early rejection likes, and sorting by shader throws that away (the GPU's
// loss); in the city the runs are short (an unsortable draw ends most of them: 35 % of the draws sortable, about 155 k
// a 10 s window refused by their shaders); and each sorted draw copies its device bytes from all over the run's arena,
// where the recorded order reads one op's bytes in a row. The emulator's census: 7.39 us a shadow draw sorted against
// 7.48 as recorded (an even trade there). Kept for its A/B (target 40, with this key on) and as the base of a sort that
// keeps the front-to-back order within each shader (not tried).
REXCVAR_DEFINE_BOOL(banjo_renut_shadow_sort, false, "Banjo/Enhancements",
                    "OFF by default (2026-10-27 console run: render thread +1.1 ms, GPU +0.65 ms a frame). "
                    "Renut's renderer (2026-10-27): the draws of a recorded command buffer that write depth only (a depth "
                    "target, no colour written, no stencil, a less / less-equal or greater / greater-equal depth test) "
                    "and follow one another into the same targets are replayed sorted by vertex shader, pixel shader and "
                    "declaration: such draws leave the same depth in any order (each sample keeps the nearest), and "
                    "sorted they change shaders and pipelines far less often. A run whose shaders export memory, skin "
                    "once or are the fur family is replayed as recorded. In-run A/B target 40. Read at start")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// banjo-nx (2026-10-27): the replay's look-ahead (2026-10-23 / 2026-10-26: while a draw is drawn, the next op's
// shader lookups, guest headers and texture memo entries are prefetched from its chunks in the arena). OFF by default
// since the console's city run of 2026-10-27 (A/B 29, one period): render thread 55.88 ms a frame with it against 53.73
// without (+2.15 ms, +4.0 %). Its loads were cheap on the emulator and their misses were most of a draw's loads in the
// 2026-10-23 profile, but on the A57 the peeks (PeekNextWord: seven words a texture slot, the header chunks' words, a
// memo entry hashed per slot) cost more than the misses they hide: the draw's own prefetches (FastDrawLocked,
// WholeGeometryTry) already reach most of those lines a few hundred cycles ahead.
REXCVAR_DEFINE_BOOL(banjo_renut_replay_lookahead, false, "Banjo/Enhancements",
                    "OFF by default (2026-10-27 console run: render thread +2.15 ms a frame with it). Renut's renderer "
                    "(2026-10-23): while a replayed draw is drawn, the next op's shader lookups, guest headers and texture "
                    "memo entries are prefetched from its recorded chunks. In-run A/B target 29 with this key on")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_renut_shadow_sort_verify, false, "Banjo/Debug",
                    "Renut's renderer (banjo_renut_shadow_sort): each sorted run's ops are also applied as recorded "
                    "and the device each sorted draw (and the run's end) is drawn from is compared whole with the one "
                    "its op had as recorded; a difference is logged and counted in the shadow sort line. Slow")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace renut::gpu {

namespace {

constexpr size_t kNone = SIZE_MAX;
// RunCommandBuffer inside a recorded buffer, and so on.
constexpr u32 kMaxRunDepth = 8;

constexpr u32 kVertexShaderF =
    u32(offsetof(d3d::Device, m_Constants) + offsetof(d3d::Constants, VertexShaderF));
constexpr u32 kPixelShaderF =
    u32(offsetof(d3d::Device, m_Constants) + offsetof(d3d::Constants, PixelShaderF));
// One m_Pending bit per 4 float constants, most significant bit first
// (SetPending_AluConstants 0x8222D0E8).
constexpr u32 kConstantGroupBytes = 4 * 16;

// What an op keeps of the device (see Op::state). The draw, clear and resolve
// paths read nothing before the fetch constants (dirty masks, ring pointers,
// the Set/Get*State call tables), and the float constants, two thirds of the
// snapshot, are kept as the groups written since the device's previous op.
constexpr u32 kFetchBegin = u32(offsetof(d3d::Device, m_Constants));
constexpr u32 kFetchBytes = kVertexShaderF - kFetchBegin;
constexpr u32 kFloatBytes = 2 * 256 * 16;
constexpr u32 kTailBegin = kVertexShaderF + kFloatBytes;
constexpr u32 kTailBytes = d3d::kDeviceSnapshotSize - kTailBegin;
static_assert(kPixelShaderF == kVertexShaderF + 256 * 16);
static_assert(kTailBegin == u32(offsetof(d3d::Device, m_Constants) + offsetof(d3d::Constants, VertexShaderB)));

// The rest, the fetch constants and the tail, in 64-byte chunks: an op keeps
// the chunks that differ from the device's previous op (Op::plain_dirty, bit
// i = chunk i), which for most draws are a few bindings and render states.
// Replaying them was a copy of the whole of both per draw on the render
// thread; comparing them is the recording threads' work.
constexpr u32 kChunkBytes = 64;
constexpr u32 kFetchChunks = kFetchBytes / kChunkBytes;
constexpr u32 kTailChunks = (kTailBytes + kChunkBytes - 1) / kChunkBytes;
constexpr u32 kPlainChunks = kFetchChunks + kTailChunks;
static_assert(kFetchBytes % kChunkBytes == 0 && kPlainChunks <= 64);
constexpr uint64_t kAllChunks = kPlainChunks == 64 ? ~uint64_t(0) : (uint64_t(1) << kPlainChunks) - 1;

// Chunk i's offset in the device, and its size (the tail's last is short).
// banjo-nx (2026-10-20, banjo_renut_chain_reuse_replays): an op's chunks (plain_dirty) as draw.h's ChainHint chunks:
// the fetch constants' twelve are the chain's 18-29 (device bytes 0x480-0x77F), the tail's are the chain's from 30.
constexpr u32 kChainFirstFetchChunk = kFetchBegin / kChunkBytes;
static_assert(kFetchBegin % kChunkBytes == 0 && kChainFirstFetchChunk + kFetchChunks == kChainHeadChunks &&
              kTailBegin == kChainTailBegin && kChainHeadChunks + kTailChunks <= 128);
// banjo-nx (2026-10-26): as shifts (a loop over the chunks before): the fetch constants' up by 18, the tail's from 30.
static_assert(kFetchChunks == 12 && kChainFirstFetchChunk == 18 && kChainHeadChunks == 30 &&
              kChainHeadChunks + kPlainChunks - kFetchChunks <= 128);
void PlainToChain(uint64_t plain, uint64_t out[2]) {
  const uint64_t tail = plain >> kFetchChunks;
  out[0] = (plain & ((uint64_t(1) << kFetchChunks) - 1)) << kChainFirstFetchChunk | tail << kChainHeadChunks;
  out[1] = tail >> (64 - kChainHeadChunks);
}

constexpr u32 ChunkOffset(u32 i) {
  return i < kFetchChunks ? kFetchBegin + i * kChunkBytes
                          : kTailBegin + (i - kFetchChunks) * kChunkBytes;
}
// banjo-nx (2026-10-23, batch 6): the tail's last chunk, the only one not 64 bytes: its copies as a fixed-size copy the
// compiler keeps inline (a C library call each before; 1.3 % of the render thread at ApplyState's).
constexpr u32 kLastChunkBytes = kTailBytes - (kTailChunks - 1) * kChunkBytes;
static_assert(kLastChunkBytes > 0 && kLastChunkBytes <= kChunkBytes);

constexpr u32 ChunkSize(u32 i) {
  return i < kFetchChunks ? kChunkBytes
                          : std::min(kChunkBytes, kTailBytes - (i - kFetchChunks) * kChunkBytes);
}

// Calls f(first, count) for each run of consecutive float constant groups in
// an m_Pending mask (bit 63 - group).
template <typename F>
void ForEachGroupRun(uint64_t mask, F f) {
  while (mask) {
    const u32 first = u32(std::countl_zero(mask));
    const u32 count = u32(std::countl_one(mask << first));
    f(first, count);
    mask = first + count >= 64 ? 0 : mask & (~uint64_t(0) >> (first + count));
  }
}

enum class OpKind { Draw, Clear, Resolve, Run };

struct Op {
  OpKind kind = OpKind::Draw;
  // banjo-nx (2026-10-27, Phase A): the arena offset of the op's resource record (RecordResources), or 0xFFFFFFFF.
  // In the padding after 'kind'.
  u32 resources = 0xFFFFFFFFu;
  // The device in the arena (CopyState): the chunks in plain_dirty, then the
  // float constant groups in vs_dirty and ps_dirty. Replay rebuilds the whole
  // snapshot from the previous op's (ApplyState).
  size_t state = kNone;
  // Chunks that differ from the device's previous op; all for a buffer's first.
  uint64_t plain_dirty = kAllChunks;
  // banjo-nx (2026-10-26, Unleashed-style step 1): the device fields (draw.h DrawField) that differ from the device's
  // previous op, found word by word; all for a buffer's first.
  u32 fields = kFieldsAll;
  // banjo-nx (2026-10-27, banjo_renut_record_extents): the arena offset of the extents of the texture slots this op's
  // chunks changed (RecordExtents), or kNoExtents (0xFFFFFFFF). Next to 'fields', in its padding: Replay reads it with
  // the op's first line.
  u32 extents = 0xFFFFFFFFu;
  // Float constant groups the buffer had set by this op (m_Pending[0] / [1]
  // bits); the rest are inherited from the running device.
  uint64_t vs_set = 0, ps_set = 0;
  // Those written since the device's previous op (ReplayConstants::vs_dirty),
  // all for a buffer's first op. Only the pending-mask consumers (the draw
  // calls, RunCommandBuffer and EndCommandBuffer, all but the last hooked)
  // clear them, so the others hold what the previous op of the buffer had.
  uint64_t vs_dirty = ~uint64_t(0), ps_dirty = ~uint64_t(0);

  // Draw
  DrawCall call;
  size_t up_vertices = kNone, up_indices = kNone;  // arena offsets

  // Clear / Resolve
  u32 flags = 0;
  bool has_rect = false;
  ClearRect rect{};
  bool has_color = false;
  float color[4] = {};
  float depth = 0.0f;
  u32 stencil = 0;
  u32 dest_texture = 0;
  // The destination header when recorded; the game often builds it on the
  // recording thread's stack.
  bool has_dest_format = false;
  d3d::TextureFetchConstant dest_format{};
  i32 dest_x = 0, dest_y = 0;
  u32 dest_level = 0, dest_slice = 0;

  // Run
  u32 command_buffer = 0;

  // banjo-nx (2026-10-27, banjo_renut_shadow_sort): a draw that writes depth only (SortFacts): its key (vertex shader
  // << 32 | pixel shader, then the declaration) and its run's signature (the targets and the depth test's family);
  // sort_sig 0: not sortable, sort_why the first reason (the census).
  uint64_t sort_key = 0;
  u32 sort_decl = 0, sort_sig = 0;
  u32 sort_vs = 0, sort_ps = 0;  // the shaders' guest addresses (the key holds their contents' digests)
  u32 sort_rs = 0;  // banjo-nx (2026-10-27): a digest of the render state the pipeline is made from (the second level)
  u32 sort_tex = 0;  // and of the first texture slots' addresses (the third: alpha-tested casters share shaders, not textures)
  u8 sort_why = 0;
};
constexpr u32 kNoExtents = 0xFFFFFFFFu;

// banjo-nx (2026-10-27, setter rewrite): the arena's growth leaves its new bytes as they are: every op's state is copied
// in whole right after (CopyState), and zeroing them first was a second write of the op's bytes on the recording threads
// (their memset). Reserve zeroes what it hands out, as before.
template <typename T>
struct DefaultInitAllocator : std::allocator<T> {
  template <typename U>
  struct rebind {
    using other = DefaultInitAllocator<U>;
  };
  DefaultInitAllocator() noexcept = default;
  template <typename U>
  DefaultInitAllocator(const DefaultInitAllocator<U> &) noexcept {}
  template <typename U>
  void construct(U *p) noexcept(std::is_nothrow_default_constructible_v<U>) {
    ::new (static_cast<void *>(p)) U;
  }
  template <typename U, typename... Args>
  void construct(U *p, Args &&...args) {
    ::new (static_cast<void *>(p)) U(std::forward<Args>(args)...);
  }
};

// banjo-nx (2026-10-27, banjo_renut_shadow_sort): a run's draws in their sorted order, each with the device bytes that
// differ from the draw before it in that order: pairs of a slot (0-52 a plain chunk, 64 + g a vertex shader constant
// group, 128 + g a pixel shader one) and where its bytes are in the arena (an op's copy). The last entry draws nothing:
// it brings the device back to the run's last op as recorded, from which the next op's own delta goes on.
struct SortPair {
  u32 slot, offset;
};
constexpr u32 kSortRestore = UINT32_MAX;
struct SortEntry {
  u32 op = kSortRestore;  // the op drawn, kSortRestore for the run's last entry
  u32 first_pair = 0, pair_count = 0;
  u32 fields = 0;  // DrawField bits of the chunks changed (whole chunks)
  uint64_t plain = 0, vs_dirty = 0, ps_dirty = 0;
};
struct SortRun {
  u32 begin = 0, end = 0;  // the ops [begin, end) as recorded
  u32 first_entry = 0, entry_count = 0;
};
inline bool ShadowSortOn() {
  static const bool on = REXCVAR_GET(banjo_renut_shadow_sort);
  return on;
}
// banjo-nx (2026-10-27): banjo_renut_replay_lookahead (off: see its definition), and not in A/B 29's off half.
inline bool ReplayLookAheadOn() {
  static const bool on = REXCVAR_GET(banjo_renut_replay_lookahead);
  return on && banjo::renut_gpu::g_ab_off.load(std::memory_order_relaxed) != 29;
}
// The replay's counts (render thread).
uint64_t g_sort_replayed_runs = 0, g_sort_replayed_draws = 0, g_sort_refused_runs = 0;
uint64_t g_sort_verified = 0, g_sort_verify_differ = 0;  // banjo_renut_shadow_sort_verify's

struct HostCommandBuffer {
  std::vector<Op> ops;
  std::vector<u8, DefaultInitAllocator<u8>> arena;
  uint64_t id = 0;  // banjo-nx (2026-10-26): a number per recording (the objects are pooled), for the census
  // banjo-nx (2026-10-27, banjo_renut_shadow_sort): the sorted runs (PlanSortedRuns, by the thread that queues the
  // buffer's run: 0 none yet, 1 being made, 2 made; the render thread reads them only at 2).
  std::vector<SortRun> sort_runs;
  std::vector<SortEntry> sort_entries;
  std::vector<SortPair> sort_pairs;
  std::atomic<u32> sort_state{0};
  void ClearSortPlan() {
    sort_runs.clear();
    sort_entries.clear();
    sort_pairs.clear();
    sort_state.store(0, std::memory_order_relaxed);
  }
};
std::atomic<uint64_t> g_recording_ids{0};

// Vertex data of a recorded Begin*Vertices draw, still being written.
struct PendingVertices {
  u32 device = 0;
  u32 command_buffer = 0;
  size_t offset = 0;
  const u8 *source = nullptr;
  u32 size = 0;
};

// banjo-nx (2026-10-26, banjo_renut_tags): NewOp's counts of the marks (TagsReport).
struct TagCounts {
  uint64_t ops = 0, marked_ops = 0, marked = 0, changed = 0, checks = 0;
  // banjo-nx (2026-10-26, banjo_renut_diag_fast_census: how much of the state comes through the hooked setters): the
  // changed chunks a setter marked and those only an inline write's pending tag marked, by the fields they hold.
  uint64_t setter_changed = 0, inline_changed = 0;
  uint64_t setter_fields[18] = {}, inline_fields[18] = {};
  // banjo-nx (2026-10-26, setter rewrite): the marks by source (the setters' touches, the tags' chunks no touch marked)
  // and the op counts they are over.
  uint64_t touch_marks = 0, tag_only_marks = 0, census_ops = 0;
};
std::atomic<uint64_t> g_census_setter_changed{0}, g_census_inline_changed{0};
std::atomic<uint64_t> g_census_touch_marks{0}, g_census_tag_only_marks{0}, g_census_ops{0};
std::atomic<uint64_t> g_census_setter_fields[18], g_census_inline_fields[18];

// What NewOp keeps per recording device.
struct DeviceRecording {
  // Float constant groups the buffer has set (Op::vs_set / ps_set).
  uint64_t vs = 0, ps = 0;
  // banjo-nx (2026-10-26, banjo_renut_tags): the shadow is the device's as of its last op and the device's marks have
  // been taken since (the next op may compare only the marked chunks); ops since the last whole check.
  bool primed = false;
  u32 unchecked = 0;
  TagCounts counts;
  // The device as its last op saw it, in the device layout; only the chunks
  // (ChunkOffset) are kept.
  u8 shadow[d3d::kDeviceSnapshotSize];
};

// A RunCommandBuffer's buffer and every buffer it runs in turn, as they were
// when it was issued: the render thread replays it later, by when the game
// may be recording the next frame's into the same D3DCommandBuffers.
using BufferSet = std::unordered_map<u32, std::shared_ptr<HostCommandBuffer>>;

// Guards the three below; never taken with State().mutex (the render thread
// replays captured buffers without it). A buffer a queued run still holds is
// replaced, not cleared, when the game records it again (BeginRecording).
std::mutex g_record_mutex;
std::unordered_map<u32, std::shared_ptr<HostCommandBuffer>> g_buffers;

// Buffers no recording or queued run holds any more, kept with their capacity:
// a buffer recorded again is a fresh one while a run holds the old, and
// growing a fresh arena to a frame's worth of snapshots cost the recording
// threads more than recording. Their own lock: the last reference may drop
// with g_record_mutex held (BeginRecording) or on the render thread.
// Bounded by count and by the capacity they keep: one frame's worth of
// recordings is what is worth keeping, and an arena one unusual frame grew
// past kMaxKeptArena goes rather than holding that memory for good.
constexpr size_t kMaxFreeBuffers = 256;
constexpr size_t kMaxFreeBytes = 64u << 20;
constexpr size_t kMaxKeptArena = 8u << 20;
std::mutex g_free_mutex;
std::vector<std::unique_ptr<HostCommandBuffer>> g_free_buffers;
size_t g_free_bytes = 0;  // arena and op capacity of g_free_buffers
std::atomic<size_t> g_live_buffers{0};

size_t CapacityBytes(const HostCommandBuffer &buffer) {
  return buffer.arena.capacity() + buffer.ops.capacity() * sizeof(buffer.ops[0]);
}

std::shared_ptr<HostCommandBuffer> NewBuffer() {
  std::unique_ptr<HostCommandBuffer> buffer;
  {
    std::lock_guard lock(g_free_mutex);
    if (!g_free_buffers.empty()) {
      buffer = std::move(g_free_buffers.back());
      g_free_buffers.pop_back();
      g_free_bytes -= CapacityBytes(*buffer);
    }
  }
  g_live_buffers.fetch_add(1, std::memory_order_relaxed);
  const uint64_t recording_id = g_recording_ids.fetch_add(1, std::memory_order_relaxed) + 1;
  if (buffer) {
    buffer->ops.clear();
    buffer->arena.clear();
    buffer->ClearSortPlan();  // banjo-nx (2026-10-27)
  } else {
    buffer = std::make_unique<HostCommandBuffer>();
  }
  buffer->id = recording_id;
  return std::shared_ptr<HostCommandBuffer>(buffer.release(), [](HostCommandBuffer *retired) {
    g_live_buffers.fetch_sub(1, std::memory_order_relaxed);
    const size_t bytes = CapacityBytes(*retired);
    {
      std::lock_guard lock(g_free_mutex);
      if (g_free_buffers.size() < kMaxFreeBuffers && retired->arena.capacity() <= kMaxKeptArena &&
          g_free_bytes + bytes <= kMaxFreeBytes) {
        g_free_buffers.emplace_back(retired);
        g_free_bytes += bytes;
        return;
      }
    }
    delete retired;
  });
}
std::unordered_map<u32, DeviceRecording> g_recording_devices;
std::vector<PendingVertices> g_pending_vertices;

// banjo-nx (2026-10-19, CPU proposal 3, banjo_renut_cpu_memos): NewOp's lookups remembered per recording thread, so an
// op takes g_record_mutex only when something changed. g_record_generation is bumped (under the mutex) whenever a
// buffer is made, replaced or released; a DeviceRecording never moves (map nodes, never erased). g_pending_count
// mirrors g_pending_vertices.size(): with anything pending, NewOp takes the lock to copy it in first.
std::atomic<uint64_t> g_record_generation{1};
std::atomic<size_t> g_pending_count{0};
struct RecordCache {
  u32 device_va = 0, command_buffer = 0;
  uint64_t generation = 0;
  HostCommandBuffer *buffer = nullptr;
  DeviceRecording *set = nullptr;
};
thread_local RecordCache t_record_cache;
// banjo-nx (2026-10-27, render thread night): the memo kept per recording device, by its g_touch slot (a device records
// on one thread at a time; its own buffer is only that thread's until it runs), for the devices that have one: a
// thread_local is a call on the Switch (__aarch64_read_tp), at every op. The same key and the same checks.
struct alignas(64) SlotRecordCache {
  RecordCache c;
};
SlotRecordCache g_record_cache_slots[native::kTouchSlots];

void SyncPendingCount() { g_pending_count.store(g_pending_vertices.size(), std::memory_order_release); }

u32 RecordingBuffer(u32 device_va) {
  const auto *word = GuestPtr<const be_u32>(device_va + d3d::kDeviceOffsetCommandBuffer);
  return word ? u32(*word) : 0u;
}

size_t Reserve(HostCommandBuffer &buffer, size_t size) {
  const size_t offset = (buffer.arena.size() + 15) & ~size_t(15);
  buffer.arena.resize(offset + size);
  std::memset(buffer.arena.data() + offset, 0, size);  // banjo-nx (2026-10-27): as the zeroing resize gave them
  return offset;
}

// Appends rather than going through Reserve: resize would zero the bytes the
// copy then overwrites (a device snapshot per op).
size_t CopyIn(HostCommandBuffer &buffer, const u8 *data, size_t size) {
  buffer.arena.resize((buffer.arena.size() + 15) & ~size_t(15));
  const size_t offset = buffer.arena.size();
  buffer.arena.insert(buffer.arena.end(), data, data + size);
  return offset;
}

// Whether two whole chunks differ: fixed-size loads the compiler keeps inline
// (and vectorizes), where a memcmp call per chunk cost more than the bytes.
bool ChunkDiffers(const u8 *a, const u8 *b) {
#if defined(__aarch64__)
  // banjo-nx (2026-10-23, batch 5): four 16-byte loads a side, one OR tree, one test (the recording threads' NewOp
  // runs this for every chunk of every op: the largest host cost of the game threads).
  const uint8x16_t d0 = veorq_u8(vld1q_u8(a), vld1q_u8(b)), d1 = veorq_u8(vld1q_u8(a + 16), vld1q_u8(b + 16));
  const uint8x16_t d2 = veorq_u8(vld1q_u8(a + 32), vld1q_u8(b + 32)), d3 = veorq_u8(vld1q_u8(a + 48), vld1q_u8(b + 48));
  const uint64x2_t o = vreinterpretq_u64_u8(vorrq_u8(vorrq_u8(d0, d1), vorrq_u8(d2, d3)));
  return (vgetq_lane_u64(o, 0) | vgetq_lane_u64(o, 1)) != 0;
#endif
  uint64_t diff = 0;
  for (u32 k = 0; k < kChunkBytes; k += 8) {
    uint64_t x, y;
    std::memcpy(&x, a + k, 8);
    std::memcpy(&y, b + k, 8);
    diff |= x ^ y;
  }
  return diff != 0;
}

#if defined(__aarch64__)
// banjo-nx (2026-10-23, batch 6b): whether any of four consecutive whole chunks from 'a' differs from 'b', as bits 0-3:
// all sixteen loads of each side issued before any test (the recording threads' NewOp waited on each chunk's loads in
// turn: its compare line was 6 % of each recording thread), no branch per chunk.
inline uint64_t FourChunksDiffer(const u8 *a, const u8 *b) {
  uint64_t bits = 0;
  uint8x16_t o[4];
  for (u32 k = 0; k < 4; ++k) {
    const u8 *x = a + k * kChunkBytes, *y = b + k * kChunkBytes;
    const uint8x16_t d0 = veorq_u8(vld1q_u8(x), vld1q_u8(y)), d1 = veorq_u8(vld1q_u8(x + 16), vld1q_u8(y + 16));
    const uint8x16_t d2 = veorq_u8(vld1q_u8(x + 32), vld1q_u8(y + 32)), d3 = veorq_u8(vld1q_u8(x + 48), vld1q_u8(y + 48));
    o[k] = vorrq_u8(vorrq_u8(d0, d1), vorrq_u8(d2, d3));
  }
  for (u32 k = 0; k < 4; ++k)
    bits |= uint64_t(vmaxvq_u8(o[k]) != 0) << k;
  return bits;
}
#endif

// The chunks of 'device' that differ from 'shadow', which takes them: a chunk at a time.
uint64_t ChangedChunksReference(const u8 *device, u8 *shadow) {
  uint64_t changed = 0;
  for (u32 i = 0; i < kPlainChunks; ++i) {
    const u32 at = ChunkOffset(i), size = ChunkSize(i);
    if (std::memcmp(device + at, shadow + at, size) != 0) {
      std::memcpy(shadow + at, device + at, size);
      changed |= uint64_t(1) << i;
    }
  }
  return changed;
}

// banjo-nx (2026-10-26): the chunks of 'device' that differ from 'shadow' (ChangedChunks' compare; the shadow is left).
uint64_t DifferingChunks(const u8 *device, const u8 *shadow) {
#if defined(__aarch64__)
  // banjo-nx (2026-10-23, batch 6b): the fetch constants' chunks and the tail's whole ones four at a time (the two
  // regions are each contiguous), the tail's last (short) chunk on its own: the same mask as the loop below.
  uint64_t changed = 0;
  u32 i = 0;
  for (; i + 4 <= kFetchChunks; i += 4)
    changed |= FourChunksDiffer(device + kFetchBegin + i * kChunkBytes, shadow + kFetchBegin + i * kChunkBytes) << i;
  for (; i < kFetchChunks; ++i)
    changed |= uint64_t(ChunkDiffers(device + kFetchBegin + i * kChunkBytes, shadow + kFetchBegin + i * kChunkBytes))
               << i;
  constexpr u32 kTailWhole = kLastChunkBytes == kChunkBytes ? kTailChunks : kTailChunks - 1;
  u32 t = 0;
  for (; t + 4 <= kTailWhole; t += 4)
    changed |= FourChunksDiffer(device + kTailBegin + t * kChunkBytes, shadow + kTailBegin + t * kChunkBytes)
               << (kFetchChunks + t);
  for (; t < kTailWhole; ++t)
    changed |= uint64_t(ChunkDiffers(device + kTailBegin + t * kChunkBytes, shadow + kTailBegin + t * kChunkBytes))
               << (kFetchChunks + t);
  if constexpr (kTailWhole < kTailChunks) {
    const u32 at = kTailBegin + kTailWhole * kChunkBytes;
    changed |= uint64_t(std::memcmp(device + at, shadow + at, kLastChunkBytes) != 0) << (kFetchChunks + kTailWhole);
  }
  return changed;
#else
  uint64_t changed = 0;
  for (u32 i = 0; i < kPlainChunks; ++i) {
    const u32 at = ChunkOffset(i), size = ChunkSize(i);
    // banjo-nx (2026-10-23, batch 6): the last chunk compared at its fixed size too (inline, no C library call at
    // every op).
    const bool differs = size == kChunkBytes ? ChunkDiffers(device + at, shadow + at)
                                             : std::memcmp(device + at, shadow + at, kLastChunkBytes) != 0;
    changed |= uint64_t(differs) << i;
  }
  return changed;
#endif
}

// banjo-nx (2026-10-26): the shadow takes the device's 'chunks'.
void CopyChunks(const u8 *device, u8 *shadow, uint64_t chunks) {
  for (; chunks; chunks &= chunks - 1) {
    const u32 c = u32(std::countr_zero(chunks));
    const u32 at = ChunkOffset(c);
    // banjo-nx (2026-10-23): a whole chunk as a fixed-size copy (inline), not a C library call per changed chunk.
    if (ChunkSize(c) == kChunkBytes)
      std::memcpy(shadow + at, device + at, kChunkBytes);
    else
      std::memcpy(shadow + at, device + at, kLastChunkBytes);
  }
}

// The same (ChangedChunks below).
uint64_t ChangedChunks(const u8 *device, u8 *shadow) {
  const uint64_t changed = DifferingChunks(device, shadow);
  CopyChunks(device, shadow, changed);
  return changed;
}

// banjo-nx (2026-10-26, banjo_renut_tags): ChangedChunks for the chunks in 'marked' only, the shadow's others being the
// device's. Each marked chunk is compared and stored into the shadow at once (the device's bytes as loaded for the
// compare, stored whether or not they differ: no branch on the compare; storing equal bytes changes nothing).
static_assert(kLastChunkBytes == 40, "TakeLastChunk's loads");
inline uint64_t TakeChunk(const u8 *device, u8 *shadow) {
#if defined(__aarch64__)
  const uint8x16_t a0 = vld1q_u8(device), a1 = vld1q_u8(device + 16);
  const uint8x16_t a2 = vld1q_u8(device + 32), a3 = vld1q_u8(device + 48);
  const uint8x16_t d0 = veorq_u8(a0, vld1q_u8(shadow)), d1 = veorq_u8(a1, vld1q_u8(shadow + 16));
  const uint8x16_t d2 = veorq_u8(a2, vld1q_u8(shadow + 32)), d3 = veorq_u8(a3, vld1q_u8(shadow + 48));
  vst1q_u8(shadow, a0);
  vst1q_u8(shadow + 16, a1);
  vst1q_u8(shadow + 32, a2);
  vst1q_u8(shadow + 48, a3);
  return vmaxvq_u8(vorrq_u8(vorrq_u8(d0, d1), vorrq_u8(d2, d3))) != 0;
#else
  const bool differs = ChunkDiffers(device, shadow);
  std::memcpy(shadow, device, kChunkBytes);
  return differs;
#endif
}
inline uint64_t TakeLastChunk(const u8 *device, u8 *shadow) {
#if defined(__aarch64__)
  const uint8x16_t a0 = vld1q_u8(device), a1 = vld1q_u8(device + 16);
  const uint8x8_t a2 = vld1_u8(device + 32);
  const uint8x16_t d0 = veorq_u8(a0, vld1q_u8(shadow)), d1 = veorq_u8(a1, vld1q_u8(shadow + 16));
  const uint8x8_t d2 = veor_u8(a2, vld1_u8(shadow + 32));
  vst1q_u8(shadow, a0);
  vst1q_u8(shadow + 16, a1);
  vst1_u8(shadow + 32, a2);
  return (vmaxvq_u8(vorrq_u8(d0, d1)) | vmaxv_u8(d2)) != 0;
#else
  const bool differs = std::memcmp(device, shadow, kLastChunkBytes) != 0;
  std::memcpy(shadow, device, kLastChunkBytes);
  return differs;
#endif
}
uint64_t ChangedChunksMarked(const u8 *device, u8 *shadow, uint64_t marked) {
  constexpr u32 kLast = kPlainChunks - 1;
  constexpr u32 kLastAt = kTailBegin + (kTailChunks - 1) * kChunkBytes;
  uint64_t changed = 0;
  if ((marked >> kLast) & 1)
    changed = TakeLastChunk(device + kLastAt, shadow + kLastAt) << kLast;
  for (marked &= ~(uint64_t(1) << kLast); marked; marked &= marked - 1) {
    const u32 c = u32(std::countr_zero(marked));
    // ChunkOffset without a branch: the tail's chunks follow the fetch constants' 8 KB further on.
    const u32 at = kFetchBegin + c * kChunkBytes + (c >= kFetchChunks ? kTailBegin - kFetchBegin - kFetchBytes : 0u);
    changed |= TakeChunk(device + at, shadow + at) << c;
  }
  return changed;
}
static_assert(kPlainChunks - 1 == kFetchChunks + kTailChunks - 1 && kLastChunkBytes < kChunkBytes);

// banjo-nx (2026-10-26, Unleashed-style step 1): each chunk's fields, word by word, and the chunks holding more than
// one field (whose changed words are looked at: the targets share one with the index buffer and the streams).
struct PlainFieldTables {
  u32 chunk[64] = {};
  u32 word[64][16] = {};
  uint64_t mixed = 0;
};
constexpr PlainFieldTables MakePlainFieldTables() {
  PlainFieldTables t{};
  for (u32 c = 0; c < kPlainChunks; ++c) {
    const u32 at = ChunkOffset(c), words = ChunkSize(c) / 4;
    u32 first = 0;
    for (u32 w = 0; w < words; ++w) {
      const u32 f = FieldOfOffset(at + 4 * w);
      t.word[c][w] = f;
      t.chunk[c] |= f;
      if (w == 0)
        first = f;
      else if (f != first)
        t.mixed |= uint64_t(1) << c;
    }
  }
  return t;
}
constexpr PlainFieldTables kPlainFields = MakePlainFieldTables();
static_assert(kChunkBytes / 4 == 16 && kLastChunkBytes % 4 == 0);

// The words (bit w: bytes 4w-4w+3) of a chunk that differ.
inline u32 ChunkWordMask(const u8 *a, const u8 *b, u32 size) {
#if defined(__aarch64__)
  if (size == kChunkBytes) {
    const uint32x4_t d0 = veorq_u32(vld1q_u32(reinterpret_cast<const uint32_t *>(a)),
                                    vld1q_u32(reinterpret_cast<const uint32_t *>(b)));
    const uint32x4_t d1 = veorq_u32(vld1q_u32(reinterpret_cast<const uint32_t *>(a + 16)),
                                    vld1q_u32(reinterpret_cast<const uint32_t *>(b + 16)));
    const uint32x4_t d2 = veorq_u32(vld1q_u32(reinterpret_cast<const uint32_t *>(a + 32)),
                                    vld1q_u32(reinterpret_cast<const uint32_t *>(b + 32)));
    const uint32x4_t d3 = veorq_u32(vld1q_u32(reinterpret_cast<const uint32_t *>(a + 48)),
                                    vld1q_u32(reinterpret_cast<const uint32_t *>(b + 48)));
    // Each word's lane all ones when it differs, narrowed to a byte per word, weighted by its bit and summed.
    const uint16x8_t n01 = vcombine_u16(vmovn_u32(vtstq_u32(d0, d0)), vmovn_u32(vtstq_u32(d1, d1)));
    const uint16x8_t n23 = vcombine_u16(vmovn_u32(vtstq_u32(d2, d2)), vmovn_u32(vtstq_u32(d3, d3)));
    static const uint8_t kWeights[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    const uint8x16_t bits = vandq_u8(vcombine_u8(vmovn_u16(n01), vmovn_u16(n23)), vld1q_u8(kWeights));
    return u32(vaddv_u8(vget_low_u8(bits))) | u32(vaddv_u8(vget_high_u8(bits))) << 8;
  }
#endif
  u32 mask = 0;
  for (u32 w = 0; w * 4 < size; ++w) {
    u32 x, y;
    std::memcpy(&x, a + 4 * w, 4);
    std::memcpy(&y, b + 4 * w, 4);
    mask |= u32(x != y) << w;
  }
  return mask;
}

// The fields of the mixed chunks among 'chunks' whose words differ between 'device' and 'shadow' (before the shadow
// takes them), and of the other chunks in 'changed' as a whole (each holds one field).
u32 MixedChunkFields(const u8 *device, const u8 *shadow, uint64_t chunks) {
  u32 fields = 0;
  for (uint64_t left = chunks & kPlainFields.mixed; left; left &= left - 1) {
    const u32 c = u32(std::countr_zero(left));
    const u32 at = ChunkOffset(c);
    for (u32 words = ChunkWordMask(device + at, shadow + at, ChunkSize(c)); words; words &= words - 1)
      fields |= kPlainFields.word[c][std::countr_zero(words)];
  }
  return fields;
}
u32 WholeChunkFields(uint64_t chunks) {
  u32 fields = 0;
  for (; chunks; chunks &= chunks - 1)
    fields |= kPlainFields.chunk[std::countr_zero(chunks)];
  return fields;
}
static_assert(native::kTouchFetchBegin == kFetchBegin && native::kTouchFetchEnd == kFetchBegin + kFetchBytes &&
              native::kTouchTailBegin == kTailBegin && native::TouchBit(kFetchBegin) == 1 &&
              native::TouchBit(kTailBegin) == uint64_t(1) << kFetchChunks &&
              native::TouchBit(d3d::kDeviceSnapshotSize - 1) == uint64_t(1) << (kPlainChunks - 1),
              "the setters' marks are the ops' chunks");

// banjo-nx (2026-10-26, banjo_renut_tags): the marks' counts for the 10 s line, kept per recording device
// (DeviceRecording::counts: no thread_local, a call on the Switch) and added to the shared ones every kTagFlushOps ops.
constexpr u32 kTagFlushOps = 1024;
std::atomic<uint64_t> g_tag_ops{0}, g_tag_marked_ops{0}, g_tag_marked{0}, g_tag_changed{0}, g_tag_checks{0};
std::atomic<uint64_t> g_tag_unmarked{0};
std::atomic<bool> g_tags_failed{false};
std::atomic<uint64_t> g_tag_logged_chunks{0};

void FlushTagCounts(TagCounts &t) {
  g_tag_ops.fetch_add(t.ops, std::memory_order_relaxed);
  g_tag_marked_ops.fetch_add(t.marked_ops, std::memory_order_relaxed);
  g_tag_marked.fetch_add(t.marked, std::memory_order_relaxed);
  g_tag_changed.fetch_add(t.changed, std::memory_order_relaxed);
  g_tag_checks.fetch_add(t.checks, std::memory_order_relaxed);
  g_census_setter_changed.fetch_add(t.setter_changed, std::memory_order_relaxed);
  g_census_inline_changed.fetch_add(t.inline_changed, std::memory_order_relaxed);
  g_census_touch_marks.fetch_add(t.touch_marks, std::memory_order_relaxed);
  g_census_tag_only_marks.fetch_add(t.tag_only_marks, std::memory_order_relaxed);
  g_census_ops.fetch_add(t.census_ops, std::memory_order_relaxed);
  for (u32 b = 0; b < 18; ++b) {
    if (t.setter_fields[b])
      g_census_setter_fields[b].fetch_add(t.setter_fields[b], std::memory_order_relaxed);
    if (t.inline_fields[b])
      g_census_inline_fields[b].fetch_add(t.inline_fields[b], std::memory_order_relaxed);
  }
  t = {};
}

// Chunks a check found changed without a mark: each chunk's first such word logged once (its device offset names the
// field and so the writer the marks miss), counted; outside the verify form the marks are not used again.
void NoteUnmarked(const u8 *device, const u8 *shadow, uint64_t chunks, OpKind kind, bool verify,
                  const uint64_t tags[3]) {
  g_tag_unmarked.fetch_add(uint64_t(std::popcount(chunks)), std::memory_order_relaxed);
  const uint64_t fresh = chunks & ~g_tag_logged_chunks.fetch_or(chunks, std::memory_order_relaxed);
  for (uint64_t left = fresh; left; left &= left - 1) {
    const u32 c = u32(std::countr_zero(left));
    const u32 at = ChunkOffset(c), size = ChunkSize(c);
    u32 word = at;
    while (word + 4 <= at + size && std::memcmp(device + word, shadow + word, 4) == 0)
      word += 4;
    u32 now = 0, was = 0;
    if (word + 4 <= at + size) {
      std::memcpy(&now, device + word, 4);
      std::memcpy(&was, shadow + word, 4);
    }
    RGPU_WARN("Recording from marks: device +0x{:X} (chunk {}, +0x{:X}-0x{:X}) changed without a mark before a {} "
              "op: 0x{:08X}, was 0x{:08X}; tags {:016X} {:016X} {:016X} (banjo_renut_tags_check)",
              word, c, at, at + size - 1,
              kind == OpKind::Draw ? "draw" : kind == OpKind::Clear ? "clear" : kind == OpKind::Resolve ? "resolve" : "run",
              __builtin_bswap32(now), __builtin_bswap32(was), tags[0], tags[1], tags[2]);
  }
  // banjo-nx (2026-10-26, setter rewrite): with the setters' tags kept apart, a write they covered by a tag alone
  // could be the one missed: their tags go back into the devices first, and only a miss after that stops the marks.
  if (!verify && native::g_tags_apart) {
    native::g_tags_apart = false;
    RGPU_ERROR("Recording from marks: a device chunk changed without a mark; the setters' tags go into the devices again "
               "from now on (banjo_renut_tags_apart)");
    return;
  }
  if (!verify && !g_tags_failed.exchange(true, std::memory_order_relaxed))
    RGPU_ERROR("Recording from marks: a device chunk changed without a mark; every op compares every chunk from now on "
               "(banjo_renut_tags)");
}

// banjo-nx (2026-10-23, batch 6b): ChangedChunks against ChangedChunksReference once, at the first op, on made-up
// devices: each chunk alone changed (one byte at a varying place), then every chunk; the masks and the shadows must be
// the same. A difference is logged (and would make every op keep every chunk: the slow, safe form).
bool ChangedChunksSelfTest() {
  std::vector<u8> device(d3d::kDeviceSnapshotSize), a(d3d::kDeviceSnapshotSize), b(d3d::kDeviceSnapshotSize);
  uint32_t seed = 0x9E3779B9u;
  for (u8 &x : device) {
    seed = seed * 1664525u + 1013904223u;
    x = u8(seed >> 24);
  }
  bool ok = true;
  for (u32 c = 0; c <= kPlainChunks && ok; ++c) {
    a = device;
    b = device;
    std::vector<u8> changed = device;
    for (u32 i = 0; i < kPlainChunks; ++i) {
      if (c < kPlainChunks && i != c)
        continue;
      const u32 at = ChunkOffset(i) + (i * 7u + c) % ChunkSize(i);
      changed[at] ^= 0x5Au;
    }
    const uint64_t fast = ChangedChunks(changed.data(), a.data());
    const uint64_t reference = ChangedChunksReference(changed.data(), b.data());
    ok = fast == reference && a == b;
  }
  if (!ok)
    RGPU_ERROR("ChangedChunks differs from its reference form: every op keeps every chunk (batch 6b self-test)");
  return ok;
}

// An op's state (Op::state) from the device.
size_t CopyState(HostCommandBuffer &buffer, const u8 *device, uint64_t plain_dirty,
                 uint64_t vs_dirty, uint64_t ps_dirty) {
  auto &arena = buffer.arena;
  const size_t offset = (arena.size() + 15) & ~size_t(15);
  // banjo-nx (2026-10-23, the recording threads' NewOp): the op's bytes counted first and the arena grown once, then
  // fixed-size copies (inline) for whole chunks and constant groups, instead of a vector insert (a capacity test and a
  // C library copy) for each chunk and each run of groups. The same bytes in the same order.
  size_t bytes = 0;
  for (uint64_t chunks = plain_dirty; chunks; chunks &= chunks - 1)
    bytes += ChunkSize(u32(std::countr_zero(chunks)));
  bytes += size_t(std::popcount(vs_dirty) + std::popcount(ps_dirty)) * kConstantGroupBytes;
  arena.resize(offset + bytes);
  u8 *to = arena.data() + offset;
  for (uint64_t chunks = plain_dirty; chunks; chunks &= chunks - 1) {
    const u32 i = u32(std::countr_zero(chunks));
    const u8 *from = device + ChunkOffset(i);
    const u32 size = ChunkSize(i);
    if (size == kChunkBytes)
      std::memcpy(to, from, kChunkBytes);
    else
      std::memcpy(to, from, kLastChunkBytes);  // banjo-nx (2026-10-23, batch 6): the only other size
    to += size;
  }
  for (const auto [dirty, base] : {std::pair{vs_dirty, kVertexShaderF}, std::pair{ps_dirty, kPixelShaderF}}) {
    ForEachGroupRun(dirty, [&](u32 first, u32 count) {
      const u8 *from = device + base + first * kConstantGroupBytes;
      for (u32 k = 0; k < count; ++k)
        std::memcpy(to + k * kConstantGroupBytes, from + k * kConstantGroupBytes, kConstantGroupBytes);
      to += count * kConstantGroupBytes;
    });
  }
  return offset;
}

// Brings 'state', the previous op's device (or anything, before a buffer's
// first op), to the op's.
void ApplyState(u8 *state, const u8 *from, uint64_t plain_dirty, uint64_t vs_dirty,
                uint64_t ps_dirty) {
  for (uint64_t chunks = plain_dirty; chunks; chunks &= chunks - 1) {
    const u32 i = u32(std::countr_zero(chunks));
    const u32 size = ChunkSize(i);
    // Whole chunks as a fixed-size copy, which stays inline.
    if (size == kChunkBytes)
      std::memcpy(state + ChunkOffset(i), from, kChunkBytes);
    else
      std::memcpy(state + ChunkOffset(i), from, kLastChunkBytes);  // banjo-nx (2026-10-23, batch 6)
    from += size;
  }
  for (const auto [dirty, base] : {std::pair{vs_dirty, kVertexShaderF}, std::pair{ps_dirty, kPixelShaderF}}) {
    ForEachGroupRun(dirty, [&](u32 first, u32 count) {
      // banjo-nx (2026-10-23, render-thread round): group by group, a fixed-size copy the compiler keeps inline (NEON
      // pairs), instead of a C library call for each run (most runs are one to four groups).
      u8 *to = state + base + first * kConstantGroupBytes;
      for (u32 k = 0; k < count; ++k)
        std::memcpy(to + k * kConstantGroupBytes, from + k * kConstantGroupBytes, kConstantGroupBytes);
      from += count * kConstantGroupBytes;
    });
  }
}

// banjo-nx (2026-10-27, banjo_renut_shadow_sort): one slot of a sorted entry (SortPair) into the device, as ApplyState
// copies a chunk or a constant group.
inline void ApplySortPair(u8 *state, const u8 *arena, const SortPair &p) {
  const u8 *from = arena + p.offset;
  if (p.slot < 64) {
    u8 *to = state + ChunkOffset(p.slot);
    if (p.slot == kPlainChunks - 1)
      std::memcpy(to, from, kLastChunkBytes);
    else
      std::memcpy(to, from, kChunkBytes);
    return;
  }
  std::memcpy(state + (p.slot < 128 ? kVertexShaderF : kPixelShaderF) + (p.slot & 63) * kConstantGroupBytes, from,
              kConstantGroupBytes);
}

// banjo-nx (2026-10-27, banjo_renut_shadow_sort): whether the shaders of a sorted run's draws allow any order (each
// distinct key once: the run is sorted by it).
bool SortedRunShadersOk(const HostCommandBuffer &b, const SortRun &run) {
  uint64_t last = 0;
  for (u32 k = 0; k + 1 < run.entry_count; ++k) {  // (the last entry draws nothing)
    const Op &o = b.ops[b.sort_entries[run.first_entry + k].op];
    if (o.sort_key == last)
      continue;  // the same shaders' contents as the draw before (the record-time table answered for each object)
    last = o.sort_key;
    if (!SortableShadersLocked(o.sort_vs, o.sort_ps))
      return false;
  }
  return true;
}

void FlushDevicePending(u32 device_va);

// banjo-nx (2026-10-27, the user's "export checks first": record-time translation, banjo_renut_record_extents). The
// guest extent of every texture slot whose fetch words the op's chunks hold (TextureGuestExtent's values: the base
// level's first page and bytes, the mip chain's), worked out here on the recording thread, so a replayed draw's export
// check reads it with the op (Replay's per-depth table) instead of looking an answer up by fetch words on the render
// thread. Each recording device's thread has a memo of its own by the fetch words (textures repeat); a slot
// covers fetch bytes [24 r, 24 r + 24), and the first op of a buffer (every chunk) gives all 26.
struct ExtentMemoEntry {
  u32 words[6];
  SlotExtent e;
  bool valid = false;
};
struct ExtentMemo {
  ExtentMemoEntry entry[256];
};
ExtentMemo g_extent_memos[native::kTouchSlots];

// False (nothing written) for words that are not a texture's (the last slots share their bytes with vertex fetch
// constants): such a slot is left out of the record, and a draw that samples it takes the answers table.
inline bool SlotExtentOf(const d3d::TextureFetchConstant &guest, ExtentMemo *memo, SlotExtent &out) {
  const TextureFetch fetch = TextureFetch::FromGuest(guest);
  if (!fetch.IsTexture())
    return false;
  ExtentMemoEntry *m = nullptr;
  if (memo) {
    uint64_t h = 0;
    for (u32 i = 0; i < 6; ++i)
      h = (h ^ fetch.dword[i]) * 0x9E3779B97F4A7C15ull;
    m = &memo->entry[u32(h >> 56)];
    if (m->valid && std::memcmp(m->words, fetch.dword, sizeof(m->words)) == 0) {
      out = m->e;
      return true;
    }
  }
  // TextureGuestExtent's computation (render/textures.cpp), without its render-thread memo.
  u32 base_bytes = 0, mip_bytes = 0;
  GetTextureDataSizes(fetch, base_bytes, mip_bytes);
  out.pages[0] = fetch.BasePage();
  out.sizes[0] = base_bytes;
  out.pages[1] = mip_bytes ? fetch.MipPage() : 0;
  out.sizes[1] = mip_bytes;
  if (m) {
    std::memcpy(m->words, fetch.dword, sizeof(m->words));
    m->e = out;
    m->valid = true;
  }
  return true;
}

// The texture slots the fetch chunks in 'chunks' (bit c = fetch bytes [64 c, 64 c + 64)) hold any word of.
// banjo-nx (2026-10-27, ARM pass): a chunk's slots from a table, one lookup a changed chunk.
constexpr std::array<u32, kFetchChunks> kSlotsOfChunk = [] {
  std::array<u32, kFetchChunks> t{};
  for (u32 r = 0; r < d3d::kTextureFetchSlots; ++r) {
    t[r * 24 / kChunkBytes] |= 1u << r;
    t[(r * 24 + 23) / kChunkBytes] |= 1u << r;
  }
  return t;
}();
inline u32 SlotsOfFetchChunks(uint64_t chunks) {
  u32 slots = 0;
  for (uint64_t bits = chunks & ((uint64_t(1) << kFetchChunks) - 1); bits; bits &= bits - 1)
    slots |= kSlotsOfChunk[std::countr_zero(bits)];
  return slots;
}
static_assert(sizeof(d3d::TextureFetchConstant) == 24 && d3d::kTextureFetchSlots * 24 <= kFetchBytes);

// The op's extents record in the arena: the slots its chunks changed, those of them that hold a texture's words, then
// each of those slots' SlotExtent, in slot order.
u32 RecordExtents(HostCommandBuffer &buffer, const u8 *device, uint64_t plain_dirty, u32 touch_slot) {
  const u32 changed = SlotsOfFetchChunks(plain_dirty & ((uint64_t(1) << kFetchChunks) - 1));
  if (!changed)
    return kNoExtents;
  ExtentMemo *memo = touch_slot - 1 < native::kTouchSlots - 1 ? &g_extent_memos[touch_slot] : nullptr;
  const auto &fetch = reinterpret_cast<const d3d::Device *>(device)->m_Constants.TextureFetch;
  SlotExtent found[d3d::kTextureFetchSlots];
  u32 slots = 0, count = 0;
  for (u32 bits = changed; bits; bits &= bits - 1) {
    const u32 r = u32(std::countr_zero(bits));
    if (SlotExtentOf(fetch[r], memo, found[count])) {
      slots |= 1u << r;
      ++count;
    }
  }
  // The record says which slots it changed (all of 'changed': those it leaves out are unknown from here on).
  auto &arena = buffer.arena;
  const size_t offset = (arena.size() + 3) & ~size_t(3);
  arena.resize(offset + 8 + size_t(count) * sizeof(SlotExtent));
  u8 *to = arena.data() + offset;
  std::memcpy(to, &changed, 4);
  std::memcpy(to + 4, &slots, 4);
  if (count)
    std::memcpy(to + 8, found, size_t(count) * sizeof(SlotExtent));
  return u32(offset);
}

// banjo-nx (2026-10-27, the user's Phase A: resources resolved when bound). The op's resource record: the texture
// objects of the slots whose address word or fetch words its chunks changed (FindTextureObject: the guest header's
// words read here, on the recording thread, where the game has just written them), and for a draw its index buffer
// header's words (what the hardware's command stream takes at the draw). Replay keeps them per depth (DrawResources)
// and the replayed draw reads them instead of guest headers.
constexpr u32 kNoResources = 0xFFFFFFFFu;
constexpr u32 kTextureVaChunk0 = kFetchChunks + (d3d::kDeviceOffsetTextures - kTailBegin) / kChunkBytes;
static_assert(d3d::kDeviceOffsetTextures >= kTailBegin && (d3d::kDeviceOffsetTextures - kTailBegin) % 4 == 0);
// The texture slots a plain chunk holds any word of (their address word or their fetch words).
constexpr std::array<u32, kPlainChunks> kTextureSlotsOfChunk = [] {
  std::array<u32, kPlainChunks> t{};
  for (u32 r = 0; r < d3d::kTextureFetchSlots; ++r) {
    t[r * 24 / kChunkBytes] |= 1u << r;
    t[(r * 24 + 23) / kChunkBytes] |= 1u << r;
    const u32 va_at = d3d::kDeviceOffsetTextures + r * 4 - kTailBegin;
    t[kFetchChunks + va_at / kChunkBytes] |= 1u << r;
  }
  return t;
}();
struct ResourceHead {
  u32 slots = 0;   // the slots the record gives (bit r), in slot order after the head (8 bytes each)
  u32 flags = 0;   // bit 0: ib holds the draw's index buffer header words
  u32 ib[3] = {};
  u32 vb_mask = 0;  // a draw's stream sources whose header words follow the objects (bit s, 8 bytes each)
};
static_assert(sizeof(ResourceHead) == 24);
u32 RecordResources(HostCommandBuffer &buffer, const u8 *device, uint64_t plain_dirty, OpKind kind) {
  u32 slots = 0;
  for (uint64_t bits = plain_dirty & kAllChunks; bits; bits &= bits - 1)
    slots |= kTextureSlotsOfChunk[std::countr_zero(bits)];
  ResourceHead head;
  head.slots = slots;
  u32 vb_words[16][2];
  if (kind == OpKind::Draw) {
    u32 ib_va;
    std::memcpy(&ib_va, device + d3d::kDeviceOffsetIndexBuffer, 4);
    if (const auto *ib = GuestPtr<const d3d::IndexBuffer>(__builtin_bswap32(ib_va))) {
      head.ib[0] = u32(ib->resource.Common);
      head.ib[1] = u32(ib->Address);
      head.ib[2] = u32(ib->Size);
      head.flags |= 1;
    }
    // The bound stream sources' vertex buffer headers (their CPU base and size: GetStreamSource reads them).
    for (u32 s = 0; s < 16; ++s) {
      u32 sva;
      std::memcpy(&sva, device + d3d::kDeviceOffsetStreamSources + s * 4, 4);
      if (const auto *vb = sva ? GuestPtr<const d3d::VertexBuffer>(__builtin_bswap32(sva)) : nullptr) {
        vb_words[s][0] = u32(vb->Format.dword[0]);
        vb_words[s][1] = u32(vb->Format.dword[1]);
        head.vb_mask |= 1u << s;
      }
    }
  }
  if (!slots && !head.flags && !head.vb_mask)
    return kNoResources;
  auto &arena = buffer.arena;
  const size_t offset = (arena.size() + 7) & ~size_t(7);
  arena.resize(offset + sizeof(head) + size_t(std::popcount(slots)) * sizeof(TextureObject *) +
               size_t(std::popcount(head.vb_mask)) * 8);
  u8 *to = arena.data() + offset;
  std::memcpy(to, &head, sizeof(head));
  to += sizeof(head);
  for (u32 bits = slots; bits; bits &= bits - 1) {
    const u32 r = u32(std::countr_zero(bits));
    u32 va;
    std::memcpy(&va, device + d3d::kDeviceOffsetTextures + r * 4, 4);
    va = __builtin_bswap32(va);
    TextureObject *obj = nullptr;
    if (const auto *header = va ? GuestPtr<const d3d::BaseTexture>(va) : nullptr) {
      u32 words[6];
      std::memcpy(words, &header->Format, sizeof(words));
      obj = FindTextureObject(va, words);
    }
    std::memcpy(to, &obj, sizeof(obj));
    to += sizeof(obj);
  }
  for (u32 bits = head.vb_mask; bits; bits &= bits - 1) {
    std::memcpy(to, vb_words[std::countr_zero(bits)], 8);
    to += 8;
  }
  return u32(offset);
}
inline void ApplyResources(DrawResources &table, const u8 *record, bool draw) {
  ResourceHead head;
  std::memcpy(&head, record, sizeof(head));
  const u8 *from = record + sizeof(head);
  for (u32 bits = head.slots; bits; bits &= bits - 1) {
    const u32 r = u32(std::countr_zero(bits));
    TextureObject *obj;
    std::memcpy(&obj, from, sizeof(obj));
    from += sizeof(obj);
    table.slot[r] = obj;
    table.mask = obj ? table.mask | (1u << r) : table.mask & ~(1u << r);
  }
  // A draw's own index buffer and vertex buffer words (a draw without them has none: never an earlier draw's).
  if (draw) {
    table.ib_valid = (head.flags & 1) != 0;
    table.vb_mask = head.vb_mask;
  }
  if (head.flags & 1)
    std::memcpy(table.ib, head.ib, sizeof(table.ib));
  for (u32 bits = head.vb_mask; bits; bits &= bits - 1) {
    std::memcpy(table.vb[std::countr_zero(bits)], from, 8);
    from += 8;
  }
}
DrawResources g_replay_resources[kMaxRunDepth];

// Replay's side: the record into the depth's table.
inline void ApplyExtents(DrawSlotExtents &table, const u8 *record) {
  u32 changed, slots;
  std::memcpy(&changed, record, 4);
  std::memcpy(&slots, record + 4, 4);
  const u8 *from = record + 8;
  for (u32 bits = slots; bits; bits &= bits - 1) {
    std::memcpy(&table.slot[std::countr_zero(bits)], from, sizeof(SlotExtent));
    from += sizeof(SlotExtent);
  }
  table.mask = (table.mask & ~changed) | slots;
}
DrawSlotExtents g_replay_extents[kMaxRunDepth];

// A new op on the device's buffer, with the device's state and the constant
// groups set so far, after the device's pending Begin*Vertices data. Consumes
// the device's dirty masks the way the original calls do when they write the
// state into the buffer. g_record_mutex is held for the lookups only: N&B
// records on several threads at once, and the snapshot copy, most of the
// work, goes into the device's own buffer, which only its recording thread
// touches until the buffer runs.
Op *NewOp(u32 device_va, OpKind kind, HostCommandBuffer **out_buffer) {
  const u32 command_buffer = RecordingBuffer(device_va);
  auto *device = GuestPtr<d3d::Device>(device_va);
  if (!command_buffer || !device)
    return nullptr;
  HostCommandBuffer *found = nullptr;
  DeviceRecording *found_set = nullptr;
  const u32 touch_slot = native::TouchSlotOf(device_va);
  RecordCache &cache = touch_slot - 1 < native::kTouchSlots - 1 ? g_record_cache_slots[touch_slot].c : t_record_cache;
  const bool memo = banjo::renut_gpu::CpuMemosOn();
  if (memo && cache.device_va == device_va && cache.command_buffer == command_buffer &&
      cache.generation == g_record_generation.load(std::memory_order_acquire) &&
      g_pending_count.load(std::memory_order_acquire) == 0) {
    found = cache.buffer;
    found_set = cache.set;
  } else {
    std::lock_guard lock(g_record_mutex);
    FlushDevicePending(device_va);
    auto &slot = g_buffers[command_buffer];
    if (!slot) {
      slot = NewBuffer();
      g_record_generation.fetch_add(1, std::memory_order_acq_rel);
    }
    found = slot.get();
    found_set = &g_recording_devices[device_va];
    if (memo)
      cache = {device_va, command_buffer, g_record_generation.load(std::memory_order_relaxed), found, found_set};
  }
  HostCommandBuffer &buffer = *found;
  DeviceRecording &set = *found_set;
  const uint64_t vs_dirty = u64(device->m_Pending[0]);
  const uint64_t ps_dirty = u64(device->m_Pending[1]);
  // banjo-nx (2026-10-26, banjo_renut_tags): the other tags as chunks, before they are cleared (native::TagChunks).
  const uint64_t tags[3] = {u64(device->m_Pending[2]), u64(device->m_Pending[3]), u64(device->m_Pending[4])};
  const uint64_t tag_chunks = native::TagChunks(tags[0], tags[1], tags[2]);
  set.vs |= vs_dirty;
  set.ps |= ps_dirty;
  for (auto &mask : device->m_Pending)
    mask = 0;
  ForgetSnapshotDeltas(device);  // banjo-nx (2026-10-20, from sa2-nx): the masks went to the buffer, not to the next delta (queued.cpp)

  Op &op = buffer.ops.emplace_back();
  op.kind = kind;
  op.vs_set = set.vs;
  op.ps_set = set.ps;
  // The first op of a buffer follows ops of other buffers: nothing known.
  const bool first = buffer.ops.size() == 1;
  const auto *bytes = reinterpret_cast<const u8 *>(device);
  // banjo-nx (2026-10-23, batch 6b): fast_chunks. banjo-nx (2026-10-26): one static (one guard test an op, not three).
  struct ChunkConfig {
    bool fast_chunks, tags;
    u32 check_period;
  };
  static const ChunkConfig config{ChangedChunksSelfTest(), REXCVAR_GET(banjo_renut_tags),
                                  u32(std::max(0, int(REXCVAR_GET(banjo_renut_tags_check))))};
  const bool fast_chunks = config.fast_chunks;
  const u32 check_period = config.check_period;
  // banjo-nx (2026-10-26, banjo_renut_tags): the chunks the setters marked since the device's previous op (taken every
  // op, so a whole compare clears them too); only those compared when the shadow is primed.
  uint64_t marked = kAllChunks;
  const bool tracked = config.tags && native::TakeTouches(device_va, &marked);
  const uint64_t setter_marked = marked;  // banjo-nx (2026-10-26): before the inline writes' tags (the census)
  marked |= tag_chunks;
  const bool from_marks = tracked && set.primed && fast_chunks && !g_tags_failed.load(std::memory_order_relaxed) &&
                          banjo::renut_gpu::g_ab_off.load(std::memory_order_relaxed) != 21;
  set.primed = tracked;
  uint64_t changed;
  // banjo-nx (2026-10-26, Unleashed-style step 1): the mixed chunks' changed words, before the shadow takes them.
  u32 fields = MixedChunkFields(bytes, set.shadow, from_marks ? marked & kAllChunks : kAllChunks);
  if (from_marks) {
    changed = ChangedChunksMarked(bytes, set.shadow, marked & kAllChunks);
    if (check_period && ++set.unchecked >= check_period) {
      set.unchecked = 0;
      ++set.counts.checks;
      if (const uint64_t unmarked = DifferingChunks(bytes, set.shadow)) {
        NoteUnmarked(bytes, set.shadow, unmarked, kind, check_period == 1, tags);
        CopyChunks(bytes, set.shadow, unmarked);
        changed |= unmarked;
        fields |= WholeChunkFields(unmarked);  // banjo-nx (2026-10-26): as a whole (its words are gone)
      }
    }
  } else {
    changed = fast_chunks ? ChangedChunks(bytes, set.shadow) : (ChangedChunksReference(bytes, set.shadow), kAllChunks);
  }
  TagCounts &counts = set.counts;
  static const bool census = REXCVAR_GET(banjo_renut_diag_fast_census);
  if (census && from_marks) {
    const uint64_t inline_only = changed & ~setter_marked;
    const uint64_t by_setter = changed & setter_marked;
    counts.inline_changed += uint64_t(std::popcount(inline_only));
    counts.setter_changed += uint64_t(std::popcount(by_setter));
    counts.touch_marks += uint64_t(std::popcount(setter_marked & kAllChunks));
    counts.tag_only_marks += uint64_t(std::popcount(tag_chunks & ~setter_marked & kAllChunks));
    ++counts.census_ops;
    for (uint64_t left = inline_only; left; left &= left - 1) {
      for (u32 f = WholeChunkFields(left & (~left + 1)); f; f &= f - 1)
        ++counts.inline_fields[std::countr_zero(f)];
    }
    for (uint64_t left = by_setter; left; left &= left - 1) {
      for (u32 f = WholeChunkFields(left & (~left + 1)); f; f &= f - 1)
        ++counts.setter_fields[std::countr_zero(f)];
    }
  }
  ++counts.ops;
  counts.marked_ops += from_marks;
  counts.marked += from_marks ? uint64_t(std::popcount(marked & kAllChunks)) : 0;
  counts.changed += uint64_t(std::popcount(changed));
  if (counts.ops >= kTagFlushOps)
    FlushTagCounts(counts);
  op.plain_dirty = first ? kAllChunks : changed;
  op.fields = first ? kFieldsAll : fields | WholeChunkFields(changed & ~kPlainFields.mixed);
  op.vs_dirty = first ? ~uint64_t(0) : vs_dirty;
  op.ps_dirty = first ? ~uint64_t(0) : ps_dirty;
  op.state = CopyState(buffer, bytes, op.plain_dirty, op.vs_dirty, op.ps_dirty);
  // banjo-nx (2026-10-27): the changed texture slots' extents (banjo_renut_record_extents).
  if (RecordExtentsOn())
    op.extents = RecordExtents(buffer, bytes, op.plain_dirty, touch_slot);
  // banjo-nx (2026-10-27, Phase A): the op's resources (banjo_renut_resource_objects).
  if (ResourceObjectsOn())
    op.resources = RecordResources(buffer, bytes, op.plain_dirty, kind);
  *out_buffer = &buffer;
  return &op;
}

void CopyPending(const PendingVertices &p) {
  auto it = g_buffers.find(p.command_buffer);
  if (it == g_buffers.end() || !it->second || p.offset + p.size > it->second->arena.size())
    return;
  std::memcpy(it->second->arena.data() + p.offset, p.source, p.size);
}

// Copies pending vertex data selected by 'match' into its buffer. Requires
// g_record_mutex.
template <typename Match>
void FlushPending(Match match) {
  for (size_t i = 0; i < g_pending_vertices.size();) {
    if (match(g_pending_vertices[i])) {
      CopyPending(g_pending_vertices[i]);
      g_pending_vertices[i] = g_pending_vertices.back();
      g_pending_vertices.pop_back();
    } else {
      ++i;
    }
  }
  SyncPendingCount();
}

void FlushDevicePending(u32 device_va) {
  if (g_pending_vertices.empty())
    return;
  FlushPending([device_va](const PendingVertices &p) { return p.device == device_va; });
}

void MergeInherited(u8 *state, const u8 *running, uint64_t set, u32 offset) {
  if (set == ~uint64_t(0))
    return;
  for (u32 group = 0; group < 64; ++group) {
    if (!(set & (uint64_t(1) << (63 - group)))) {
      const u32 at = offset + group * kConstantGroupBytes;
      std::memcpy(state + at, running + at, kConstantGroupBytes);
    }
  }
}

// The tile predication mask of a device or snapshot; ~0 when none is set.
u32 Predication(const u8 *state) {
  const u32 mask = *reinterpret_cast<const be_u32 *>(state + d3d::kDeviceOffsetPredication);
  return mask ? mask : ~0u;
}

// banjo-nx (2026-10-23, render-thread round): a device word of the op after this one as it will be once ApplyState has
// brought the state to it: from that op's chunks when it changed the word's chunk (they are in the arena in chunk
// order, all whole but the tail's last), else from 'state', this op's. Words outside the chunks (the float constants
// and what comes before the fetch constants) are read from 'state'. Only for prefetching.
u32 PeekNextWord(const u8 *state, const u8 *next, uint64_t next_dirty, u32 offset) {
  u32 chunk = UINT32_MAX;
  if (offset >= kTailBegin)
    chunk = kFetchChunks + (offset - kTailBegin) / kChunkBytes;
  else if (offset >= kFetchBegin && offset < kFetchBegin + kFetchBytes)
    chunk = (offset - kFetchBegin) / kChunkBytes;
  const u8 *at = chunk != UINT32_MAX && ((next_dirty >> chunk) & 1)
                     ? next + std::popcount(next_dirty & ((uint64_t(1) << chunk) - 1)) * kChunkBytes +
                           (offset - ChunkOffset(chunk))
                     : state + offset;
  u32 word;
  std::memcpy(&word, at, 4);
  return __builtin_bswap32(word);
}
static_assert(d3d::kDeviceOffsetVertexShader >= kTailBegin && d3d::kDeviceOffsetPixelShader >= kTailBegin);

// banjo-nx (2026-10-23, render-thread round): the guest headers the next draw reads that it changed: its textures',
// vertex buffers' and index buffer's. Second form (same day, after the console profile gave the first 5 % of the render
// thread: 43 words tested at every draw): only the tail chunks that hold header words and that the op changed, and in
// them only the words whose value is not this draw's (this draw read those headers itself: they are cached).
struct HeaderChunk {
  u8 count = 0;
  u8 word[16];    // the word's offset in the chunk / 4
  u16 bytes[16];  // the header it names
};
struct HeaderChunks {
  uint64_t mask = 0;  // tail chunks (bit c = chunk kFetchChunks + c of the op's plain_dirty) holding header words
  HeaderChunk chunk[64];
};
constexpr HeaderChunks MakeHeaderChunks() {
  HeaderChunks h{};
  auto add = [&](u32 offset, u32 bytes) {
    const u32 c = (offset - kTailBegin) / kChunkBytes;
    HeaderChunk &k = h.chunk[c];
    k.word[k.count] = u8((offset - kTailBegin - c * kChunkBytes) / 4);
    k.bytes[k.count] = u16(bytes);
    ++k.count;
    h.mask |= uint64_t(1) << c;
  };
  for (u32 r = 0; r < d3d::kTextureFetchSlots; ++r)
    add(d3d::kDeviceOffsetTextures + r * 4, sizeof(d3d::BaseTexture));
  for (u32 stream = 0; stream < 16; ++stream)
    add(d3d::kDeviceOffsetStreamSources + stream * 4, sizeof(d3d::VertexBuffer));
  add(d3d::kDeviceOffsetIndexBuffer, sizeof(d3d::IndexBuffer));
  return h;
}
constexpr HeaderChunks kHeaderChunks = MakeHeaderChunks();
static_assert(kHeaderChunks.mask >> kTailChunks == 0, "header words past the tail chunks");

// banjo-nx (2026-10-26, the record-time draw path's round 3): the next draw's texture memo entries, for the slots this
// draw bound (FastTextureMaskGuess), each from the next op's texture word and fetch constant (its chunks where it
// changed them, else this draw's): FillTextures' lookups of the next draw, in flight while this one is drawn.
// banjo-nx (2026-10-27, render thread night): the bit of an op's plain_dirty for the chunk holding a device byte (as
// PeekNextWord finds it), 0 outside the chunks.
constexpr uint64_t ChunkBitOf(u32 offset) {
  if (offset >= kTailBegin)
    return uint64_t(1) << (kFetchChunks + (offset - kTailBegin) / kChunkBytes);
  if (offset >= kFetchBegin && offset < kFetchBegin + kFetchBytes)
    return uint64_t(1) << ((offset - kFetchBegin) / kChunkBytes);
  return 0;
}

void PrefetchNextTextures(const u8 *state, const u8 *next, uint64_t next_dirty) {
  u32 mask = FastTextureMaskGuess() & ((1u << d3d::kTextureFetchSlots) - 1u);
  for (; mask; mask &= mask - 1) {
    const u32 r = u32(std::countr_zero(mask));
    const u32 at = u32(offsetof(d3d::Device, m_Constants) + offsetof(d3d::Constants, TextureFetch)) +
                   r * u32(sizeof(d3d::TextureFetchConstant));
    // banjo-nx (2026-10-27, render thread night): a slot whose texture word and fetch constant the next op leaves as
    // they are has this draw's memo entry, which this draw's lookup brings in: nothing to fetch (seven words peeked
    // and an entry hashed for it at every draw before).
    if (!(next_dirty & (ChunkBitOf(d3d::kDeviceOffsetTextures + r * 4) | ChunkBitOf(at) |
                        ChunkBitOf(at + u32(sizeof(d3d::TextureFetchConstant)) - 1))))
      continue;
    const u32 va = PeekNextWord(state, next, next_dirty, d3d::kDeviceOffsetTextures + r * 4);
    if (!va)
      continue;
    // The fetch constant's six words as stored (big-endian), from the chunk that holds each.
    alignas(4) u8 raw[sizeof(d3d::TextureFetchConstant)];
    for (u32 w = 0; w < sizeof(raw) / 4; ++w) {
      const u32 word = __builtin_bswap32(PeekNextWord(state, next, next_dirty, at + w * 4));
      std::memcpy(raw + w * 4, &word, 4);
    }
    d3d::TextureFetchConstant fetch;
    std::memcpy(&fetch, raw, sizeof(fetch));
    PrefetchTextureMemoLocked(va, fetch);
  }
}

void PrefetchNextHeaders(const u8 *state, const u8 *next, uint64_t next_dirty) {
  uint64_t chunks = (next_dirty >> kFetchChunks) & kHeaderChunks.mask;
  if (!chunks)
    return;
  // The op's chunks are in the arena in chunk order: chunk i is at popcount(dirty below i) * 64.
  for (; chunks; chunks &= chunks - 1) {
    const u32 c = u32(std::countr_zero(chunks));
    const u32 i = kFetchChunks + c;
    const u8 *now = next + std::popcount(next_dirty & ((uint64_t(1) << i) - 1)) * kChunkBytes;
    const u8 *was = state + kTailBegin + c * kChunkBytes;
    const HeaderChunk &k = kHeaderChunks.chunk[c];
    for (u32 w = 0; w < k.count; ++w) {
      u32 word, old;
      std::memcpy(&word, now + 4u * k.word[w], 4);
      std::memcpy(&old, was + 4u * k.word[w], 4);
      if (word == old || !word)
        continue;
      if (const auto *p = GuestPtr<const u8>(__builtin_bswap32(word))) {
        __builtin_prefetch(p);
        __builtin_prefetch(p + k.bytes[w] - 1);
      }
    }
  }
}
static_assert(d3d::kDeviceOffsetIndexBuffer >= kTailBegin && d3d::kDeviceOffsetStreamSources >= kTailBegin &&
              d3d::kDeviceOffsetTextures >= kTailBegin);

// banjo-nx (2026-10-27, banjo_renut_shadow_sort): PrefetchNextHeaders for a sorted entry: the guest headers named in
// the tail chunks it brings (their new bytes are the arena copies the pairs point at).
void PrefetchSortedHeaders(const u8 *arena, const SortPair *pairs, u32 count) {
  for (u32 p = 0; p < count; ++p) {
    const u32 slot = pairs[p].slot;
    if (slot < kFetchChunks || slot >= 64)
      continue;
    const u32 c = slot - kFetchChunks;
    if (!((kHeaderChunks.mask >> c) & 1))
      continue;
    const u8 *now = arena + pairs[p].offset;
    const HeaderChunk &k = kHeaderChunks.chunk[c];
    for (u32 w = 0; w < k.count; ++w) {
      u32 word;
      std::memcpy(&word, now + 4u * k.word[w], 4);
      if (!word)
        continue;
      if (const auto *ptr = GuestPtr<const u8>(__builtin_bswap32(word))) {
        __builtin_prefetch(ptr);
        __builtin_prefetch(ptr + k.bytes[w] - 1);
      }
    }
  }
}

// The device each Replay depth rebuilds its ops' states in (ApplyState); only
// the render thread replays. The bytes before the fetch constants stay zero.
alignas(64) u8 g_replay_state[kMaxRunDepth][d3d::kDeviceSnapshotSize];

// banjo-nx (2026-10-26, banjo_renut_diag_fast_census): whether a replayed recording is the one its command buffer had
// the frame before: its ops and their device chunks (the structure), and its float constants. Diagnostics only.
struct RepeatCensus {
  struct Last {
    uint64_t structure = 0, constants = 0;
    // banjo-nx (2026-10-26, setter rewrite): each op's own hash (its kind, call and device chunks, no constants) and its
    // chunks' hashes by field, the frame before.
    std::vector<uint64_t> ops;
    std::vector<std::array<uint64_t, 18>> fields;
  };
  std::unordered_map<u32, Last> last;
  uint64_t recordings = 0, draws = 0, same_structure = 0, same_structure_draws = 0, same_all = 0, same_all_draws = 0;
  // banjo-nx (2026-10-26, setter rewrite): draws whose op is the one at the same place in the recording the frame before,
  // those whose every op up to theirs is, and for the others the fields whose bytes differ.
  uint64_t op_same_draws = 0, prefix_same_draws = 0, compared_draws = 0;
  uint64_t differ_fields[18] = {};
} g_repeat_census;

void CensusRecording(const HostCommandBuffer &buffer, u32 command_buffer_va) {
  uint64_t hs = 0x9E3779B97F4A7C15ull, hc = 0x632BE59BD9B4E019ull;
  const auto mix = [](uint64_t &h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xFF51AFD7ED558CCDull;
  };
  uint64_t draws = 0;
  RepeatCensus &census = g_repeat_census;
  RepeatCensus::Last &previous = census.last[command_buffer_va];
  std::vector<uint64_t> op_hashes;
  std::vector<std::array<uint64_t, 18>> op_fields;
  op_hashes.reserve(buffer.ops.size());
  op_fields.reserve(buffer.ops.size());
  bool prefix = true;
  for (const Op &op : buffer.ops) {
    uint64_t ho = 0x2545F4914F6CDD1Dull;
    std::array<uint64_t, 18> fh{};
    mix(ho, uint64_t(op.kind) | uint64_t(op.plain_dirty) << 8);
    if (op.kind == OpKind::Draw) {
      const DrawCall &c = op.call;
      mix(ho, uint64_t(c.primitive) | uint64_t(c.indexed) << 8 | uint64_t(u32(c.base_vertex)) << 16);
      mix(ho, uint64_t(c.start_index) | uint64_t(c.start_vertex) << 32);
      mix(ho, c.count);
    } else if (op.kind == OpKind::Run) {
      mix(ho, op.command_buffer);
    }
    if (op.state != kNone) {
      const u8 *at = buffer.arena.data() + op.state;
      for (uint64_t chunks = op.plain_dirty; chunks; chunks &= chunks - 1) {
        const u32 i = u32(std::countr_zero(chunks));
        const u32 size = ChunkSize(i);
        if (size_t(at - buffer.arena.data()) + size > buffer.arena.size())
          break;
        const uint64_t h = banjo::renut_gpu::ContentHash64(at, size);
        mix(ho, h);
        for (u32 f = kPlainFields.chunk[i]; f; f &= f - 1)
          mix(fh[std::countr_zero(f)], h ^ i);
        at += size;
      }
    }
    const size_t k = op_hashes.size();
    const bool same = k < previous.ops.size() && previous.ops[k] == ho;
    prefix = prefix && same;
    if (op.kind == OpKind::Draw && !previous.ops.empty()) {
      ++census.compared_draws;
      census.op_same_draws += same;
      census.prefix_same_draws += prefix;
      if (!same && k < previous.fields.size()) {
        for (u32 f = 0; f < 18; ++f)
          census.differ_fields[f] += fh[f] != previous.fields[k][f];
      }
    }
    op_hashes.push_back(ho);
    op_fields.push_back(fh);
    mix(hs, uint64_t(op.kind) | uint64_t(op.fields) << 8);
    mix(hs, op.plain_dirty);
    mix(hs, op.vs_set ^ (op.ps_set << 1));
    mix(hs, op.vs_dirty ^ (op.ps_dirty << 1));
    if (op.kind == OpKind::Draw) {
      ++draws;
      const DrawCall &c = op.call;
      mix(hs, uint64_t(c.primitive) | uint64_t(c.indexed) << 8 | uint64_t(c.tessellation) << 9 |
                  uint64_t(u32(c.base_vertex)) << 16);
      mix(hs, uint64_t(c.start_index) | uint64_t(c.start_vertex) << 32);
      mix(hs, c.count);
    } else if (op.kind == OpKind::Run) {
      mix(hs, op.command_buffer);
    } else {
      mix(hs, uint64_t(op.flags) | uint64_t(op.dest_texture) << 32);
    }
    if (op.state == kNone)
      continue;
    size_t plain = 0;
    for (uint64_t chunks = op.plain_dirty; chunks; chunks &= chunks - 1)
      plain += ChunkSize(u32(std::countr_zero(chunks)));
    const size_t constants = size_t(std::popcount(op.vs_dirty) + std::popcount(op.ps_dirty)) * kConstantGroupBytes;
    const u8 *at = buffer.arena.data() + op.state;
    if (op.state + plain + constants > buffer.arena.size())
      continue;
    mix(hs, banjo::renut_gpu::ContentHash64(at, plain));
    if (constants)
      mix(hc, banjo::renut_gpu::ContentHash64(at + plain, constants));
  }
  RepeatCensus &c = g_repeat_census;
  ++c.recordings;
  c.draws += draws;
  RepeatCensus::Last &l = previous;
  l.ops = std::move(op_hashes);
  l.fields = std::move(op_fields);
  if (l.structure == hs) {
    ++c.same_structure;
    c.same_structure_draws += draws;
    if (l.constants == hc) {
      ++c.same_all;
      c.same_all_draws += draws;
    }
  }
  l.structure = hs;
  l.constants = hc;
}

// On the render thread. 'predication': the mask the buffer runs under
// (SetPredication before RunCommandBuffer), which the draws' own masks narrow
// (see SetReplayPredicationLocked).
void Replay(const u8 *running, const BufferSet &buffers, u32 command_buffer_va, u32 depth,
            u32 predication) {
  if (depth >= kMaxRunDepth)
    return;
  auto it = buffers.find(command_buffer_va);
  if (it == buffers.end() || !it->second)
    return;
  // Only this thread touches a captured buffer: the game records into a new
  // one (BeginRecording).
  HostCommandBuffer &buffer = *it->second;
  static const bool repeat_census = REXCVAR_GET(banjo_renut_diag_fast_census);
  if (repeat_census)
    CensusRecording(buffer, command_buffer_va);
  static uint64_t replays = 0;
  const uint64_t replay = ++replays;
  uint64_t vs_dirty = ~uint64_t(0), ps_dirty = ~uint64_t(0);
  u8 *state = g_replay_state[depth];
  // banjo-nx (2026-10-19, CPU proposal 18): the draws' predication set once for the buffer, not around every draw:
  // only draws read it (ComputeViewport), and a nested run puts this one back when it ends.
  const u32 previous_predication = SetReplayPredicationLocked(predication);
  // banjo-nx (2026-10-20, banjo_renut_chain_reuse_replays): this replay's last draw's hint number and the chunks its ops
  // applied since then (draw.h ChainHint). A nested run's draws take numbers too, so the draw after it marks every chunk.
  static const bool hints = REXCVAR_GET(banjo_renut_chain_reuse_replays);
  uint64_t hint_last = 0, hint_plain = 0;
  u32 hint_fields = 0;  // banjo-nx (2026-10-26)
  const Op *const ops = buffer.ops.data();
  const size_t op_count = buffer.ops.size();
  uint64_t replayed_draws = 0;
  // banjo-nx (2026-10-27, banjo_renut_record_extents): the depth's slot extents, unknown until the ops give them (the
  // first op of a buffer gives all).
  DrawSlotExtents &extents = g_replay_extents[depth];
  extents.mask = 0;
  // banjo-nx (2026-10-27, Phase A): the depth's resources, unknown until the ops give them (the first gives all), and
  // not while A/B 45 has them off.
  DrawResources &resources = g_replay_resources[depth];
  resources.mask = 0;
  resources.ib_valid = false;
  resources.vb_mask = 0;
  const bool use_resources = ResourceObjectsOn() && banjo::renut_gpu::g_ab_off.load(std::memory_order_relaxed) != 45;
  static const bool kinds = DrawKindsCensusOn();  // banjo-nx (2026-10-27): the draw-kind census's times
  const Op *merge_prev = nullptr;  // banjo-nx (2026-10-27): the draw op before, with nothing between (the merge census)
  // banjo-nx (2026-10-27, banjo_renut_shadow_sort): the buffer's sorted runs (PlanSortedRuns), each taken at its first op
  // when its shaders allow any order and A/B target 40 is not in its off half; the ops as recorded otherwise.
  const bool sort_runs = ShadowSortOn() && buffer.sort_state.load(std::memory_order_acquire) == 2 &&
                         !buffer.sort_runs.empty() &&
                         banjo::renut_gpu::g_ab_off.load(std::memory_order_relaxed) != 40;
  size_t sort_cursor = 0;
  for (size_t op_index = 0; op_index < op_count; ++op_index) {
    if (sort_runs && sort_cursor < buffer.sort_runs.size()) {
      while (sort_cursor < buffer.sort_runs.size() && buffer.sort_runs[sort_cursor].begin < op_index)
        ++sort_cursor;  // (a merged draw took a run's first op)
      if (sort_cursor < buffer.sort_runs.size() && buffer.sort_runs[sort_cursor].begin == op_index) {
        const SortRun &run = buffer.sort_runs[sort_cursor++];
        if (SortedRunShadersOk(buffer, run)) {
          const u8 *arena = buffer.arena.data();
          const SortEntry *entries = buffer.sort_entries.data() + run.first_entry;
          // banjo_renut_shadow_sort_verify: each op's device as recorded (the run applied in order from this one).
          static const bool sort_verify = REXCVAR_GET(banjo_renut_shadow_sort_verify);
          static std::vector<u8> verify_states;
          constexpr u32 kVerifyFrom = kFetchBegin, kVerifyBytes = d3d::kDeviceSnapshotSize - kFetchBegin;
          if (sort_verify) {
            static std::vector<u8> scratch;
            scratch.assign(state, state + d3d::kDeviceSnapshotSize);
            verify_states.resize(size_t(run.end - run.begin) * kVerifyBytes);
            for (u32 j = run.begin; j < run.end; ++j) {
              ApplyState(scratch.data(), arena + ops[j].state, ops[j].plain_dirty, ops[j].vs_dirty, ops[j].ps_dirty);
              std::memcpy(verify_states.data() + size_t(j - run.begin) * kVerifyBytes, scratch.data() + kVerifyFrom,
                          kVerifyBytes);
            }
          }
          for (u32 k = 0; k < run.entry_count; ++k) {
            const SortEntry &e = entries[k];
            const uint64_t entry_start = kinds ? RenderTicks() : 0;
            const SortPair *pairs = buffer.sort_pairs.data() + e.first_pair;
            for (u32 p = 0; p < e.pair_count; ++p)
              ApplySortPair(state, arena, pairs[p]);
            hint_plain |= e.plain;
            hint_fields |= e.fields;
            vs_dirty |= e.vs_dirty;
            ps_dirty |= e.ps_dirty;
            if (sort_verify) {
              const u32 as = e.op == kSortRestore ? run.end - 1 : e.op;
              const u8 *want = verify_states.data() + size_t(as - run.begin) * kVerifyBytes;
              ++g_sort_verified;
              if (std::memcmp(state + kVerifyFrom, want, kVerifyBytes) != 0) {
                ++g_sort_verify_differ;
                u32 at = 0;
                while (at < kVerifyBytes && state[kVerifyFrom + at] == want[at])
                  ++at;
                static uint32_t logged = 0;
                if (logged++ < 16)
                  RGPU_WARN("banjo_renut_shadow_sort_verify: the device of op {} of a sorted run [{}, {}) differs from "
                            "the recorded one at +0x{:X}{}", as, run.begin, run.end, kVerifyFrom + at,
                            e.op == kSortRestore ? " (the run's end)" : "");
              }
            }
            if (e.op == kSortRestore)
              continue;
            const Op &sop = ops[e.op];
            // The next draw's shader lookups' lines, when its shaders are others, and the guest headers its chunks name
            // (the replay's look-ahead, A/B 29).
            if (k + 2 < run.entry_count && ReplayLookAheadOn()) {
              const SortEntry &ne = entries[k + 1];
              const Op &nop = ops[ne.op];
              if (nop.sort_vs != sop.sort_vs)
                PrefetchShaderLocked(nop.sort_vs, RecordKind::VertexShader);
              if (nop.sort_ps != sop.sort_ps)
                PrefetchShaderLocked(nop.sort_ps, RecordKind::PixelShader);
              PrefetchSortedHeaders(arena, buffer.sort_pairs.data() + ne.first_pair, ne.pair_count);
            }
            const ReplayConstants constants{running, sop.vs_set, sop.ps_set, &vs_dirty, &ps_dirty, replay};
            ++replayed_draws;
            if (hints) {
              const uint64_t seq = NextChainSeq();
              g_chain_hint.seq = seq;
              if (hint_last && seq == hint_last + 1) {
                PlainToChain(hint_plain, g_chain_hint.changed);
                g_chain_hint.fields = hint_fields;
              } else {
                g_chain_hint.changed[0] = g_chain_hint.changed[1] = ~uint64_t(0);
                g_chain_hint.fields = kFieldsAll;
              }
              hint_last = seq;
              hint_plain = 0;
              hint_fields = 0;
            }
            DrawStateLocked(state, sop.call, &constants);
            g_chain_hint = {};
            if (kinds)
              NoteDrawKind(RenderTicks() - entry_start);
          }
          ++g_sort_replayed_runs;
          g_sort_replayed_draws += run.entry_count - 1;
          extents.mask = 0;  // banjo-nx (2026-10-27): the sorted run's chunks came without their ops' extents
          resources.mask = 0;  // (and their resources)
          resources.ib_valid = false;
          resources.vb_mask = 0;
          merge_prev = nullptr;
          op_index = run.end - 1;  // the loop's step goes on at run.end, from the run's last op as recorded
          continue;
        }
        ++g_sort_refused_runs;
      }
    }
    const Op &op = ops[op_index];
    const uint64_t op_start = kinds ? RenderTicks() : 0;
    ApplyState(state, buffer.arena.data() + op.state, op.plain_dirty, op.vs_dirty, op.ps_dirty);
    if (op.extents != kNoExtents)
      ApplyExtents(extents, buffer.arena.data() + op.extents);
    else if (op.plain_dirty & ((uint64_t(1) << kFetchChunks) - 1))
      extents.mask &= ~SlotsOfFetchChunks(op.plain_dirty);  // (recorded with the key off: not known)
    if (op.resources != kNoResources) {
      ApplyResources(resources, buffer.arena.data() + op.resources, op.kind == OpKind::Draw);
    } else {  // nothing recorded: a draw has no index words of its own; slots it changed (key off) are not known
      if (op.kind == OpKind::Draw) {
        resources.ib_valid = false;
        resources.vb_mask = 0;
      }
      for (uint64_t bits = op.plain_dirty & kAllChunks; bits; bits &= bits - 1)
        resources.mask &= ~kTextureSlotsOfChunk[std::countr_zero(bits)];
    }
    hint_plain |= op.plain_dirty;
    hint_fields |= op.fields;
    vs_dirty |= op.vs_dirty;
    ps_dirty |= op.ps_dirty;
    switch (op.kind) {
    case OpKind::Draw: {
      // banjo-nx (2026-10-23, batch 5): the op's own call unless it has Begin/Draw*UP data to point at (most draws).
      DrawCall up_call;
      const DrawCall *call_ptr = &op.call;
      if (op.up_vertices != kNone || op.up_indices != kNone) {
        up_call = op.call;
        if (op.up_vertices != kNone)
          up_call.up_vertices = buffer.arena.data() + op.up_vertices;
        if (op.up_indices != kNone)
          up_call.up_indices = buffer.arena.data() + op.up_indices;
        call_ptr = &up_call;
      }
      // banjo-nx (2026-10-27, render thread night, banjo_renut_merge_draws / A/B 33): the draws right after this one that
      // change no state and write no constant (their ApplyState does nothing) and draw the next indices of the same list
      // with the same base vertex (or the next vertices) become part of this draw: one host draw, the same primitives
      // in the same order. Their ops are skipped.
      static const bool merge_on = MergeDrawsOn();
      size_t merged = 0;
      DrawCall merged_call;
      if (merge_on && call_ptr == &op.call && !op.call.tessellation &&
          (op.call.primitive == 1 || op.call.primitive == 2 || op.call.primitive == 4) &&
          banjo::renut_gpu::g_ab_off.load(std::memory_order_relaxed) != 33) {
        const DrawCall &c0 = op.call;
        u32 end = c0.indexed ? c0.start_index + c0.count : c0.start_vertex + c0.count;
        size_t j = op_index + 1;
        for (; j < op_count; ++j) {
          const Op &n = ops[j];
          if (n.kind != OpKind::Draw || n.plain_dirty || n.vs_dirty || n.ps_dirty || n.up_vertices != kNone ||
              n.up_indices != kNone)
            break;
          const DrawCall &c = n.call;
          if (c.primitive != c0.primitive || c.indexed != c0.indexed || c.tessellation || !c.count ||
              (c0.indexed ? (c.base_vertex != c0.base_vertex || c.start_index != end) : c.start_vertex != end))
            break;
          end += c.count;
        }
        if (j > op_index + 1 && MergeableShadersLocked(state)) {
          merged_call = c0;
          merged_call.count = end - (c0.indexed ? c0.start_index : c0.start_vertex);
          merged = j - op_index - 1;
          call_ptr = &merged_call;
          CountMergedDraws(1, merged);
        }
      }
      const DrawCall &call = *call_ptr;
      // banjo-nx (2026-10-27, the user's shadow-caster culling, banjo_renut_shadow_cull_indices; off by default, not
      // pixel-identical): a small shadow caster is not drawn. Its state stays applied and its changes go to the next
      // draw (the hints and the constants' dirty groups keep gathering until a draw takes them).
      if (ShadowCullIndices() && ShadowCasterCulledLocked(state, call)) {
        merge_prev = nullptr;
        op_index += merged;
        break;
      }
      if (kinds) {
        const bool same_state = merge_prev && op.fields == 0 && op.plain_dirty == 0 && op.vs_dirty == 0 &&
                                op.ps_dirty == 0 && op.up_vertices == kNone && op.up_indices == kNone &&
                                merge_prev->call.primitive == call.primitive && merge_prev->call.indexed == call.indexed &&
                                !call.tessellation &&
                                (call.primitive == 1 || call.primitive == 2 || call.primitive == 4);  // point, line, triangle lists
        const bool contiguous = same_state && (call.indexed ? merge_prev->call.base_vertex == call.base_vertex &&
                                                                  merge_prev->call.start_index + merge_prev->call.count ==
                                                                      call.start_index
                                                            : merge_prev->call.start_vertex + merge_prev->call.count ==
                                                                  call.start_vertex);
        NoteDrawMerge(same_state, contiguous);
        merge_prev = &op;
      }
      // The draw reads its inherited constants from the running device.
      const ReplayConstants constants{running, op.vs_set, op.ps_set, &vs_dirty, &ps_dirty, replay};
      ++replayed_draws;  // banjo-nx (2026-10-23): counted once for the buffer (CountDrawSource), not at every draw
      if (hints) {
        const uint64_t seq = NextChainSeq();
        g_chain_hint.seq = seq;
        if (hint_last && seq == hint_last + 1) {
          PlainToChain(hint_plain, g_chain_hint.changed);
          g_chain_hint.fields = hint_fields;
        } else {
          g_chain_hint.changed[0] = g_chain_hint.changed[1] = ~uint64_t(0);
          g_chain_hint.fields = kFieldsAll;
        }
        hint_last = seq;
        hint_plain = 0;
        hint_fields = 0;
      }
      // banjo-nx (2026-10-23, render-thread round): the next draw's shader lookups' lines, fetched while this draw runs
      // (the first thing a draw reads; its own prefetches come after them), and the start of its state in the arena.
      // banjo-nx (2026-10-27): OFF by default since the console run of 2026-10-27 (+2.15 ms a frame with it, A/B 29):
      // banjo_renut_replay_lookahead. (The op after the ones this draw took, when it took some.)
      const size_t next_index = op_index + 1 + merged;
      if (next_index < op_count && ops[next_index].kind == OpKind::Draw && ops[next_index].state != kNone &&
          ReplayLookAheadOn()) {
        const Op &next = ops[next_index];
        const u8 *next_state = buffer.arena.data() + next.state;
        __builtin_prefetch(next_state);
        // banjo-nx (2026-10-26, round 4): only when the next op changed a shader word (else this draw's, fetched).
        if (next.fields & kFieldShaders) {
          PrefetchShaderLocked(PeekNextWord(state, next_state, next.plain_dirty, d3d::kDeviceOffsetVertexShader),
                               RecordKind::VertexShader);
          PrefetchShaderLocked(PeekNextWord(state, next_state, next.plain_dirty, d3d::kDeviceOffsetPixelShader),
                               RecordKind::PixelShader);
        }
        // banjo-nx (2026-10-26, Unleashed-style step 1): only when the next op changed a header word (its fields).
        if (next.fields & (kFieldTexWords | kFieldStreams | kFieldIndex))
          PrefetchNextHeaders(state, next_state, next.plain_dirty);
        if (next.fields & (kFieldFetch | kFieldTexWords))  // banjo-nx (2026-10-26, round 3)
          PrefetchNextTextures(state, next_state, next.plain_dirty);
      }
      g_draw_slot_extents = extents.mask ? &extents : nullptr;  // banjo-nx (2026-10-27)
      g_draw_resources = use_resources ? &resources : nullptr;  // (Phase A)
      DrawStateLocked(state, call, &constants);
      g_draw_slot_extents = nullptr;
      g_draw_resources = nullptr;
      g_chain_hint = {};  // only for the draw itself
      if (kinds)
        NoteDrawKind(RenderTicks() - op_start);
      op_index += merged;  // banjo-nx (2026-10-27): the ops this draw took (no state, no constants: nothing to apply)
      break;
    }
    case OpKind::Clear:
      merge_prev = nullptr;
      ClearStateLocked(state, op.flags, op.has_rect ? &op.rect : nullptr, op.color, op.depth,
                       op.stencil);
      break;
    case OpKind::Resolve:
      merge_prev = nullptr;
      ResolveStateLocked(state, op.flags, op.has_rect ? &op.rect : nullptr, op.dest_texture,
                         op.has_dest_format ? &op.dest_format : nullptr, op.dest_x, op.dest_y,
                         op.dest_level, op.dest_slice, op.has_color ? op.color : nullptr,
                         op.depth, op.stencil);
      break;
    case OpKind::Run:
      // The nested buffer runs on this op's state: complete its constants.
      // Idempotent: the inherited groups are rewritten on every run.
      merge_prev = nullptr;
      MergeInherited(state, running, op.vs_set, kVertexShaderF);
      MergeInherited(state, running, op.ps_set, kPixelShaderF);
      Replay(state, buffers, op.command_buffer, depth + 1, predication & Predication(state));
      break;
    }
  }
  SetReplayPredicationLocked(previous_predication);
  if (replayed_draws)
    banjo::renut_gpu::CountDrawSource(1, replayed_draws);
}

// The buffer and those it runs, nested up to kMaxRunDepth, with their pending
// Begin*Vertices data copied in. Requires g_record_mutex.
void CaptureBuffers(u32 command_buffer_va, u32 depth, BufferSet &buffers) {
  if (depth >= kMaxRunDepth || buffers.contains(command_buffer_va))
    return;
  FlushPending([command_buffer_va](const PendingVertices &p) {
    return p.command_buffer == command_buffer_va;
  });
  auto it = g_buffers.find(command_buffer_va);
  if (it == g_buffers.end() || !it->second)
    return;
  buffers.emplace(command_buffer_va, it->second);
  for (const Op &op : it->second->ops) {
    if (op.kind == OpKind::Run)
      CaptureBuffers(op.command_buffer, depth + 1, buffers);
  }
}

} // namespace

bool IsRecordingCommandBuffer(u32 device_va) {
  return RecordingBuffer(device_va) != 0;
}

void BeginRecording(u32 device_va, u32 command_buffer_va) {
  std::lock_guard lock(g_record_mutex);
  FlushPending([command_buffer_va](const PendingVertices &p) {
    return p.command_buffer == command_buffer_va;
  });
  // A queued run may still hold the old recording.
  auto &slot = g_buffers[command_buffer_va];
  g_record_generation.fetch_add(1, std::memory_order_acq_rel);  // banjo-nx: NewOp's per-thread lookups
  if (!slot || slot.use_count() > 1) {
    slot = NewBuffer();
  } else {
    slot->ops.clear();
    slot->arena.clear();
    slot->ClearSortPlan();  // banjo-nx (2026-10-27)
    slot->id = g_recording_ids.fetch_add(1, std::memory_order_relaxed) + 1;  // banjo-nx (2026-10-26): a new recording
  }
  // BeginCommandBuffer marks the state the buffer does not inherit dirty.
  // (The shadow stays: a buffer's first op keeps every chunk anyway.)
  DeviceRecording &set = g_recording_devices[device_va];
  set.vs = 0;
  set.ps = 0;
  if (const auto *device = GuestPtr<const d3d::Device>(device_va)) {
    set.vs = u64(device->m_Pending[0]);
    set.ps = u64(device->m_Pending[1]);
  }
}

// banjo-nx (2026-10-27, banjo_renut_shadow_sort): whether a recorded draw writes depth only, so that the draws of a run
// of them leave the same depth target in any order, from the device as the op records it: a depth target; stencil
// off; no colour target whose channels are written; depth written with a less / less-equal (each sample keeps the
// smallest) or greater / greater-equal test (the largest), or not written at all; no Draw*UP data, tessellation or
// point sprites. Its pixel shader may discard (a discarded fragment writes nothing, the others still keep the
// nearest); exports, skinning once and the fur family are the replay's test (SortableShadersLocked).
namespace {
// banjo-nx (2026-10-27, banjo_renut_shadow_sort): each shader object's content (a 31-bit digest of its hash) and whether
// its draws may be reordered (ShaderSortableByHash), published where the object is made (hooks/shader.cpp, on the
// game's threads) and read at record time: N&B makes a shader object per mesh, so the addresses differ where the
// shader is the same. Open addressing by address, entries address << 32 | digest << 1 | sortable; a miss or a full
// probe leaves a draw unsorted (the key is only the order: any key is exact).
constexpr u32 kShaderKeyBits = 14, kShaderKeyProbes = 32;
std::atomic<uint64_t> g_shader_keys[1u << kShaderKeyBits];
inline u32 ShaderKeyIndex(u32 va) {
  return (va * 0x9E3779B1u) >> (32 - kShaderKeyBits);
}
// The shader's word (digest << 1 | sortable), 0 when unknown.
u32 ShaderSortWord(u32 va) {
  if (!va)
    return 0;
  for (u32 p = 0, i = ShaderKeyIndex(va); p < kShaderKeyProbes; ++p, i = (i + 1) & ((1u << kShaderKeyBits) - 1)) {
    const uint64_t e = g_shader_keys[i].load(std::memory_order_acquire);
    if (!e)
      return 0;
    if (u32(e >> 32) == va)
      return u32(e);
  }
  return 0;
}
}  // namespace

void NoteShaderSortKey(u32 va, uint64_t hash, bool sortable) {
  if (!va)
    return;
  const u32 digest = (u32(hash) ^ u32(hash >> 32)) & 0x7FFFFFFFu;
  const uint64_t word = uint64_t(va) << 32 | uint64_t(digest) << 1 | uint64_t(sortable ? 1 : 0);
  for (u32 p = 0, i = ShaderKeyIndex(va); p < kShaderKeyProbes; ++p, i = (i + 1) & ((1u << kShaderKeyBits) - 1)) {
    uint64_t e = g_shader_keys[i].load(std::memory_order_acquire);
    if (!e && g_shader_keys[i].compare_exchange_strong(e, word, std::memory_order_acq_rel))
      return;
    if (u32(e >> 32) == va) {
      g_shader_keys[i].store(word, std::memory_order_release);
      return;
    }
  }
}

namespace {
enum SortWhy : u8 { kSortOk, kSortOff, kSortCall, kSortNoDepth, kSortStencil, kSortColor, kSortDepthFunc, kSortShader,
                    kSortWhyCount };
u8 SortFacts(const u8 *bytes, const DrawCall &call, Op &op) {
  if (!ShadowSortOn())
    return kSortOff;
  if (call.up_vertices || call.up_indices || call.tessellation || call.primitive == 1 || !call.count)
    return kSortCall;  // (primitive 1: point list)
  const auto word = [bytes](u32 offset) {
    u32 v;
    std::memcpy(&v, bytes + offset, 4);
    return __builtin_bswap32(v);
  };
  const u32 ds = word(d3d::kDeviceOffsetDepthStencil);
  if (!ds)
    return kSortNoDepth;
  const u32 dc = word(u32(offsetof(d3d::Device, m_ControlPacket) + offsetof(d3d::ControlPacket, DepthControl)));
  if (dc & 1)
    return kSortStencil;
  const u32 mask = word(u32(offsetof(d3d::Device, m_ValuesPacket) + offsetof(d3d::ValuesPacket, ColorMask)));
  u32 rt[d3d::kMaxRenderTargets];
  for (u32 i = 0; i < d3d::kMaxRenderTargets; ++i) {
    rt[i] = word(d3d::kDeviceOffsetRenderTargets + i * 4);
    if (rt[i] && ((mask >> (4 * i)) & 0xF))
      return kSortColor;
  }
  u32 family = 3;  // depth not written
  if ((dc & 2) && (dc & 4)) {
    const u32 func = (dc >> 4) & 7;  // CompareFunction: 1 less, 3 less-equal, 4 greater, 6 greater-equal
    family = func == 1 || func == 3 ? 1u : func == 4 || func == 6 ? 2u : 0u;
    if (!family)
      return kSortDepthFunc;
  }
  const u32 vs = word(d3d::kDeviceOffsetVertexShader), ps = word(d3d::kDeviceOffsetPixelShader);
  const u32 vs_word = ShaderSortWord(vs), ps_word = ps ? ShaderSortWord(ps) : 1u;
  if (!(vs_word & 1) || !(ps_word & 1))
    return kSortShader;  // unknown, or one whose draws keep their order
  op.sort_key = uint64_t(vs_word >> 1) << 32 | (ps ? ps_word >> 1 : 0u);
  op.sort_vs = vs;
  op.sort_ps = ps;
  op.sort_decl = word(d3d::kDeviceOffsetVertexDeclaration);
  // The pipeline's render state (render/draw.cpp FastRenderState's words: the blend, colour, depth, stencil, mode and
  // colour controls, the shaders' bools) and the primitive: draws of one shader pair and render state in a row share a
  // pipeline. Only the order: any digest is exact.
  {
    constexpr u32 kControl = u32(offsetof(d3d::Device, m_ControlPacket));
    constexpr u32 kValues = u32(offsetof(d3d::Device, m_ValuesPacket));
    constexpr u32 kWords[] = {kControl + u32(offsetof(d3d::ControlPacket, BlendControl0)),
                              kControl + u32(offsetof(d3d::ControlPacket, BlendControl1)),
                              kControl + u32(offsetof(d3d::ControlPacket, BlendControl2)),
                              kControl + u32(offsetof(d3d::ControlPacket, BlendControl3)),
                              kControl + u32(offsetof(d3d::ControlPacket, ColorControl)),
                              kControl + u32(offsetof(d3d::ControlPacket, ModeControl)),
                              kValues + u32(offsetof(d3d::ValuesPacket, StencilRefMask)),
                              kValues + u32(offsetof(d3d::ValuesPacket, StencilRefMaskBF))};
    u32 h = dc * 0x9E3779B1u ^ mask ^ call.primitive;
    for (const u32 at : kWords)
      h = (h ^ word(at)) * 0x85EBCA6Bu;
    for (u32 i = 0; i < 8; ++i)  // the bools (VertexShaderB, then the pixel shader's)
      h = (h ^ word(kTailBegin + i * 4)) * 0xC2B2AE35u;
    op.sort_rs = h;
    u32 t = 0;
    for (u32 r = 0; r < 4; ++r)
      t = (t ^ word(d3d::kDeviceOffsetTextures + r * 4)) * 0x9E3779B1u;
    op.sort_tex = t;
  }
  u32 h = ds * 0x9E3779B1u ^ family;
  for (u32 i = 0; i < d3d::kMaxRenderTargets; ++i)
    h = (h ^ rt[i]) * 0x85EBCA6Bu;
  op.sort_sig = h | 1u;
  return kSortOk;
}
}  // namespace

void RecordDraw(u32 device_va, const DrawCall &call, u32 up_vertex_bytes, u32 up_index_bytes) {
  HostCommandBuffer *buffer = nullptr;
  Op *op = NewOp(device_va, OpKind::Draw, &buffer);
  if (!op)
    return;
  // banjo-nx (2026-10-27, banjo_renut_shadow_sort): only with the sort on (PlanSortedRuns is its only reader; about
  // thirty device words and three hashes a recorded draw on the draw threads for nothing since the sort went off).
  if (ShadowSortOn()) {
    if (const auto *bytes = GuestPtr<const u8>(device_va))
      op->sort_why = SortFacts(bytes, call, *op);
  }
  op->call = call;
  op->call.up_vertices = nullptr;
  op->call.up_indices = nullptr;
  if (call.up_vertices)
    op->up_vertices = CopyIn(*buffer, call.up_vertices, up_vertex_bytes);
  if (call.up_indices)
    op->up_indices = CopyIn(*buffer, call.up_indices, up_index_bytes);
}

void RecordDeferredDraw(u32 device_va, const DrawCall &call, const u8 *scratch, u32 bytes) {
  HostCommandBuffer *buffer = nullptr;
  Op *op = NewOp(device_va, OpKind::Draw, &buffer);
  if (!op)
    return;
  op->call = call;
  op->call.up_vertices = nullptr;
  op->up_vertices = Reserve(*buffer, bytes);
  std::lock_guard lock(g_record_mutex);
  g_pending_vertices.push_back(
      {device_va, RecordingBuffer(device_va), op->up_vertices, scratch, bytes});
  SyncPendingCount();
}

void RecordClear(u32 device_va, u32 flags, const ClearRect *rect, const float color[4],
                 float depth, u32 stencil) {
  HostCommandBuffer *buffer = nullptr;
  Op *op = NewOp(device_va, OpKind::Clear, &buffer);
  if (!op)
    return;
  op->flags = flags;
  op->has_rect = rect != nullptr;
  if (rect)
    op->rect = *rect;
  std::memcpy(op->color, color, sizeof(op->color));
  op->depth = depth;
  op->stencil = stencil;
}

void RecordResolve(u32 device_va, u32 flags, const ClearRect *source_rect, u32 dest_texture_va,
                   i32 dest_x, i32 dest_y, u32 dest_level, u32 dest_slice,
                   const float clear_color[4], float clear_depth, u32 clear_stencil) {
  HostCommandBuffer *buffer = nullptr;
  Op *op = NewOp(device_va, OpKind::Resolve, &buffer);
  if (!op)
    return;
  op->flags = flags;
  op->has_rect = source_rect != nullptr;
  if (source_rect)
    op->rect = *source_rect;
  op->has_color = clear_color != nullptr;
  if (clear_color)
    std::memcpy(op->color, clear_color, sizeof(op->color));
  op->depth = clear_depth;
  op->stencil = clear_stencil;
  op->dest_texture = dest_texture_va;
  if (const auto *header = GuestPtr<const d3d::BaseTexture>(dest_texture_va)) {
    op->has_dest_format = true;
    op->dest_format = header->Format;
  }
  op->dest_x = dest_x;
  op->dest_y = dest_y;
  op->dest_level = dest_level;
  op->dest_slice = dest_slice;
}

// banjo-nx (2026-10-26, banjo_renut_diag_fast_census): the 10 s line of CensusRecording.
std::string RepeatCensusReport() {
  RepeatCensus &c = g_repeat_census;
  if (!c.recordings)
    return {};
  const double d = c.draws ? double(c.draws) : 1.0;
  std::string out = fmt::format("recordings against the frame before's (banjo_renut_diag_fast_census): {} replayed "
                                "({} draws); the same ops and device chunks {} ({:.1f} % of the draws), and the same "
                                "float constants too {} ({:.1f} % of the draws)",
                                c.recordings, c.draws, c.same_structure, 100.0 * double(c.same_structure_draws) / d,
                                c.same_all, 100.0 * double(c.same_all_draws) / d);
  // banjo-nx (2026-10-26, setter rewrite): by op, the frame before's at the same place.
  if (c.compared_draws) {
    const double cd = double(c.compared_draws);
    static constexpr const char *kNames[18] = {"fetch", "bools", "dest", "view packets", "misc", "29C0", "unread",
                                               "private", "decl", "tail", "index", "targets", "streams", "tex words",
                                               "scissor", "shaders", "predication", "vertex fetch"};
    out += fmt::format("; by op: {} draws compared, the same op as the frame before's at its place {:.1f} %, every op "
                       "up to it the same {:.1f} %; the others' fields that differ:",
                       c.compared_draws, 100.0 * double(c.op_same_draws) / cd, 100.0 * double(c.prefix_same_draws) / cd);
    const double other = std::max(1.0, cd - double(c.op_same_draws));
    for (u32 f = 0; f < 18; ++f)
      if (c.differ_fields[f])
        out += fmt::format(" {} {:.0f} %", kNames[f], 100.0 * double(c.differ_fields[f]) / other);
  }
  c.recordings = c.draws = c.same_structure = c.same_structure_draws = c.same_all = c.same_all_draws = 0;
  c.op_same_draws = c.prefix_same_draws = c.compared_draws = 0;
  std::fill(std::begin(c.differ_fields), std::end(c.differ_fields), uint64_t(0));
  return out;
}

void RecordRun(u32 device_va, u32 command_buffer_va) {
  HostCommandBuffer *buffer = nullptr;
  if (Op *op = NewOp(device_va, OpKind::Run, &buffer))
    op->command_buffer = command_buffer_va;
}

void FlushRecordedVertices(u32 device_va) {
  // banjo-nx (2026-10-20, from sa2-nx, banjo_renut_callbacks_fast): every hook calls this first; nothing pending (always so without
  // command buffers, SA2's case) needs no lock. g_pending_count is kept under g_record_mutex (SyncPendingCount).
  if (HookFastPathsOn() && g_pending_count.load(std::memory_order_acquire) == 0)
    return;
  std::lock_guard lock(g_record_mutex);
  FlushDevicePending(device_va);
}

namespace {
// banjo-nx (2026-10-27, banjo_renut_shadow_sort): the plan's census (the 10 s line), from the threads that queue runs.
constexpr u32 kMinSortRun = 3;
constexpr u32 kSortSlots = 192;  // 64 plain chunk slots (53 used), 64 + 64 constant groups
std::atomic<uint64_t> g_sort_plans{0}, g_sort_runs{0}, g_sort_run_draws{0}, g_sort_sorted_runs{0}, g_sort_sorted_draws{0};
std::atomic<uint64_t> g_sort_changes_before{0}, g_sort_changes_after{0}, g_sort_pairs{0}, g_sort_draws{0};
std::atomic<uint64_t> g_sort_why[kSortWhyCount], g_sort_lengths[6], g_sort_end[6];

// An op's chunk and constant group copies in the arena (CopyState's order: the chunks by index, then the vertex
// shader's groups and the pixel shader's, each by group).
void ApplyVersions(const Op &op, u32 *ver) {
  if (op.state == kNone)
    return;
  u32 at = u32(op.state);
  for (uint64_t c = op.plain_dirty; c; c &= c - 1) {
    const u32 i = u32(std::countr_zero(c));
    ver[i] = at;
    at += ChunkSize(i);
  }
  for (uint64_t m = op.vs_dirty; m;) {  // bit 63 - group: the highest bit is the lowest group, copied first
    const u32 g = u32(std::countl_zero(m));
    ver[64 + g] = at;
    at += kConstantGroupBytes;
    m &= ~(uint64_t(1) << (63 - g));
  }
  for (uint64_t m = op.ps_dirty; m;) {
    const u32 g = u32(std::countl_zero(m));
    ver[128 + g] = at;
    at += kConstantGroupBytes;
    m &= ~(uint64_t(1) << (63 - g));
  }
}

// The sorted runs of a captured buffer (once; the render thread reads them at sort_state 2). A run: from op 1 on,
// consecutive draws with one signature (SortFacts), at least kMinSortRun, not in order already. Each draw's state is
// the versions (arena copies) of the slots the run's ops write; the sorted order's entries carry the slots whose version
// differs from the device the draw before left, the groups whose source changes (op set / the running device), and a
// last entry back to the run's last op as recorded.
void PlanSortedRuns(HostCommandBuffer &b) {
  u32 expected = 0;
  if (!ShadowSortOn() || !b.sort_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
    return;
  const Op *const ops = b.ops.data();
  const u32 n = u32(b.ops.size());
  uint64_t why[kSortWhyCount] = {}, lengths[6] = {}, ends[6] = {}, draws = 0;
  struct Cand {
    u32 begin, end;
  };
  std::vector<Cand> cands;
  for (u32 i = 0; i < n;) {
    const Op &o = ops[i];
    if (o.kind == OpKind::Draw) {
      ++draws;
      ++why[o.sort_sig ? kSortOk : o.sort_why && o.sort_why < kSortWhyCount ? o.sort_why : kSortCall];  // (Begin*Vertices: no facts)
    }
    if (o.kind != OpKind::Draw || !o.sort_sig || o.state == kNone || i == 0) {
      ++i;
      continue;
    }
    u32 j = i + 1;
    while (j < n && ops[j].kind == OpKind::Draw && ops[j].sort_sig == o.sort_sig && ops[j].state != kNone) {
      ++draws;
      ++why[kSortOk];
      ++j;
    }
    const u32 len = j - i;
    ++lengths[len <= 2 ? 0 : len <= 4 ? 1 : len <= 8 ? 2 : len <= 16 ? 3 : len <= 64 ? 4 : 5];
    // What ended it: the buffer's end, a run op, a clear or resolve, a draw of other targets, an unsortable draw.
    ++ends[j >= n ? 0 : ops[j].kind == OpKind::Run ? 1 : ops[j].kind != OpKind::Draw ? 2 : ops[j].sort_sig ? 3 : 4];
    if (len >= kMinSortRun)
      cands.push_back({i, j});
    i = j;
  }
  g_sort_plans.fetch_add(1, std::memory_order_relaxed);
  g_sort_draws.fetch_add(draws, std::memory_order_relaxed);
  for (u32 k = 0; k < kSortWhyCount; ++k)
    if (why[k])
      g_sort_why[k].fetch_add(why[k], std::memory_order_relaxed);
  for (u32 k = 0; k < 6; ++k) {
    if (lengths[k])
      g_sort_lengths[k].fetch_add(lengths[k], std::memory_order_relaxed);
    if (ends[k])
      g_sort_end[k].fetch_add(ends[k], std::memory_order_relaxed);
  }
  uint64_t runs = 0, run_draws = 0, sorted_runs = 0, sorted_draws = 0, before = 0, after = 0;
  u32 ver[kSortSlots] = {};
  u32 applied = 0;  // ops [0, applied) are in 'ver'
  std::vector<u32> verw, order;
  std::vector<u8> slots;
  for (const Cand &c : cands) {
    const u32 len = c.end - c.begin;
    ++runs;
    run_draws += len;
    order.resize(len);
    for (u32 k = 0; k < len; ++k)
      order[k] = k;
    const auto less = [&](u32 x, u32 y) {
      const Op &a = ops[c.begin + x], &bo = ops[c.begin + y];
      if (a.sort_key != bo.sort_key)
        return a.sort_key < bo.sort_key;
      if (a.sort_rs != bo.sort_rs)
        return a.sort_rs < bo.sort_rs;
      return a.sort_tex != bo.sort_tex ? a.sort_tex < bo.sort_tex : a.sort_decl < bo.sort_decl;
    };
    std::stable_sort(order.begin(), order.end(), less);
    u32 changes_before = 0, changes_after = 0;
    bool identity = true;
    for (u32 k = 0; k < len; ++k) {
      identity = identity && order[k] == k;
      if (k) {
        changes_before += ops[c.begin + k].sort_key != ops[c.begin + k - 1].sort_key;
        changes_after += ops[c.begin + order[k]].sort_key != ops[c.begin + order[k - 1]].sort_key;
      }
    }
    before += changes_before;
    after += identity ? changes_before : changes_after;
    if (identity)
      continue;
    // The slots the run's ops write, and the versions of each op (as recorded) of those.
    uint64_t plain = 0, vsw = 0, psw = 0;
    for (u32 k = c.begin; k < c.end; ++k) {
      plain |= ops[k].plain_dirty;
      vsw |= ops[k].vs_dirty;
      psw |= ops[k].ps_dirty;
    }
    slots.clear();
    for (uint64_t m = plain; m; m &= m - 1)
      slots.push_back(u8(std::countr_zero(m)));
    for (u32 g = 0; g < 64; ++g)
      if ((vsw >> (63 - g)) & 1)
        slots.push_back(u8(64 + g));
    for (u32 g = 0; g < 64; ++g)
      if ((psw >> (63 - g)) & 1)
        slots.push_back(u8(128 + g));
    const u32 w = u32(slots.size());
    for (; applied < c.begin; ++applied)
      ApplyVersions(ops[applied], ver);
    std::vector<u32> cur(w);
    for (u32 k = 0; k < w; ++k)
      cur[k] = ver[slots[k]];
    verw.resize(size_t(len) * w);
    for (u32 j = 0; j < len; ++j) {
      ApplyVersions(ops[c.begin + j], ver);
      for (u32 k = 0; k < w; ++k)
        verw[size_t(j) * w + k] = ver[slots[k]];
    }
    applied = c.end;
    SortRun run;
    run.begin = c.begin;
    run.end = c.end;
    run.first_entry = u32(b.sort_entries.size());
    uint64_t prev_vs = ops[c.begin - 1].vs_set, prev_ps = ops[c.begin - 1].ps_set;
    const auto emit = [&](u32 op_index, const u32 *target, uint64_t vs_set, uint64_t ps_set) {
      SortEntry e;
      e.op = op_index;
      e.first_pair = u32(b.sort_pairs.size());
      for (u32 k = 0; k < w; ++k) {
        if (target[k] == cur[k])
          continue;
        const u32 slot = slots[k];
        // A chunk holding several fields (the targets share one with the index buffer and the streams): the fields of
        // the words that differ between the copy the device holds and the new one, as NewOp finds them; another chunk
        // as a whole. (Whole-chunk fields told the fast path its targets had changed at nearly every sorted draw.)
        if (slot < 64 && ((kPlainFields.mixed >> slot) & 1)) {
          const u8 *was = b.arena.data() + cur[k], *now = b.arena.data() + target[k];
          for (u32 words = ChunkWordMask(now, was, ChunkSize(slot)); words; words &= words - 1)
            e.fields |= kPlainFields.word[slot][std::countr_zero(words)];
        } else if (slot < 64) {
          e.fields |= kPlainFields.chunk[slot];
        }
        cur[k] = target[k];
        b.sort_pairs.push_back({slot, target[k]});
        if (slot < 64)
          e.plain |= uint64_t(1) << slot;
        else if (slot < 128)
          e.vs_dirty |= uint64_t(1) << (63 - (slot - 64));
        else
          e.ps_dirty |= uint64_t(1) << (63 - (slot - 128));
      }
      e.pair_count = u32(b.sort_pairs.size()) - e.first_pair;
      // A group read from the op's state by one draw and from the running device by the other changes too.
      e.vs_dirty |= prev_vs ^ vs_set;
      e.ps_dirty |= prev_ps ^ ps_set;
      prev_vs = vs_set;
      prev_ps = ps_set;
      b.sort_entries.push_back(e);
    };
    for (u32 k = 0; k < len; ++k) {
      const Op &o = ops[c.begin + order[k]];
      emit(c.begin + order[k], &verw[size_t(order[k]) * w], o.vs_set, o.ps_set);
    }
    emit(kSortRestore, &verw[size_t(len - 1) * w], ops[c.end - 1].vs_set, ops[c.end - 1].ps_set);
    run.entry_count = u32(b.sort_entries.size()) - run.first_entry;
    b.sort_runs.push_back(run);
    ++sorted_runs;
    sorted_draws += len;
  }
  g_sort_runs.fetch_add(runs, std::memory_order_relaxed);
  g_sort_run_draws.fetch_add(run_draws, std::memory_order_relaxed);
  g_sort_sorted_runs.fetch_add(sorted_runs, std::memory_order_relaxed);
  g_sort_sorted_draws.fetch_add(sorted_draws, std::memory_order_relaxed);
  g_sort_changes_before.fetch_add(before, std::memory_order_relaxed);
  g_sort_changes_after.fetch_add(after, std::memory_order_relaxed);
  g_sort_pairs.fetch_add(b.sort_pairs.size(), std::memory_order_relaxed);
  b.sort_state.store(2, std::memory_order_release);
}
}  // namespace

void QueueRunCommandBuffer(u32 device_va, u32 command_buffer_va) {
  FlushDeferredVertices(device_va);
  const u8 *device = GuestPtr<const u8>(device_va);
  if (!device)
    return;
  struct Run {
    std::vector<u8> running;  // the running device, whose constants the buffer inherits
    BufferSet buffers;
  };
  auto run = std::make_shared<Run>();
  run->running.assign(device, device + d3d::kDeviceSnapshotSize);
  {
    std::lock_guard lock(g_record_mutex);
    CaptureBuffers(command_buffer_va, 0, run->buffers);
  }
  // banjo-nx (2026-10-27, banjo_renut_shadow_sort): each captured recording's sorted runs, here and not on the render
  // thread (once a recording: a buffer run again keeps its plan).
  if (ShadowSortOn()) {
    for (const auto &[va, recording] : run->buffers)
      if (recording)
        PlanSortedRuns(*recording);
  }
  if (UnleashedCensusOn()) {  // banjo-nx (2026-10-26): each recording of the run, with its draws
    const uint64_t swap = rex::latency::SwapCount();
    for (const auto &[va, recording] : run->buffers) {
      if (!recording)
        continue;
      u32 draws = 0;
      for (const Op &op : recording->ops)
        draws += op.kind == OpKind::Draw;
      NoteCommandBufferRun(reinterpret_cast<const void *>(uintptr_t(recording->id)), draws, swap);
    }
  }
  render_thread::Queue([run, command_buffer_va] {
    const u8 *running = run->running.data();
    Replay(running, run->buffers, command_buffer_va, 0, Predication(running));
  });
}

void ReleaseCommandBuffer(u32 command_buffer_va) {
  std::lock_guard lock(g_record_mutex);
  if (g_buffers.erase(command_buffer_va)) {
    g_record_generation.fetch_add(1, std::memory_order_acq_rel);  // banjo-nx: NewOp's per-thread lookups
    for (size_t i = 0; i < g_pending_vertices.size();) {
      if (g_pending_vertices[i].command_buffer == command_buffer_va) {
        g_pending_vertices[i] = g_pending_vertices.back();
        g_pending_vertices.pop_back();
      } else {
        ++i;
      }
    }
    SyncPendingCount();
  }
}

// banjo-nx (2026-10-26, banjo_renut_diag_fast_census): the setters' and the inline writes' shares of the changed state.
std::string StateSourceReport() {
  const uint64_t setter = g_census_setter_changed.exchange(0, std::memory_order_relaxed);
  const uint64_t inl = g_census_inline_changed.exchange(0, std::memory_order_relaxed);
  const uint64_t touch_marks = g_census_touch_marks.exchange(0, std::memory_order_relaxed);
  const uint64_t tag_only = g_census_tag_only_marks.exchange(0, std::memory_order_relaxed);
  const uint64_t census_ops = g_census_ops.exchange(0, std::memory_order_relaxed);
  if (!setter && !inl)
    return {};
  static constexpr const char *kNames[18] = {"fetch", "bools", "dest", "view packets", "misc", "29C0", "unread",
                                             "private", "decl", "tail", "index", "targets", "streams", "tex words",
                                             "scissor", "shaders", "predication", "vertex fetch"};
  std::string out = fmt::format("state by source (banjo_renut_diag_fast_census): {} changed chunks marked by the hooked "
                                "setters, {} only by an inline write's pending tag ({:.1f} % inline); setters by field:",
                                setter, inl, 100.0 * double(inl) / double(setter + inl));
  if (census_ops)
    out += fmt::format(" marks an op: {:.1f} by the setters' touches, {:.1f} only by the tags, {:.1f} changed;",
                       double(touch_marks) / double(census_ops), double(tag_only) / double(census_ops),
                       double(setter + inl) / double(census_ops));
  for (u32 b = 0; b < 18; ++b)
    if (const uint64_t n = g_census_setter_fields[b].exchange(0, std::memory_order_relaxed))
      out += fmt::format(" {} {}", kNames[b], n);
  out += "; inline by field:";
  for (u32 b = 0; b < 18; ++b)
    if (const uint64_t n = g_census_inline_fields[b].exchange(0, std::memory_order_relaxed))
      out += fmt::format(" {} {}", kNames[b], n);
  return out;
}

// banjo-nx (2026-10-27, banjo_renut_shadow_sort): the 10 s line (empty when nothing was planned or replayed).
std::string ShadowSortReport() {
  const uint64_t plans = g_sort_plans.exchange(0, std::memory_order_relaxed);
  if (!plans && !g_sort_replayed_runs && !g_sort_refused_runs)
    return {};
  const auto take = [](std::atomic<uint64_t> &a) { return a.exchange(0, std::memory_order_relaxed); };
  const uint64_t draws = take(g_sort_draws);
  uint64_t why[kSortWhyCount], len[6], end[6];
  for (u32 k = 0; k < kSortWhyCount; ++k)
    why[k] = take(g_sort_why[k]);
  for (u32 k = 0; k < 6; ++k) {
    len[k] = take(g_sort_lengths[k]);
    end[k] = take(g_sort_end[k]);
  }
  const double d = draws ? double(draws) : 1.0;
  std::string out = fmt::format(
      "shadow sort (banjo_renut_shadow_sort): {} recordings planned, {} draws: {:.1f} % sortable (not: off {}, the "
      "call {}, no depth target {}, stencil {}, colour written {}, depth test {}, shaders {}); runs of sortable draws by length: "
      "1-2 {}, 3-4 {}, 5-8 {}, 9-16 {}, 17-64 {}, more {}; ended by the recording's end {}, a run op {}, a clear or "
      "resolve {}, other targets {}, an unsortable draw {}; {} runs of 3+ ({} draws), {} sorted ({} draws): shader "
      "changes {} -> {}, {} slots copied; replayed sorted {} runs ({} draws), {} refused by their shaders; verify: {} "
      "devices compared, {} differ",
      plans, draws, 100.0 * double(why[kSortOk]) / d, why[kSortOff], why[kSortCall], why[kSortNoDepth],
      why[kSortStencil], why[kSortColor], why[kSortDepthFunc], why[kSortShader], len[0], len[1], len[2], len[3], len[4],
      len[5], end[0],
      end[1], end[2], end[3], end[4], take(g_sort_runs), take(g_sort_run_draws), take(g_sort_sorted_runs),
      take(g_sort_sorted_draws), take(g_sort_changes_before), take(g_sort_changes_after), take(g_sort_pairs),
      g_sort_replayed_runs, g_sort_replayed_draws, g_sort_refused_runs, g_sort_verified, g_sort_verify_differ);
  g_sort_replayed_runs = g_sort_replayed_draws = g_sort_refused_runs = 0;
  g_sort_verified = g_sort_verify_differ = 0;
  return out;
}

std::string TagsReport() {
  const uint64_t ops = g_tag_ops.exchange(0, std::memory_order_relaxed);
  const uint64_t marked_ops = g_tag_marked_ops.exchange(0, std::memory_order_relaxed);
  const uint64_t marked = g_tag_marked.exchange(0, std::memory_order_relaxed);
  const uint64_t changed = g_tag_changed.exchange(0, std::memory_order_relaxed);
  const uint64_t checks = g_tag_checks.exchange(0, std::memory_order_relaxed);
  const uint64_t unmarked = g_tag_unmarked.exchange(0, std::memory_order_relaxed);
  if (!ops)
    return {};
  return fmt::format("recording from marks (banjo_renut_tags): {} ops, {:.1f} % compared from the setters' marks, {:.1f} "
                     "chunks marked an op of those (of {}), {:.1f} chunks changed an op; {} whole checks, {} chunks "
                     "changed without a mark{}",
                     ops, 100.0 * double(marked_ops) / double(ops),
                     marked_ops ? double(marked) / double(marked_ops) : 0.0, kPlainChunks,
                     double(changed) / double(ops), checks, unmarked,
                     g_tags_failed.load(std::memory_order_relaxed) ? " (marks off: every chunk compared)" : "");
}

CommandBufferMemory CommandBufferMemoryUsage() {
  CommandBufferMemory usage;
  {
    std::lock_guard lock(g_record_mutex);
    for (const auto &[va, buffer] : g_buffers) {
      if (buffer) {
        usage.recorded_bytes += CapacityBytes(*buffer);
        ++usage.recorded;
      }
    }
  }
  {
    std::lock_guard lock(g_free_mutex);
    usage.spare_bytes = g_free_bytes;
    usage.spare = g_free_buffers.size();
  }
  usage.live = g_live_buffers.load(std::memory_order_relaxed);
  return usage;
}

} // namespace renut::gpu
