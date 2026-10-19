// banjo-nx - hand-written native versions of hot guest functions (CPU enhancements banjo_hand_*).
//
// The localized copies of tools/localize_native.py keep a function's own statements; these replace the statements:
// the XDK C runtime's memcpy and memset and Direct3D's two shader float-constant writers, written from what the
// routines do. They are sa2-nx's natives (app/src/cpu/hot/n_827F05C0.h, n_827EFEA0.h, n_d3d_constants.h, GPL-3.0;
// the memcpy came there from masseffect-nx's n_82AC4AF0.h): tools/crt_signatures.py of sa2-nx finds the same three C
// runtime routines in Banjo's image (unique matches), and the two constant writers are the same instructions with
// the same two offsets. Banjo's own additions are the two writers of Direct3D's state flush (namespace flush below).
// What is different here is how they are used and checked:
//
//   * Each is a template on the store policy of banjo_hot_common.h, so the guard's logged form records what it is
//     about to write like any generated copy (no hand-written list of written ranges).
//   * tools/localize_native.py puts them IN FRONT of the generated copy of the same function wherever that copy is
//     used: as a hook of its own, and inside every other function's copy that contains it (app/src/cpu/hot/hand.txt).
//     Inside a caller's copy the call is inlined: no hook, no hand-over of every register (on the Switch that
//     hand-over is what made copies with real calls slower than their originals).
//   * A native returns false to DECLINE a call it does not cover (nothing written yet): the generated copy of the
//     original then runs, in the same place.
//   * The registers the original leaves behind as scratch (hand.txt gives them per function; tools/localize_native.py
//     takes them from the original's own statements) are NOT reproduced: the natives write no register. Those
//     registers are left out of the guard's register compare for every hook whose copy contains the native. Sound
//     because they are caller-saved scratch of assembly routines and of a callee the callers reach through the
//     normal calling convention: a caller that read one after the call would get a different value, would act on
//     it, and the guard would see that in everything else it compares.
//
// ARM (the A57 is where this has to pay, and sa2-nx's first native Direct3D layer lost there for reasons no PC run
// showed: docs/api-renderer.md of sa2-nx, section 6): nothing here is per thread (no thread_local: a function call on
// the Switch), nothing copies a context, no loop keeps a count where a store could alias it, nothing is byte-swapped
// word by word (the copies are guest order to guest order), and sizes up to 128 bytes (memcpy; 64 for memset) are
// moved with overlapping 16/8/4/2-byte loads and stores without a call into the C library. In a PC run of title,
// menus, intro and gameplay 62 % of the 21.9 million memcpy calls were under 64 bytes and 21 % were 64 to 127; 46 %
// of the memsets were 4 to 7 bytes; 98 % of the 26 million constant uploads were 1 to 7 vectors.
//
// Guest function addresses may be named in this folder only (tools/direct_calls.py and tools/localize_native.py
// read every other source's addresses as hooked functions; these four are hooked, by app/src/cpu/hot/localized.txt).
#pragma once

#include <cstdint>
#include <cstring>

#include <rex/ppc/context.h>

#include "cpu/banjo_hot_common.h"

namespace banjo::hand {

// The natives' own keys (banjo_hot_guest.cpp), each read once: 0 not read yet, 1 off, 2 on.
enum : uint32_t { kMemcpy = 0, kMemset = 1, kConstants = 2, kFlush = 3, kNatives = 4 };
extern uint8_t g_state[kNatives];
uint8_t Init(uint32_t which);
[[gnu::always_inline]] inline bool On(uint32_t which) {
  const uint8_t state = g_state[which];
  if (state == 2) [[likely]] return true;
  return state == 0 && Init(which) == 2;
}

namespace detail {

// 16 bytes through a register (any alignment; one NEON load or store on the A57).
struct B16 {
  uint8_t b[16];
};
[[gnu::always_inline]] inline B16 L16(const uint8_t* p) {
  B16 v;
  std::memcpy(v.b, p, 16);
  return v;
}
[[gnu::always_inline]] inline void S16(uint8_t* p, const B16& v) { std::memcpy(p, v.b, 16); }

// 1 <= n <= 64; all loads before all stores (memmove semantics for dst <= src overlap).
[[gnu::always_inline]] inline void CopySmall(uint8_t* d, const uint8_t* s, uint32_t n) {
  if (n >= 16) {
    if (n > 32) {
      const B16 a = L16(s), b = L16(s + 16), c = L16(s + n - 32), e = L16(s + n - 16);
      S16(d, a);
      S16(d + 16, b);
      S16(d + n - 32, c);
      S16(d + n - 16, e);
    } else {
      const B16 a = L16(s), e = L16(s + n - 16);
      S16(d, a);
      S16(d + n - 16, e);
    }
  } else if (n >= 8) {
    uint64_t a, e;
    std::memcpy(&a, s, 8);
    std::memcpy(&e, s + n - 8, 8);
    std::memcpy(d, &a, 8);
    std::memcpy(d + n - 8, &e, 8);
  } else if (n >= 4) {
    uint32_t a, e;
    std::memcpy(&a, s, 4);
    std::memcpy(&e, s + n - 4, 4);
    std::memcpy(d, &a, 4);
    std::memcpy(d + n - 4, &e, 4);
  } else if (n >= 2) {
    uint16_t a, e;
    std::memcpy(&a, s, 2);
    std::memcpy(&e, s + n - 2, 2);
    std::memcpy(d, &a, 2);
    std::memcpy(d + n - 2, &e, 2);
  } else {
    *d = *s;
  }
}

// 64 < n <= 128; all loads before all stores. (Banjo's calls: a fifth are 64 to 127 bytes.)
[[gnu::always_inline]] inline void CopyMedium(uint8_t* d, const uint8_t* s, uint32_t n) {
  const B16 a = L16(s), b = L16(s + 16), c = L16(s + 32), e = L16(s + 48);
  const B16 f = L16(s + n - 64), g = L16(s + n - 48), h = L16(s + n - 32), i = L16(s + n - 16);
  S16(d, a);
  S16(d + 16, b);
  S16(d + 32, c);
  S16(d + 48, e);
  S16(d + n - 64, f);
  S16(d + n - 48, g);
  S16(d + n - 32, h);
  S16(d + n - 16, i);
}

// 1 <= n <= 64, every byte = v.
[[gnu::always_inline]] inline void FillSmall(uint8_t* d, uint8_t v, uint32_t n) {
  if (n >= 16) {
    B16 x;
    std::memset(x.b, v, 16);
    S16(d, x);
    S16(d + n - 16, x);
    if (n > 32) {
      S16(d + 16, x);
      S16(d + n - 32, x);
    }
  } else {
    const uint64_t x = uint64_t(v) * 0x0101010101010101ull;
    if (n >= 8) {
      std::memcpy(d, &x, 8);
      std::memcpy(d + n - 8, &x, 8);
    } else if (n >= 4) {
      std::memcpy(d, &x, 4);
      std::memcpy(d + n - 4, &x, 4);
    } else if (n >= 2) {
      std::memcpy(d, &x, 2);
      std::memcpy(d + n - 2, &x, 2);
    } else {
      *d = v;
    }
  }
}

// [a, a + n) is one stretch of host memory: it does not wrap around 4 GB and (on hosts with the physical-view
// offset) does not cross its boundary. n >= 1.
[[gnu::always_inline]] inline bool Straight(uint32_t a, uint32_t n) {
  if (uint64_t(a) + n > 0x100000000ull) return false;
  return ::banjo::hot::PhysOff(a) == ::banjo::hot::PhysOff(a + n - 1u);
}

}  // namespace detail

// sub_82BB4510 - XDK C runtime memcpy: r3 = dst, r4 = src, r5 = n (its 32-bit part); returns r3 as it came.
//
// The original stores r3 at -8(r1) first (a real store: it reloads r3 from there at every return), then copies
// FORWARD: a byte head up to 8-byte dst alignment, then 128-byte blocks / doublewords / words / bytes chosen by the
// alignment of src; dcbt / dcbtst are hints. Facts used:
//   * every size and address is the 32-bit part (cmplw, rlwinm, ea = u32 + u32);
//   * no loop reads past src + n or writes past dst + n;
//   * within each unit all loads precede its stores and units run upward: for dst <= src the result is memmove's,
//     for ranges that do not overlap a plain copy. For dst > src with dst - src < n the forward copy smears bytes it
//     already copied: DECLINED;
//   * n == 0 copies nothing (the save of r3 still happens).
// Also declined: ranges that wrap around 4 GB, and a destination that covers the save slot (the original would then
// return what was copied over it).
// Scratch the original leaves and this does not reproduce (hand.txt): r0, r4-r12.
namespace n_82BB4510 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kMemcpy)) [[unlikely]] return false;
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, n = ctx.r5.u32, slot = ctx.r1.u32 - 8u;
  s.Hand(kMemcpy, n);
  if (n) {
    if (uint32_t(dst - src - 1u) < n - 1u) [[unlikely]] return false;  // dst > src, dst - src < n
    if (!detail::Straight(dst, n) || !detail::Straight(src, n)) [[unlikely]] return false;
    if (uint64_t(uint32_t(slot + 7u - dst)) < uint64_t(n) + 7u) [[unlikely]] return false;  // dst covers the slot
  }
  if (!s.Note(slot, 8)) [[unlikely]] return true;
  const uint64_t saved = __builtin_bswap64(ctx.r3.u64);
  std::memcpy(::banjo::hot::Raw(base, slot), &saved, 8);
  if (n == 0) return true;
  if (!s.Note(dst, n)) [[unlikely]] return true;
  uint8_t* d = ::banjo::hot::Raw(base, dst);
  const uint8_t* p = ::banjo::hot::Raw(base, src);
  if (n <= 64) {
    detail::CopySmall(d, p, n);
  } else if (n <= 128) {
    detail::CopyMedium(d, p, n);
  } else if (uint32_t(src - dst) < n) {
    std::memmove(d, p, n);  // dst < src (or equal), overlapping: the forward copy is memmove
  } else {
    std::memcpy(d, p, n);
  }
  return true;
}

}  // namespace n_82BB4510

// sub_82BB4A40 - XDK C runtime memset: r3 = dst, r4 = value (its low byte), r5 = n; r3 is not written.
//
// The original (ori r6,r3,0; ctr = n + 1): single bytes until r6 is 4-aligned or n is used up, the low byte of r4
// replicated into a word, 16-byte blocks of four stw, up to three stw, up to three stb. Every store writes that byte,
// each byte of [dst, dst + n) exactly once, nothing outside, and nothing is read. Facts used:
//   * ctr = r5 + 1 is 64-bit: with anything in the upper half of r5 the head loop could run past n: DECLINED;
//   * ranges that wrap around 4 GB are declined.
// Scratch not reproduced (hand.txt): r0, r4, r5, r6.
namespace n_82BB4A40 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kMemset)) [[unlikely]] return false;
  if (ctx.r5.u64 >> 32) [[unlikely]] return false;
  const uint32_t dst = ctx.r3.u32, n = ctx.r5.u32;
  s.Hand(kMemset, n);
  if (n == 0) return true;
  if (!detail::Straight(dst, n)) [[unlikely]] return false;
  if (!s.Note(dst, n)) [[unlikely]] return true;
  uint8_t* d = ::banjo::hot::Raw(base, dst);
  if (n <= 64) {
    detail::FillSmall(d, ctx.r4.u8, n);
  } else {
    std::memset(d, int(ctx.r4.u8), n);
  }
  return true;
}

}  // namespace n_82BB4A40

// Direct3D's two shader float-constant writers (called for every constant upload of every draw):
//   sub_8222EFA8: r3 = device, r4 = first constant, r5 = source (any alignment), r6 = count of 16-byte vectors,
//                 r7 = 64-bit dirty bits. Destination ((r3 + (r4 + 120) * 16) & ~15) + 16 k; dirty mask at r3 + 0.
//   sub_8222F0D8: the same with (r4 + 376) and the dirty mask at r3 + 8.
// The originals copy four vectors per iteration, then one per iteration: lvlx + lvrx + vor is an unaligned 16-byte
// load (lvrx at an aligned address loads nothing), stvx128 an aligned store, dcbt are hints; then ld / or r7 / std on
// the dirty mask. Facts used:
//   * every address is the 32-bit part. The counters here are 32-bit compares (addic. / cmplwi), but r9 = r6 is
//     decremented as 64 bits: a count with anything in its upper half is DECLINED;
//   * the four-at-a-time loop loads its four vectors before it stores them: ranges that overlap are DECLINED, as are
//     ranges that wrap around 4 GB;
//   * the dirty mask is read after the copy, in the same order here;
//   * the originals' first instruction saves r31 at -8(r1). The generated code keeps r31 in a local of the function
//     (zero at that point, the context's r31 is never touched) and makes the store unless the build elides dead
//     saves (REX_NV_SAVES_ELIDE, off in the shipped build): eight zero bytes there, written first, as the original
//     does. (Should a later code generator keep r31 in the context instead, the guard reports the slot's bytes.)
// Scratch not reproduced (hand.txt): r4, r5, r6, r8-r11, v0, v10-v13.
namespace constants {

template <class S>
[[gnu::always_inline]] inline bool Upload(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s,
                                          uint32_t first, uint32_t mask_offset) {
  if (!On(kConstants)) [[unlikely]] return false;
  if (ctx.r6.u64 >> 32) [[unlikely]] return false;
  const uint32_t count = ctx.r6.u32, src = ctx.r5.u32, mask = ctx.r3.u32 + mask_offset;
  const uint32_t dst = (((ctx.r4.u32 + first) << 4) + ctx.r3.u32) & ~15u;
  s.Hand(kConstants, count);
  if (mask > 0xFFFFFFF7u) [[unlikely]] return false;
  if (count > 0x0FFFFFFFu) [[unlikely]] return false;
  const uint32_t bytes = count << 4;
  if (count) {
    if (!detail::Straight(dst, bytes) || !detail::Straight(src, bytes)) [[unlikely]] return false;
    if (uint32_t(src - dst) < bytes || uint32_t(dst - src) < bytes) [[unlikely]] return false;
  }
#if !defined(REX_NV_SAVES_ELIDE)
  {
    const uint32_t slot = ctx.r1.u32 - 8u;  // std r31,-8(r1), the local r31 (zero)
    if (!s.Note(slot, 8)) [[unlikely]] return true;
    std::memset(::banjo::hot::Raw(base, slot), 0, 8);
  }
#endif
  if (count) {
    if (!s.Note(dst, bytes)) [[unlikely]] return true;
    uint8_t* d = ::banjo::hot::Raw(base, dst);
    const uint8_t* p = ::banjo::hot::Raw(base, src);
    if (count <= 8) {
      const uint8_t* const end = p + bytes;  // (a pointer cursor: no store here can alias the loop's bound)
      do {
        detail::S16(d, detail::L16(p));
        d += 16;
        p += 16;
      } while (p != end);
    } else {
      std::memcpy(d, p, bytes);
    }
  }
  if (!s.Note(mask, 8)) [[unlikely]] return true;
  uint64_t m;
  std::memcpy(&m, ::banjo::hot::Raw(base, mask), 8);
  m |= __builtin_bswap64(ctx.r7.u64);
  std::memcpy(::banjo::hot::Raw(base, mask), &m, 8);
  return true;
}

}  // namespace constants

namespace n_8222EFA8 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  return constants::Upload(ctx, base, s, 120, 0);
}
}  // namespace n_8222EFA8

namespace n_8222F0D8 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  return constants::Upload(ctx, base, s, 376, 8);
}
}  // namespace n_8222F0D8

// Direct3D's state flush, the two functions that turn dirty bits into PM4 packets in the command buffer (called by
// the flush of every draw, once per dirty block of state):
//   sub_8222CB90: r3 = device, r4 = 64 dirty bits (bit 63 first), r5 = register number of the first bit, r6 = the
//                 shadow words (word i belongs to bit i). For every run of set bits: a type-0 header
//                 ((length - 1) << 16 | register) and the run's words, appended after the write pointer at device + 48
//                 (which names the last word written); the new pointer is stored at the end.
//   sub_8222D0E8: the same for the float constants, one bit per block of four vectors (16 registers, 64 bytes): the
//                 run is first padded with type-2 packets (0x80000000) so that its vectors land 16-aligned (the
//                 original stores them with stvx), then header ((16 * blocks - 1) << 16 | register) and the blocks.
// Facts used:
//   * both reload the buffer's limit (device + 52) for every run and call Direct3D's "more space" function when a
//     run does not fit: a call in which a run could fail is DECLINED before anything is written (Runs below);
//   * no dirty bit at all is declined (the originals would then write a run of length 0 and loop);
//   * words go from guest memory to guest memory unchanged: no byte order is touched;
//   * the constants' source must be 16-aligned and the write pointer a multiple of 4 (the vector loads and stores
//     round their addresses down): otherwise declined;
//   * the buffer between the write pointer and its limit must not overlap the shadow words or the device's two
//     pointer words (declined), and no address may be near the top of the 4 GB;
//   * the originals' prologue links a stack frame (stwu r1,-144 / -176): the old stack pointer is stored at the new
//     one unless the build elides the back chain (REX_BACK_CHAIN_ELIDE, off in the shipped build). Their saves of the
//     non-volatile registers are not stores in the generated code (those registers are locals of the function).
// Scratch not reproduced (hand.txt): r4-r12, and v0, v11-v13 for the constants. r1 and r3 end as they came.
namespace flush {

[[gnu::always_inline]] inline uint32_t Load32(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, ::banjo::hot::Raw(base, a), 4);
  return __builtin_bswap32(v);
}
[[gnu::always_inline]] inline void Store32(uint8_t* p, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(p, &v, 4);
}
// [a, a + n) and [b, b + m) share a byte (n, m >= 1; neither wraps).
[[gnu::always_inline]] inline bool Overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m) {
  return uint64_t(a) < uint64_t(b) + m && uint64_t(b) < uint64_t(a) + n;
}

// kUnit: bytes per dirty bit (4: register words, 64: constant blocks). kFrame: the original's frame size.
//
// One pass, no register saved for it, and only the tests exactness needs. (The first version checked every run in a
// pass of its own and compared the exact ranges: 131 A57 instructions for a call with one short run, where the
// recompiled original takes 152 on average. A second, in one pass but with every pair of ranges tested, still took
// 119. Measured with tools/hot_arm_compare.py and the Switch build's disassembly; none of it showed on the PC.)
//   * the runs are written into the window between the write pointer and the buffer's limit, so that window as a
//     whole is what must not touch the 64 shadow units, the device's two pointer words or the frame word;
//   * a run that does not fit is where the original calls for more space. Whether any run could fail is decided
//     up front from the number of dirty bits alone: n bits make at most n runs, each at most 16 bytes of padding and
//     header besides its words, so with start + 16 n + (bytes of n units) + 12 below the limit every run passes the
//     original's own test and none of them is tested again. Otherwise the call is DECLINED before anything is
//     written (about 1 call in 1000 in a PC run; the original runs, with its call for space). (Handing the rest of
//     the bits to the original at the first run that fails is also exact, but those calls reach a call the guard
//     cannot repeat, so that path would have run unchecked.)
template <class S, uint32_t kUnit, uint32_t kFrame>
[[gnu::always_inline]] inline bool Runs(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kFlush)) [[unlikely]] return false;
  uint64_t m = ctx.r4.u64;
  if (m == 0) [[unlikely]] return false;
  const uint32_t device = ctx.r3.u32, reg0 = ctx.r5.u32, src = ctx.r6.u32, frame = ctx.r1.u32 - kFrame;
  constexpr uint32_t kShadow = 64u * kUnit;  // the shadow bytes 64 dirty bits can name (256 or 4096)
  // No address near the top of the 4 GB, so no sum below wraps.
  if (device > 0xFFFFE000u || frame > 0xFFFFE000u || src > 0xFFFFE000u) [[unlikely]] return false;
  if (kUnit == 64 && (src & 15u)) [[unlikely]] return false;
  // The original's first store, the frame's back chain, comes before it reads anything. Made here before the checks
  // that need the buffer's pointers: should one of them decline, the original starts by storing the same word.
#if !defined(REX_BACK_CHAIN_ELIDE)
  if (!s.Note(frame, 4)) [[unlikely]] return true;
  Store32(::banjo::hot::Raw(base, frame), ctx.r1.u32);  // stwu r1,-frame(r1)
#endif
  const uint32_t start = Load32(base, device + 48), limit = Load32(base, device + 52);
  constexpr uint32_t kWordsPerBit = kUnit / 4;
  const uint32_t bits = uint32_t(__builtin_popcountll(m));
  // Every run fits as the original judges it (above); this also says limit > start + 12.
  if (uint64_t(start) + 16u * bits + 4u * kWordsPerBit * bits + 12u >= limit) [[unlikely]] return false;
  if (limit > 0xFFFFFFE0u || (kUnit == 64 && (start & 3u))) [[unlikely]] return false;
  // Every word written lies in [start + 4, limit + 8): a run is only written when its last word is below limit.
  // That window must not touch the shadow units (they are read while it is written) nor the device's two pointer
  // words (the original reads the limit again for every run and stores the pointer at the end). The frame word
  // needs no test: it is written before everything else, here as there.
  const uint32_t window = limit - start + 8u;
  if (Overlap(start, window, src, kShadow) || Overlap(start, window, device + 48, 8)) [[unlikely]] return false;
  if (::banjo::hot::PhysOff(start) != ::banjo::hot::PhysOff(limit + 7u) ||
      ::banjo::hot::PhysOff(src) != ::banjo::hot::PhysOff(src + kShadow - 1u))
    [[unlikely]] return false;
  s.Hand(kFlush, bits);
  uint8_t* const buffer = ::banjo::hot::Raw(base, start);  // the word the write pointer names
  const uint8_t* const shadow = ::banjo::hot::Raw(base, src);
  uint32_t wp = start;
  uint32_t bit = 0;  // the dirty bit the top of m stands for
  do {
    const uint32_t zeros = uint32_t(__builtin_clzll(m));
    m <<= zeros;
    bit += zeros;
    const uint32_t length = ~m ? uint32_t(__builtin_clzll(~m)) : 64u;
    const uint32_t words = length * kWordsPerBit, bytes = words * 4u;
    // (sub_8222CB90's test, wp + 4 * length < limit, and sub_8222D0E8's, wp + 4 * (16 * blocks + 3) < limit, hold
    // for every run: decided above.)
    uint32_t pads = 0;
    if (kUnit == 64) {
      const uint32_t at = wp & 15u;  // 8: the header lands on 12 already
      pads = at == 4 ? 1u : at == 0 ? 2u : at == 12 ? 3u : 0u;
    }
    const uint32_t run_bytes = 4u * pads + 4u + bytes;
    if (!s.Note(wp + 4u, run_bytes)) [[unlikely]] return true;
    uint8_t* d = buffer + (wp - start) + 4;
    for (uint32_t k = 0; k < pads; ++k) {
      Store32(d, 0x80000000u);
      d += 4;
    }
    Store32(d, (((words - 1u) << 16) & 0xFFFF0000u) | (reg0 + bit * kWordsPerBit));
    d += 4;
    const uint8_t* p = shadow + size_t(bit) * kUnit;
    if (bytes <= 64) {
      detail::CopySmall(d, p, bytes);
    } else {
      std::memcpy(d, p, bytes);
    }
    wp += run_bytes;
    bit += length;
    m = length == 64 ? 0 : m << length;
  } while (m);
  if (!s.Note(device + 48, 4)) [[unlikely]] return true;
  Store32(::banjo::hot::Raw(base, device + 48), wp);
  return true;
}

}  // namespace flush

namespace n_8222CB90 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  return flush::Runs<S, 4, 144>(ctx, base, s);
}
}  // namespace n_8222CB90

namespace n_8222D0E8 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  return flush::Runs<S, 64, 176>(ctx, base, s);
}
}  // namespace n_8222D0E8

}  // namespace banjo::hand
