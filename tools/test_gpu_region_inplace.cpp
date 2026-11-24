// Host test (2026-11-24): GpuRegionRanges::giveBackBottomInPlace neither mallocs nor frees.
// _tools/llvm/bin/clang++.exe -std=c++20 -I app/thirdparty/plume tools/test_gpu_region_inplace.cpp -o out/t.exe (sasrt-nx's clang; any clang++ with C++20)
#include "plume_deko3d_region.h"
#include <cstdio>
#include <cstdlib>
#include <new>
static int g_allocs = 0;
void* operator new(std::size_t n) { ++g_allocs; void* p = std::malloc(n); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { ++g_allocs; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ++g_allocs; std::free(p); }
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main() {
  const uint64_t step = 32ull << 20;
  uintptr_t heapEnd = 0;
  auto raise = [&](uintptr_t low) { heapEnd = low; return true; };
  // free bottom of 3.5 steps, a block above, another free range
  plume::GpuRegionRanges r;
  r.low = 0x10000000; r.high = 0x40000000;
  r.free.emplace(r.low, 3 * step + step / 2);
  r.free.emplace(0x30000000, step);
  int before = g_allocs;
  uint64_t g = r.giveBackBottomInPlace(step, raise);
  CHECK(g_allocs == before);                   // no allocation or free
  CHECK(g == 3 * step);
  CHECK(r.low == 0x10000000 + 3 * step && heapEnd == r.low);
  CHECK(r.free.begin()->first == r.low && r.free.begin()->second == step / 2);
  CHECK(r.free.size() == 2 && r.givenBack == 3 * step && r.givebacks == 1);
  CHECK(r.freeBottom() == step / 2);
  // what is left (half a step): nothing to give
  CHECK(r.giveBackBottomInPlace(step, raise) == 0);
  // an exact multiple: one step stays, so the node is never erased
  plume::GpuRegionRanges e;
  e.low = 0x10000000; e.high = 0x40000000;
  e.free.emplace(e.low, 4 * step);
  before = g_allocs;
  CHECK(e.giveBackBottomInPlace(step, raise) == 3 * step && g_allocs == before);
  CHECK(e.free.size() == 1 && e.free.begin()->first == e.low && e.free.begin()->second == step);
  CHECK(e.giveBackBottomInPlace(step, raise) == 0);  // a single step: kept
  // refused raise: nothing changes
  plume::GpuRegionRanges f;
  f.low = 0x10000000; f.high = 0x40000000;
  f.free.emplace(f.low, 5 * step);
  CHECK(f.giveBackBottomInPlace(step, [](uintptr_t) { return false; }) == 0);
  CHECK(f.low == 0x10000000 && f.free.begin()->second == 5 * step);
  // no free bottom (a block at the bottom)
  plume::GpuRegionRanges b;
  b.low = 0x10000000; b.high = 0x40000000;
  b.free.emplace(0x20000000, 5 * step);
  CHECK(b.giveBackBottomInPlace(step, raise) == 0);
  // the old giveBackBottom does allocate (the reason for the new one)
  plume::GpuRegionRanges o;
  o.low = 0x10000000; o.high = 0x40000000;
  o.free.emplace(o.low, 3 * step + step / 2);
  before = g_allocs;
  CHECK(o.giveBackBottom(step, raise) == 3 * step);
  std::printf("old giveBackBottom: %d allocator calls; in place: 0\nall OK\n", g_allocs - before);
  return 0;
}
