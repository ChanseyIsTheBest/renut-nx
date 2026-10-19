/**
 * @file    gpu/backend/queued.cpp
 */
#include "renut_engine/gpu/backend/queued.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "renut_engine/gpu/guest/callbacks.h"
#include "renut_engine/gpu/guest/command_buffers.h"
#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/banjo_renut_tuning.h"

namespace renut::gpu {

namespace {

// What a queued draw reads besides guest memory the game keeps until a fence.
struct DrawCapture {
  std::vector<u8> state;  // the device, kDeviceSnapshotSize bytes
  std::vector<u8> vertices, indices;
  DrawCall call;
};

// banjo-nx (banjo_renut_capture_pool): a draw's capture comes from a pool and goes back to it once the render thread
// has drawn it. Each draw made three heap allocations on the game thread (the capture, its 12.7 KB device snapshot,
// the queued closure) and freed them on the render thread: about 2,000 a frame through the Switch's one malloc lock.
// A pooled capture keeps its snapshot's storage, and the closure holds a plain pointer, which std::function stores
// without allocating. Off: every capture is deleted after use, as before (a new one per draw).
class CapturePool {
public:
  DrawCapture *Take() {
    {
      std::lock_guard lock(mutex_);
      if (!free_.empty()) {
        DrawCapture *capture = free_.back();
        free_.pop_back();
        return capture;
      }
    }
    return new DrawCapture();
  }
  void Give(DrawCapture *capture) {
    if (!capture)
      return;
    if (banjo::renut_gpu::CapturePoolOn()) {
      capture->vertices.clear();
      capture->indices.clear();
      std::lock_guard lock(mutex_);
      if (free_.size() < kMaxFree) {
        free_.push_back(capture);
        return;
      }
    }
    delete capture;
  }

private:
  static constexpr size_t kMaxFree = 2048;  // more than a frame's draws in flight
  std::mutex mutex_;
  std::vector<DrawCapture *> free_;
};
CapturePool g_capture_pool;

// A Begin*Vertices draw waiting for its vertices, per device.
struct DeferredDraw {
  DrawCapture *capture = nullptr;  // owned (g_capture_pool)
  const u8 *scratch = nullptr;
  u32 bytes = 0;
};
std::mutex g_deferred_mutex;
std::unordered_map<u32, DeferredDraw> g_deferred;

// The original draw calls consume the device's dirty masks as they write
// their packets; a snapshot taken after this has them clear, as the command
// buffer ops' do.
void ClearPending(d3d::Device *device) {
  for (auto &mask : device->m_Pending)
    mask = 0;
}

std::vector<u8> Snapshot(const u8 *device) {
  return std::vector<u8>(device, device + d3d::kDeviceSnapshotSize);
}

void QueueCapture(DrawCapture *capture) {
  render_thread::Queue([capture] {
    DrawCall call = capture->call;
    if (!capture->vertices.empty())
      call.up_vertices = capture->vertices.data();
    if (!capture->indices.empty())
      call.up_indices = capture->indices.data();
    DrawStateLocked(capture->state.data(), call);
    g_capture_pool.Give(capture);
  });
}

DrawCapture *Capture(u32 device_va, const DrawCall &call) {
  auto *device = GuestPtr<d3d::Device>(device_va);
  if (!device)
    return nullptr;
  ClearPending(device);
  DrawCapture *capture = g_capture_pool.Take();
  // assign() keeps a pooled capture's storage (Snapshot() made a new vector each time).
  const auto *bytes = reinterpret_cast<const u8 *>(device);
  capture->state.assign(bytes, bytes + d3d::kDeviceSnapshotSize);
  capture->call = call;
  capture->call.up_vertices = nullptr;
  capture->call.up_indices = nullptr;
  return capture;
}

} // namespace

void QueueDraw(u32 device_va, const DrawCall &call, u32 up_vertex_bytes, u32 up_index_bytes) {
  FlushDeferredVertices(device_va);
  RunReadyCallbacks();
  DrawCapture *capture = Capture(device_va, call);
  if (!capture)
    return;
  if (call.up_vertices)
    capture->vertices.assign(call.up_vertices, call.up_vertices + up_vertex_bytes);
  if (call.up_indices)
    capture->indices.assign(call.up_indices, call.up_indices + up_index_bytes);
  QueueCapture(capture);
}

void QueueDeferredDraw(u32 device_va, const DrawCall &call, const u8 *scratch, u32 bytes) {
  FlushDeferredVertices(device_va);
  RunReadyCallbacks();
  DrawCapture *capture = Capture(device_va, call);
  if (!capture)
    return;
  if (!scratch) {
    g_capture_pool.Give(capture);
    return;
  }
  std::lock_guard lock(g_deferred_mutex);
  DeferredDraw &slot = g_deferred[device_va];
  g_capture_pool.Give(slot.capture);  // one not flushed (the original replaced it the same way)
  slot = {capture, scratch, bytes};
}

void FlushDeferredVertices(u32 device_va) {
  FlushRecordedVertices(device_va);
  DeferredDraw deferred;
  {
    std::lock_guard lock(g_deferred_mutex);
    auto it = g_deferred.find(device_va);
    if (it == g_deferred.end())
      return;
    deferred = std::move(it->second);
    g_deferred.erase(it);
  }
  deferred.capture->vertices.assign(deferred.scratch, deferred.scratch + deferred.bytes);
  QueueCapture(deferred.capture);
}

void QueueClear(u32 device_va, u32 flags, const ClearRect *rect, const float color[4], float depth,
                u32 stencil) {
  FlushDeferredVertices(device_va);
  RunReadyCallbacks();
  const auto *device = GuestPtr<const u8>(device_va);
  if (!device)
    return;
  struct Args {
    std::vector<u8> state;
    bool has_rect;
    ClearRect rect;
    float color[4];
  };
  auto args = std::make_shared<Args>();
  args->state = Snapshot(device);
  args->has_rect = rect != nullptr;
  args->rect = rect ? *rect : ClearRect{};
  std::memcpy(args->color, color, sizeof(args->color));
  render_thread::Queue([args, flags, depth, stencil] {
    ClearStateLocked(args->state.data(), flags, args->has_rect ? &args->rect : nullptr, args->color,
                     depth, stencil);
  });
}

void QueueResolve(u32 device_va, u32 flags, const ClearRect *source_rect, u32 dest_texture_va,
                  i32 dest_x, i32 dest_y, u32 dest_level, u32 dest_slice,
                  const float clear_color[4], float clear_depth, u32 clear_stencil) {
  FlushDeferredVertices(device_va);
  RunReadyCallbacks();
  const auto *device = GuestPtr<const u8>(device_va);
  if (!device)
    return;
  struct Args {
    std::vector<u8> state;
    bool has_rect, has_color, has_dest_format;
    ClearRect rect;
    float color[4];
    // The destination header as it is now: the game often builds it on the
    // stack (as for recorded resolves).
    d3d::TextureFetchConstant dest_format;
  };
  auto args = std::make_shared<Args>();
  args->state = Snapshot(device);
  args->has_rect = source_rect != nullptr;
  args->rect = source_rect ? *source_rect : ClearRect{};
  args->has_color = clear_color != nullptr;
  if (clear_color)
    std::memcpy(args->color, clear_color, sizeof(args->color));
  const auto *header = GuestPtr<const d3d::BaseTexture>(dest_texture_va);
  args->has_dest_format = header != nullptr;
  if (header)
    args->dest_format = header->Format;
  render_thread::Queue([args, flags, dest_texture_va, dest_x, dest_y, dest_level, dest_slice,
                        clear_depth, clear_stencil] {
    ResolveStateLocked(args->state.data(), flags, args->has_rect ? &args->rect : nullptr,
                       dest_texture_va, args->has_dest_format ? &args->dest_format : nullptr,
                       dest_x, dest_y, dest_level, dest_slice,
                       args->has_color ? args->color : nullptr, clear_depth, clear_stencil);
  });
}

} // namespace renut::gpu
