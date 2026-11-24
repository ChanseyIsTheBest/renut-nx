// banjo-nx (2026-10-17): Renut's renderer on plume's deko3d back end (banjo_renut_deko3d.h, docs/deko3d-renderer.md).
#include "renut_engine/gpu/banjo_renut_deko3d.h"

#include <algorithm>
#include <chrono>  // renut-nx (2026-11-01)
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <unistd.h>  // ftruncate (LoadCache)

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <zstd.h>

#include "renut_engine/gpu/banjo_renut_tuning.h"
#include "renut_engine/gpu/ultimate.h"
#include "renut_engine/gpu/sasrt_gpu_round.h"  // sasrt-nx (2026-11-08)
#include "renut_engine/gpu/guest/command_buffers.h"  // banjo-nx (2026-10-28): ReleaseSpareBuffersNow
#include "renut_engine/gpu/shaders/shader_cache.h"  // banjo-nx (2026-10-22): the installed package's shader table

// sasrt-nx (2026-11-08, the GPU round after the move to the ultimate base): defined on every platform so the tomls read
// the same; only the deko3d back end counts.
// Port 165 (from fnxf_xiii, its notes of 2026-10-25 and 10-29): plume_deko3d.h Deko3DOptions::swapInterval.
REXCVAR_DEFINE_INT32(banjo_deko3d_swap_interval, 1, "Banjo/Enhancements",
                     "deko3d: the display refreshes each image is held for with vsync on, 1-4 (fnxf_xiii's frame "
                     "interval: 2 shows a 30 fps game evenly at 60 Hz, paced by the display itself). 1: as before")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// Port 161 (from fnxf_xiii, its note of 2026-11-05): see DiagLogWriter.
REXCVAR_DEFINE_BOOL(banjo_deko3d_log_writer, true, "Banjo/Enhancements",
                    "deko3d.log's progress lines (memory blocks, compile threads, swapchain) written and flushed by a "
                    "thread of their own instead of the thread that said them (failures still at once). false: every "
                    "line written and flushed where it is said, as before")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// Port 161 (from fnxf_xiii, its note of 2026-10-30): plume_deko3d.h beforeBufferCopy. Off by default since port 250
// (2026-11-21, blur-nx's console bisect: with it on, Blur's races drew rainbow colour fringes, its release NRO too;
// docs/off-by-default.md section 1).
// banjo-nx (2026-11-20, the rebase's console runs: 20,000 stages on the generic variant): plume's logMisses.
// renut-nx (2026-11-22, the user: the slow boot): the DKSH cache's start took 101-105 s on the console with 195,826
// variants (one small read a record on the SD card). NAME.idx beside it lists the records (LoadCache).
REXCVAR_DEFINE_BOOL(banjo_deko3d_cache_index, true, "Banjo/Enhancements",
                    "The DKSH cache's index file (deko3d_shaders.bin.idx): the start reads the records' keys, offsets "
                    "and sizes from it in one read and walks only the records added since (false: the whole cache is "
                    "walked at every start, in 4 MB pieces)");
REXCVAR_DEFINE_BOOL(banjo_deko3d_log_misses, false, "Banjo/Diagnostics",
                    "deko3d renderer: every shader variant a draw got the generic one for (neither loaded nor in "
                    "deko3d_shaders.bin) is logged once to deko3d.log as a \"run-time variant\" line, for "
                    "tools/deko3d_variant_cache.py --runtime-log: the variants a cache made from the pipeline lists lacks")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_deko3d_copy_run_barriers, false, "Banjo/Enhancements",
                    "deko3d: no barrier between back-to-back buffer copies once a full one has run (the copy engine "
                    "was taken to run them one at a time; each full barrier is 2 GPU submission entries). Off by "
                    "default: with it on, Blur's console races drew rainbow colour fringes (port 250). "
                    "false: a barrier before every copy, as before port 161")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(sasrt_deko3d_diag_trace, false, "SASRT/Diagnostics",
                    "deko3d renderer, a measurement (the second GPU round of 2026-11-08): every 600th frame's command "
                    "stream logged as tokens ([deko3d trace] lines: barriers with their causes, draws, dispatches, target "
                    "binds, clears, depth decompressions, copies), to find work that changes nothing");
REXCVAR_DEFINE_BOOL(sasrt_deko3d_diag_barriers, false, "SASRT/Diagnostics",
                    "deko3d renderer, a measurement: the barriers emitted counted by what they wait for and why (the "
                    "layouts and stages of every call merged into them); the 10 s deko3d line then lists the commonest "
                    "causes since the start");
// renut-ultimate (2026-11-10, port 59, from LostOdysseyRecomp ff013164): descriptor writes. LostOdysseyRecomp writes a
// descriptor set for every draw, and these two were about 3 % of its command thread; Renut writes a texture slot when a
// texture, view or surface is made (a bindless table), so far fewer writes go through here.
REXCVAR_DEFINE_BOOL(lo_deko3d_descriptor_cache, true, "Banjo/Enhancements",
                    "deko3d renderer (from LostOdysseyRecomp): a sampled image descriptor is made once per texture view "
                    "and copied at later writes of that view, instead of being made again every time. Same descriptors")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(lo_deko3d_relaxed_descriptor_counters, true, "Banjo/Enhancements",
                    "deko3d renderer (from LostOdysseyRecomp): a descriptor write's count and epoch use relaxed atomic "
                    "increments, so the write does not wait for its store into the uncached descriptor memory to drain. "
                    "false: the ordered increments as before")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

#if defined(PLUME_DEKO3D)
#include <plume_deko3d.h>
#endif

// The GLSL package (tools/deko3d_shader_package.py: deko3d_glsl_switch.cpp embeds it); absent from builds made without
// one, hence weak.
extern const uint8_t g_deko3dGlslPackage[] __attribute__((weak));
extern const size_t g_deko3dGlslPackageSize __attribute__((weak));

REXCVAR_DECLARE(std::string, log_level);  // sdk/src/core/logging.cpp

namespace banjo::renut_gpu::deko3d {

namespace {

#pragma pack(push, 1)
struct PackageHeader {
  char magic[4];
  uint32_t version, count, dictSize, tableOffset, dataOffset, reserved[2];
};
struct PackageEntry {
  uint64_t hash;
  uint32_t offset, compressedSize, size, stage;
  uint32_t boolMask[8];
};
#pragma pack(pop)
static_assert(sizeof(PackageEntry) == 56, "tools/deko3d_shader_package.py's entry");

struct Package {
  std::once_flag once;
  bool ok = false;
  const PackageHeader *header = nullptr;
  const PackageEntry *entries = nullptr;
  const uint8_t *data = nullptr;
  ZSTD_DDict *dict = nullptr;
  std::mutex mutex;  // the decompression context
  ZSTD_DCtx *dctx = nullptr;
};
Package g_package;

// renut-ultimate (port 62): whether the installer's package (version 2, OpenInstalled below) has the game shaders.
bool InstalledPackageOk();

Package &Open() {
  std::call_once(g_package.once, [] {
    Package &p = g_package;
    // katamari-nx (2026-11-02, futari-nx's design): deko3d_glsl.bin beside the NRO first, read whole: a release NRO
    // carries no game shader (BANJO_RENUT_RELEASE_SHADERS) and its shader table is built in, so this file is version 1,
    // which the web installer makes from the player's game files. Held in memory: no card access while the game runs.
    static std::vector<uint8_t> beside;
    const uint8_t *base = nullptr;
    size_t size = 0;
    if (FILE *f = std::fopen(NextToExecutable("deko3d_glsl.bin").string().c_str(), "rb")) {
      PackageHeader h{};
      std::fseek(f, 0, SEEK_END);
      const long length = std::ftell(f);
      std::fseek(f, 0, SEEK_SET);
      if (length >= long(sizeof(h)) && std::fread(&h, sizeof(h), 1, f) == 1 && h.version == 1) {
        beside.resize(size_t(length));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(beside.data(), 1, beside.size(), f) == beside.size()) {
          base = beside.data();
          size = beside.size();
        }
      }
      std::fclose(f);
    }
    if (!base && &g_deko3dGlslPackageSize != nullptr && g_deko3dGlslPackageSize >= sizeof(PackageHeader)) {
      base = g_deko3dGlslPackage;
      size = g_deko3dGlslPackageSize;
    }
    if (!base) {
      // renut-ultimate (port 62, 2026-11-10): a release NRO (BANJO_RENUT_RELEASE_SHADERS) with the web installer's
      // package beside it (version 2, read by OpenInstalled) has every game shader there: no version 1 package is
      // then expected. Only a build with neither has no game shaders.
      if (InstalledPackageOk())
        REXLOG_INFO("[deko3d] no version 1 GLSL package: the installed package (version 2) has the game shaders");
      else
        REXLOG_ERROR("[deko3d] no GLSL package (deko3d_glsl.bin beside the NRO, or built in): no game shaders");
      return;
    }
    p.header = reinterpret_cast<const PackageHeader *>(base);
    if (std::memcmp(p.header->magic, "BDKG", 4) != 0 || p.header->version != 1) {
      REXLOG_ERROR("[deko3d] the GLSL package is not one this build reads");
      return;
    }
    p.entries = reinterpret_cast<const PackageEntry *>(base + p.header->tableOffset);
    p.data = base + p.header->dataOffset;
    // banjo-nx (2026-10-22): no dictionary in a distribution build's package (deko3d_shader_package.py --raw): its
    // frames are raw blocks, which the installer's page reads out of the NRO.
    p.dict = p.header->dictSize ? ZSTD_createDDict(base + sizeof(PackageHeader), p.header->dictSize) : nullptr;
    p.dctx = ZSTD_createDCtx();
    p.ok = (p.dict || !p.header->dictSize) && p.dctx;
    REXLOG_INFO("[deko3d] GLSL package: {} shaders, {} KB", p.header->count, size >> 10);
  });
  return g_package;
}

const PackageEntry *FindIn(const PackageEntry *begin, const PackageEntry *end, uint64_t hash) {
  const PackageEntry *it = std::lower_bound(begin, end, hash, [](const PackageEntry &e, uint64_t h) { return e.hash < h; });
  return (it != end && it->hash == hash) ? it : nullptr;
}

const PackageEntry *Find(Package &p, uint64_t hash) {
  if (!p.ok)
    return nullptr;
  return FindIn(p.entries, p.entries + p.header->count, hash);
}

// ---- banjo-nx (2026-10-22): the installer's package (banjo-nx-installer, its lib/package.js), deko3d_glsl.bin next
// to the NRO. A distribution NRO carries no game shaders but the few the game makes at runtime: the installer makes
// the rest from the player's game files. Version 2: version 1 without a dictionary, each shader's GLSL one zstd frame
// (raw blocks), plus the numbers of the shader table (shader_cache.h) for the shaders it holds:
//   header: "BDKG", 2, count, 0, table offset, data offset, info offset, info size
//   info: tag (16 bytes, kInstalledTag), u32 vertex input count, u32 literal count, then per table entry (same order)
//         24 u32: specConstantsMask, vertexFetchMemoryMask, textureFetchMask, memExportConstants[8],
//         floatConstants[8], vertexInputOffset, vertexInputCount, literalOffset, literalCount, shaderFlags; then the
//         vertex inputs (ShaderVertexInput, 8 bytes each), then the literals (ShaderLiteral, 20 bytes each)
// The table and the numbers are read at start; a shader's GLSL from the card when the shader is made (the GLSL of every
// shader is 175 MB).
// banjo-nx (2026-10-23, the console's stutters: the render thread waited on this file's fseek / fread for each new
// shader, and on the pipeline prebuild's reads under the same lock): version 2 with a dictionary (dictionary size not 0,
// the dictionary at offset 32; tools/deko3d_glsl_compress.py makes it from the installer's package, about 13 MB) is
// read whole into memory at start, and a shader's GLSL is unpacked from there: no card access while the game runs.
// The translator and GLSL chain the installer must have run for this NRO: a change to either changes the tag (and the
// installer's release with it), and a package with another tag is not read.
// renut-nx 1.0.3 (2026-11-23): the ultimate base's translator and GLSL chain (renut7 was 1.0.0-1.0.2's).
constexpr char kInstalledTag[16] = "renut8-ultglsl1";

struct InstalledInfo {
  uint32_t specConstantsMask, vertexFetchMemoryMask, textureFetchMask;
  uint32_t memExportConstants[8], floatConstants[8];
  uint32_t vertexInputOffset, vertexInputCount, literalOffset, literalCount, shaderFlags;
};
static_assert(sizeof(InstalledInfo) == 96, "banjo-nx-installer's lib/package.js");
static_assert(sizeof(ShaderVertexInput) == 8 && sizeof(ShaderLiteral) == 20, "banjo-nx-installer's lib/package.js");

struct InstalledPackage {
  std::once_flag once;
  bool ok = false;
  FILE *file = nullptr;
  uint32_t data_offset = 0;
  std::vector<PackageEntry> entries;
  std::vector<InstalledInfo> info;
  std::vector<ShaderVertexInput> inputs;
  std::vector<ShaderLiteral> literals;
  std::mutex mutex;  // the file and the decompression context
  ZSTD_DCtx *dctx = nullptr;
  // banjo-nx (2026-10-23): a package with a dictionary, held in memory (its data section and the dictionary).
  std::vector<uint8_t> data;
  ZSTD_DDict *ddict = nullptr;
};
InstalledPackage g_installed;

template <typename T>
bool ReadAt(FILE *f, long at, std::vector<T> &out, size_t count) {
  out.resize(count);
  return std::fseek(f, at, SEEK_SET) == 0 && (count == 0 || std::fread(out.data(), sizeof(T), count, f) == count);
}

// renut-nx (2026-11-01, the public build, the user's idea): the web installer writes deko3d_glsl.bin with raw frames and
// no dictionary (the page has no zstd compressor), so each shader would be read from the card when it is made and its
// text kept. With the dictionary the installer puts beside it (deko3d_glsl.dict: the native package's, trained on the
// shaders' GLSL), the package is compressed here once, at the first start (zstd level 3: 161 MB of GLSL to about 23 MB),
// and written over the file; from then on OpenInstalled holds it in memory, as the native package. The texts are the
// same bytes, so every variant key and the DKSH cache stay valid. Any failure leaves the file as it was.
void CompressInstalledPackage(const std::string &path) {
  const std::string tmp_path = path + ".tmp";
  // A conversion stopped between removing the old file and renaming the new one: the new one is whole.
  if (FILE *done = std::fopen(tmp_path.c_str(), "rb")) {
    std::fclose(done);
    if (FILE *old = std::fopen(path.c_str(), "rb")) {
      std::fclose(old);
      std::remove(tmp_path.c_str());  // a write that did not finish
    } else {
      std::rename(tmp_path.c_str(), path.c_str());
    }
  }
  std::vector<uint8_t> dict;
  {
    FILE *d = std::fopen(NextToExecutable("deko3d_glsl.dict").string().c_str(), "rb");
    if (!d)
      return;
    std::fseek(d, 0, SEEK_END);
    const long n = std::ftell(d);
    const bool ok = n > 8 && n <= long(4u << 20) && ReadAt(d, 0, dict, size_t(n));
    std::fclose(d);
    if (!ok)
      return;
  }
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return;
  PackageHeader h{};
  std::fseek(f, 0, SEEK_END);
  const long length = std::ftell(f);
  std::vector<uint8_t> in;
  const bool raw = length > long(sizeof(h)) && std::fseek(f, 0, SEEK_SET) == 0 && std::fread(&h, sizeof(h), 1, f) == 1 &&
                   std::memcmp(h.magic, "BDKG", 4) == 0 && h.version == 2 && h.dictSize == 0;
  const bool read = raw && ReadAt(f, 0, in, size_t(length));
  std::fclose(f);
  if (!read)
    return;  // already compressed, or not a package of ours
  const uint64_t info_offset = h.reserved[0], info_size = h.reserved[1];
  if (uint64_t(h.tableOffset) + 56ull * h.count > in.size() || h.dataOffset > in.size() ||
      info_offset + info_size > in.size())
    return;
  std::vector<PackageEntry> entries(h.count);
  std::memcpy(entries.data(), in.data() + h.tableOffset, 56ull * h.count);
  const auto start = std::chrono::steady_clock::now();
  ZSTD_CDict *cdict = ZSTD_createCDict(dict.data(), dict.size(), 3);
  ZSTD_CCtx *cctx = ZSTD_createCCtx();
  ZSTD_DCtx *dctx = ZSTD_createDCtx();
  std::vector<uint8_t> data, text;
  bool ok = cdict && cctx && dctx;
  for (PackageEntry &e : entries) {
    if (!ok)
      break;
    if (uint64_t(h.dataOffset) + e.offset + e.compressedSize > in.size() || e.size > (16u << 20)) {
      ok = false;
      break;
    }
    text.resize(e.size);
    const size_t n = ZSTD_decompressDCtx(dctx, text.data(), e.size, in.data() + h.dataOffset + e.offset, e.compressedSize);
    if (ZSTD_isError(n) || n != e.size) {
      ok = false;
      break;
    }
    const size_t at = data.size(), bound = ZSTD_compressBound(e.size);
    data.resize(at + bound);
    const size_t c = ZSTD_compress_usingCDict(cctx, data.data() + at, bound, text.data(), e.size, cdict);
    if (ZSTD_isError(c)) {
      ok = false;
      break;
    }
    data.resize((at + c + 7) & ~size_t(7));
    e.offset = uint32_t(at);
    e.compressedSize = uint32_t(c);
  }
  ZSTD_freeCDict(cdict);
  ZSTD_freeCCtx(cctx);
  ZSTD_freeDCtx(dctx);
  if (!ok) {
    REXLOG_ERROR("[deko3d] installed GLSL package {}: could not be compressed with deko3d_glsl.dict; kept as it is", path);
    return;
  }
  // header | dictionary | table | info | data, each part 8-byte aligned
  const auto align8 = [](uint64_t v) { return (v + 7) & ~uint64_t(7); };
  PackageHeader out = h;
  out.dictSize = uint32_t(dict.size());
  out.tableOffset = uint32_t(align8(32 + dict.size()));
  out.reserved[0] = uint32_t(out.tableOffset + 56ull * h.count);
  out.reserved[1] = uint32_t(info_size);
  out.dataOffset = uint32_t(align8(out.reserved[0] + info_size));
  std::vector<uint8_t> file(out.dataOffset, 0);
  std::memcpy(file.data(), &out, sizeof(out));
  std::memcpy(file.data() + 32, dict.data(), dict.size());
  std::memcpy(file.data() + out.tableOffset, entries.data(), 56ull * h.count);
  std::memcpy(file.data() + out.reserved[0], in.data() + info_offset, info_size);
  in.clear();
  in.shrink_to_fit();
  FILE *w = std::fopen(tmp_path.c_str(), "wb");
  bool written = w && std::fwrite(file.data(), 1, file.size(), w) == file.size() &&
                 std::fwrite(data.data(), 1, data.size(), w) == data.size();
  if (w)
    written = std::fclose(w) == 0 && written;
  if (!written) {
    std::remove(tmp_path.c_str());
    REXLOG_ERROR("[deko3d] installed GLSL package {}: the compressed copy could not be written; kept as it is", path);
    return;
  }
  std::remove(path.c_str());
  std::rename(tmp_path.c_str(), path.c_str());
  REXLOG_INFO("[deko3d] installed GLSL package {}: compressed with deko3d_glsl.dict once ({} MB of shader data, {:.1f} s)",
              path, (file.size() + data.size()) >> 20,
              std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
}

InstalledPackage &OpenInstalled() {
  std::call_once(g_installed.once, [] {
    InstalledPackage &p = g_installed;
    const std::string path = NextToExecutable("deko3d_glsl.bin").string();
    CompressInstalledPackage(path);  // renut-nx (2026-11-01): the web installer's raw package, once
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
      REXLOG_INFO("[deko3d] no installed GLSL package ({}): the NRO's own shaders only", path);
      return;
    }
    std::fseek(f, 0, SEEK_END);
    const long length = std::ftell(f);
    PackageHeader h{};
    char tag[16] = {};
    uint32_t counts[2] = {};
    const auto fail = [&](const char *why) {
      REXLOG_ERROR("[deko3d] installed GLSL package {}: {}; not used (run the installer of this release again)", path, why);
      std::fclose(f);
      p.entries.clear();
      p.info.clear();
      p.inputs.clear();
      p.literals.clear();
    };
    if (std::fseek(f, 0, SEEK_SET) != 0 || std::fread(&h, sizeof(h), 1, f) != 1 || std::memcmp(h.magic, "BDKG", 4) != 0)
      return fail("not a package");
    if (h.version == 1) {  // katamari-nx (2026-11-02): the shaders alone, the table in the NRO: Open() reads it
      std::fclose(f);
      return;
    }
    if (h.version != 2)
      return fail("not a version 2 package");
    if (h.dictSize && (32ull + h.dictSize > h.tableOffset || h.dictSize > (4u << 20)))
      return fail("its dictionary is not sound");
    const uint64_t info_offset = h.reserved[0], info_size = h.reserved[1];
    if (uint64_t(h.tableOffset) + 56ull * h.count > uint64_t(length) || h.dataOffset > uint64_t(length) ||
        info_offset + info_size > uint64_t(length) || info_size < 24 + 96ull * h.count)
      return fail("cut short");
    if (std::fseek(f, long(info_offset), SEEK_SET) != 0 || std::fread(tag, 1, sizeof(tag), f) != sizeof(tag) ||
        std::fread(counts, sizeof(uint32_t), 2, f) != 2)
      return fail("unreadable");
    if (std::memcmp(tag, kInstalledTag, sizeof(tag)) != 0)
      return fail(fmt::format("made for another build (\"{}\", this NRO reads \"{}\")",
                              std::string_view(tag, strnlen(tag, sizeof(tag))), kInstalledTag).c_str());
    if (24 + 96ull * h.count + 8ull * counts[0] + 20ull * counts[1] != info_size)
      return fail("its sizes disagree");
    const long info_at = long(info_offset) + 24;
    if (!ReadAt(f, long(h.tableOffset), p.entries, h.count) || !ReadAt(f, info_at, p.info, h.count) ||
        !ReadAt(f, info_at + long(96 * h.count), p.inputs, counts[0]) ||
        !ReadAt(f, info_at + long(96 * h.count + 8ull * counts[0]), p.literals, counts[1]))
      return fail("unreadable");
    for (size_t i = 0; i < p.entries.size(); ++i) {
      const PackageEntry &e = p.entries[i];
      const InstalledInfo &n = p.info[i];
      if ((i && p.entries[i - 1].hash >= e.hash) || uint64_t(h.dataOffset) + e.offset + e.compressedSize > uint64_t(length) ||
          e.size > (16u << 20) || uint64_t(n.vertexInputOffset) + n.vertexInputCount > counts[0] ||
          uint64_t(n.literalOffset) + n.literalCount > counts[1])
        return fail(fmt::format("entry {} is not sound", i).c_str());
    }
    p.data_offset = h.dataOffset;
    p.dctx = ZSTD_createDCtx();
    if (h.dictSize) {
      // banjo-nx (2026-10-23): the dictionary and the whole data section into memory; the file is closed.
      std::vector<uint8_t> dictionary;
      if (!ReadAt(f, 32, dictionary, h.dictSize) || !ReadAt(f, long(h.dataOffset), p.data, size_t(length - h.dataOffset)))
        return fail("unreadable");
      p.ddict = ZSTD_createDDict(dictionary.data(), dictionary.size());
      std::fclose(f);
      p.ok = p.dctx != nullptr && p.ddict != nullptr;
      REXLOG_INFO("[deko3d] installed GLSL package {}: {} shaders, {} MB in memory (compressed with a dictionary: no "
                  "card reads while the game runs)", path, h.count, p.data.size() >> 20);
      return;
    }
    p.file = f;
    p.ok = p.dctx != nullptr;
    REXLOG_INFO("[deko3d] installed GLSL package {}: {} shaders, {} MB (raw frames: each shader read from the card when "
                "it is made; tools/deko3d_glsl_compress.py makes the in-memory form)", path, h.count, length >> 20);
  });
  return g_installed;
}

bool InstalledPackageOk() { return OpenInstalled().ok; }

const PackageEntry *Find(InstalledPackage &p, uint64_t hash) {
  if (!p.ok)
    return nullptr;
  return FindIn(p.entries.data(), p.entries.data() + p.entries.size(), hash);
}

// ---- the DKSH cache next to the NRO: "BDKC", version, the compiler's tag, then records (u64 key, u32 size, DKSH)
// uam 1.1.0 with SwitchWakerHD's 8 patches and banjo-nx's passes (bnx3, 2026-10-18: with the value numbering).
// banjo-nx (2026-10-20): bnx4 with uam banjo flag 1024 (the 128-bit bone rows) in the default flags: a cache
// compiled before is another compiler's.
constexpr char kCacheTag[16] = "uam110-sw8-bnx4";

constexpr uint32_t kDkshMagic = 0x48534B44u;  // "DKSH", the first word of every record's data

struct DkshCache {
  std::mutex mutex;
  FILE *file = nullptr;
  std::unordered_map<uint64_t, std::pair<long, uint32_t>> index;  // key -> offset of the DKSH, size
  long end = 0;  // the end of the last whole record: where the next one is written
  bool read_only = false;  // a write failed: no more records
  uint64_t added = 0, hits = 0, misses = 0;
  std::string path;
};
DkshCache g_cache;

// renut-nx (2026-11-22, the user: the slow boot): the cache's start was 101-105 s on the console at every start since
// at least c40 (195,826 variants). The walk sought to each record and read its 16-byte head: a seek drops stdio's
// buffer, so each record was a small read at a new place on the SD card. Now (1) NAME.idx beside the cache lists its
// records' keys, offsets and sizes (16 bytes each, ~3 MB), read in one go; it holds the cache's length when written
// and the first and last records' heads, checked against the cache; records appended after it (a run's compiles) are
// walked from its end, and a cache it does not match is walked whole. (2) The walk reads the cache in 4 MB pieces and
// finds the heads inside them. The index is written again (NAME.idx.new, then renamed) when the walk found records.
// Credit: the cache format and the walk are banjo-nx's (2026-10-18, its record cut of 2026-10-20).
struct CacheIndexHead {
  char magic[4];     // "BDKI"
  uint32_t version;  // 1
  char tag[16];      // kCacheTag
  uint64_t length;   // the cache's end of whole records when the index was written
  uint64_t count;    // records listed
  uint64_t first_key, last_key;
  uint64_t first_at, last_at;  // the first and the last listed record's DKSH offsets
  uint32_t first_size, last_size;
};
struct CacheIndexRecord {
  uint64_t key;
  uint64_t at_size;  // the DKSH's offset (low 40 bits) and its size (high 24 bits)
};
static_assert(sizeof(CacheIndexRecord) == 16, "16-byte index records");
constexpr uint64_t kIndexAtMask = 0xFFFFFFFFFFull;

// One record head (key, size, the DKSH magic) at `at`: false when there is not one.
bool ReadCacheHead(FILE *f, long at, long length, uint64_t &key, uint32_t &size) {
  uint32_t magic = 0;
  return at >= 0 && at + 16 <= length && std::fseek(f, at, SEEK_SET) == 0 && std::fread(&key, 8, 1, f) == 1 &&
         std::fread(&size, 4, 1, f) == 1 && std::fread(&magic, 4, 1, f) == 1 && size >= 64 && size <= (16u << 20) &&
         at + 12 + long(size) <= length && magic == kDkshMagic;
}

// The index, when it matches the cache: c.index filled, the end of its records returned; else -1 (nothing kept).
long ReadCacheIndex(DkshCache &c, long length) {
  FILE *f = std::fopen((c.path + ".idx").c_str(), "rb");
  if (!f)
    return -1;
  CacheIndexHead h{};
  bool ok = std::fread(&h, sizeof(h), 1, f) == 1 && std::memcmp(h.magic, "BDKI", 4) == 0 && h.version == 1 &&
            std::memcmp(h.tag, kCacheTag, sizeof(kCacheTag)) == 0 && long(h.length) <= length && h.count > 0 &&
            h.count < (16u << 20);
  std::vector<CacheIndexRecord> records;
  if (ok) {
    records.resize(size_t(h.count));
    ok = std::fread(records.data(), sizeof(CacheIndexRecord), records.size(), f) == records.size();
  }
  std::fclose(f);
  uint64_t key = 0;
  uint32_t size = 0;
  if (!ok || !ReadCacheHead(c.file, long(h.first_at) - 12, length, key, size) || key != h.first_key ||
      size != h.first_size || !ReadCacheHead(c.file, long(h.last_at) - 12, length, key, size) || key != h.last_key ||
      size != h.last_size || long(h.last_at) + long(size) != long(h.length))
    return -1;
  c.index.reserve(records.size() + 4096);
  for (const CacheIndexRecord &r : records)
    c.index[r.key] = {long(r.at_size & kIndexAtMask), uint32_t(r.at_size >> 40)};
  return long(h.length);
}

// The cache's records from `at` on, read in 4 MB pieces: the end of the last whole record.
long WalkCacheRecords(DkshCache &c, long at, long length) {
  constexpr size_t kPiece = 4u << 20;
  std::vector<uint8_t> buf(kPiece);
  long buf_at = -1;  // the file offset of buf[0]
  size_t buf_len = 0;
  while (at + 16 <= length) {
    if (buf_at < 0 || at < buf_at || at + 16 > buf_at + long(buf_len)) {
      if (std::fseek(c.file, at, SEEK_SET) != 0)
        break;
      buf_at = at;
      buf_len = std::fread(buf.data(), 1, std::min<size_t>(kPiece, size_t(length - at)), c.file);
      if (buf_len < 16)
        break;
    }
    const uint8_t *p = buf.data() + (at - buf_at);
    uint64_t key;
    uint32_t size, magic;
    std::memcpy(&key, p, 8);
    std::memcpy(&size, p + 8, 4);
    std::memcpy(&magic, p + 12, 4);
    if (size < 64 || size > (16u << 20) || at + 12 + long(size) > length || magic != kDkshMagic)
      break;
    c.index[key] = {at + 12, size};
    at += 12 + long(size);
  }
  return at;
}

void WriteCacheIndex(const DkshCache &c) {
  if (c.index.empty())
    return;
  std::vector<CacheIndexRecord> records;
  records.reserve(c.index.size());
  for (const auto &[key, v] : c.index)
    records.push_back({key, uint64_t(v.first) | (uint64_t(v.second) << 40)});
  std::sort(records.begin(), records.end(), [](const CacheIndexRecord &a, const CacheIndexRecord &b) {
    return (a.at_size & kIndexAtMask) < (b.at_size & kIndexAtMask);
  });
  CacheIndexHead h{};
  std::memcpy(h.magic, "BDKI", 4);
  h.version = 1;
  std::memcpy(h.tag, kCacheTag, sizeof(kCacheTag));
  h.length = uint64_t(c.end);
  h.count = records.size();
  h.first_key = records.front().key;
  h.first_at = records.front().at_size & kIndexAtMask;
  h.first_size = uint32_t(records.front().at_size >> 40);
  h.last_key = records.back().key;
  h.last_at = records.back().at_size & kIndexAtMask;
  h.last_size = uint32_t(records.back().at_size >> 40);
  const std::string path = c.path + ".idx", tmp = path + ".new";
  FILE *f = std::fopen(tmp.c_str(), "wb");
  if (!f)
    return;
  const bool ok = std::fwrite(&h, sizeof(h), 1, f) == 1 &&
                  std::fwrite(records.data(), sizeof(CacheIndexRecord), records.size(), f) == records.size();
  if (std::fclose(f) == 0 && ok) {
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) == 0)
      return;
  }
  std::remove(tmp.c_str());
}

void LoadCache() {
  DkshCache &c = g_cache;
  // banjo-nx (2026-10-18): the code uam makes with other flags (banjo_deko3d_uam_prmt, banjo_deko3d_uam_gvn off) has a
  // cache of its own.
  const unsigned flags = Deko3DUamFlags();
  c.path = NextToExecutable(flags == 1471u ? std::string("deko3d_shaders.bin")
                                          : "deko3d_shaders_f" + std::to_string(flags) + ".bin").string();
  c.file = std::fopen(c.path.c_str(), "r+b");
  bool fresh = c.file == nullptr;
  if (c.file) {
    char head[24] = {};
    if (std::fread(head, 1, sizeof(head), c.file) != sizeof(head) || std::memcmp(head, "BDKC", 4) != 0 ||
        std::memcmp(head + 8, kCacheTag, sizeof(kCacheTag)) != 0) {
      REXLOG_INFO("[deko3d] DKSH cache {}: another compiler's or unreadable; starting it over", c.path);
      std::fclose(c.file);
      c.file = nullptr;
      fresh = true;
    }
    else {
      std::fseek(c.file, 0, SEEK_END);
      const long length = std::ftell(c.file);
      // Records up to the first that is not whole: its size runs past the end, or its data is not a DKSH (an append
      // the process did not finish). The file is cut there and new records follow the good ones. Before, appends
      // went after such a record, where the next start's walk never found them: Ryujinx runs 20-22 compiled the same
      // ~30 shaders on every start.
      // renut-nx (2026-11-22): the index's records, then the walk from where they end (WalkCacheRecords: 4 MB pieces).
      const auto t0 = std::chrono::steady_clock::now();
      const bool use_index = REXCVAR_GET(banjo_deko3d_cache_index);
      const long indexed = use_index ? ReadCacheIndex(c, length) : -1;
      const size_t from_index = c.index.size();
      long at = indexed > 0 ? indexed : long(sizeof(head));
      at = WalkCacheRecords(c, at, length);
      c.end = at;
      const bool walked = at != (indexed > 0 ? indexed : long(sizeof(head)));
      if (at != length) {
        REXLOG_WARN("[deko3d] DKSH cache {}: {} bytes after the last whole record ({} of them) dropped", c.path,
                    length - at, c.index.size());
        std::fflush(c.file);
        if (ftruncate(fileno(c.file), at) != 0)
          REXLOG_WARN("[deko3d] DKSH cache {}: could not be cut; new records overwrite the rest", c.path);
      }
      if (use_index && (walked || indexed < 0))
        WriteCacheIndex(c);
      REXLOG_INFO("[deko3d] DKSH cache {}: read in {} ms ({} records from its index{}, {} walked{})", c.path,
                  std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                  from_index, use_index ? (indexed < 0 ? ": none or not this cache's" : "") : ": off",
                  c.index.size() - from_index, use_index && (walked || indexed < 0) ? "; the index written" : "");
    }
  }
  if (fresh) {
    c.file = std::fopen(c.path.c_str(), "w+b");
    if (c.file) {
      char head[24] = {'B', 'D', 'K', 'C', 1, 0, 0, 0};
      std::memcpy(head + 8, kCacheTag, sizeof(kCacheTag));
      std::fwrite(head, 1, sizeof(head), c.file);
      std::fflush(c.file);
      c.end = sizeof(head);
    }
  }
  REXLOG_INFO("[deko3d] DKSH cache {}: {} variants{}", c.path, c.index.size(), c.file ? "" : " (cannot write it)");
}

}  // namespace

bool PackageAvailable() { return OpenInstalled().ok || Open().ok; }

bool InstalledShaderTable(std::vector<ShaderCacheEntry> &entries, std::vector<ShaderVertexInput> &inputs,
                          std::vector<ShaderLiteral> &literals) {
  InstalledPackage &p = OpenInstalled();
  if (!p.ok)
    return false;
  entries.resize(p.entries.size());
  for (size_t i = 0; i < p.entries.size(); ++i) {
    const InstalledInfo &n = p.info[i];
    ShaderCacheEntry &e = entries[i];
    e = {};
    e.hash = p.entries[i].hash;
    e.specConstantsMask = n.specConstantsMask;
    e.vertexFetchMemoryMask = n.vertexFetchMemoryMask;
    e.textureFetchMask = n.textureFetchMask;
    std::memcpy(e.memExportConstants, n.memExportConstants, sizeof(e.memExportConstants));
    std::memcpy(e.floatConstants, n.floatConstants, sizeof(e.floatConstants));
    e.vertexInputOffset = n.vertexInputOffset;
    e.vertexInputCount = n.vertexInputCount;
    e.literalOffset = n.literalOffset;
    e.literalCount = n.literalCount;
    e.shaderFlags = n.shaderFlags;
  }
  inputs = p.inputs;
  literals = p.literals;
  return true;
}

#if defined(PLUME_DEKO3D)

// banjo-nx (2026-11-01): a shader's GLSL from the package that has it, the installer's first (empty: in neither, or it did
// not unpack). in_memory: that package is in memory (unpacking it again later reads no card).
static std::string UnpackGlsl(uint64_t hash, uint32_t &stage, bool &in_memory) {
  InstalledPackage &f = OpenInstalled();
  Package &p = Open();
  const PackageEntry *installed = Find(f, hash);
  const PackageEntry *e = installed ? installed : Find(p, hash);
  in_memory = false;
  if (!e) {
    static int logged = 0;
    if ((f.ok || p.ok) && logged++ < 16)
      REXLOG_ERROR("[deko3d] shader {:016X} is in no GLSL package", hash);
    return {};
  }
  stage = e->stage;
  std::string text(e->size, '\0');
  if (installed && f.ddict) {
    // banjo-nx (2026-10-23): from memory (a package with a dictionary).
    std::lock_guard<std::mutex> lock(f.mutex);
    const size_t n = ZSTD_decompress_usingDDict(f.dctx, text.data(), e->size, f.data.data() + e->offset,
                                                e->compressedSize, f.ddict);
    if (ZSTD_isError(n) || n != e->size) {
      REXLOG_ERROR("[deko3d] shader {:016X}: the installed GLSL did not unpack ({})", hash,
                   ZSTD_isError(n) ? ZSTD_getErrorName(n) : "short");
      return {};
    }
    in_memory = true;
  } else if (installed) {
    std::lock_guard<std::mutex> lock(f.mutex);
    std::vector<uint8_t> frame(e->compressedSize);
    const bool read = std::fseek(f.file, long(f.data_offset + e->offset), SEEK_SET) == 0 &&
                      std::fread(frame.data(), 1, frame.size(), f.file) == frame.size();
    const size_t n = read ? ZSTD_decompressDCtx(f.dctx, text.data(), e->size, frame.data(), frame.size()) : 0;
    if (!read || ZSTD_isError(n) || n != e->size) {
      REXLOG_ERROR("[deko3d] shader {:016X}: the installed GLSL did not read ({})", hash,
                   !read ? "card" : ZSTD_isError(n) ? ZSTD_getErrorName(n) : "short");
      return {};
    }
  } else {
    std::lock_guard<std::mutex> lock(p.mutex);
    const size_t n = p.dict ? ZSTD_decompress_usingDDict(p.dctx, text.data(), e->size, p.data + e->offset,
                                                         e->compressedSize, p.dict)
                            : ZSTD_decompressDCtx(p.dctx, text.data(), e->size, p.data + e->offset, e->compressedSize);
    if (ZSTD_isError(n) || n != e->size) {
      REXLOG_ERROR("[deko3d] shader {:016X}: the GLSL did not unpack ({})", hash,
                   ZSTD_isError(n) ? ZSTD_getErrorName(n) : "short");
      return {};
    }
    in_memory = true;
  }
  return text;
}

std::unique_ptr<plume::RenderShader> CreateGameShader(plume::RenderDevice *device, uint64_t hash) {
  // The installer's package first (made from the player's files), then the NRO's own.
  if (!device)
    return nullptr;
  uint32_t stage = 0;
  bool in_memory = false;
  const std::string glsl = UnpackGlsl(hash, stage, in_memory);
  if (glsl.empty())
    return nullptr;
  std::vector<uint8_t> blob(sizeof(plume::Deko3DShaderBlob) + glsl.size());
  plume::Deko3DShaderBlob header;
  header.stage = stage;
  header.glslSize = uint32_t(glsl.size());
  header.dkshSize = 0;
  std::memcpy(blob.data(), &header, sizeof(header));
  std::memcpy(blob.data() + sizeof(header), glsl.data(), glsl.size());
  auto shader = device->createShader(blob.data(), blob.size(), "main", plume::RenderShaderFormat::DEKO3D);
  if (shader) {
    shader->setName(fmt::format("{}_{:016X}", stage == 4 ? "ps" : "vs", hash));
    // banjo-nx (2026-11-01, the console's out-of-memory crash): the text is not kept with the shader; plume unpacks it
    // again when a new variant needs it (plume::Deko3DGlslSource). A package read from the card keeps it, as before.
    if (in_memory) {
      plume::Deko3DShaderSetGlslLoader(shader.get(), [hash] {
        uint32_t st = 0;
        bool mem = false;
        return UnpackGlsl(hash, st, mem);
      });
    }
  }
  return shader;
}

// Port 161 (from fnxf_xiii, its note of 2026-11-05; banjo_deko3d_log_writer): deko3d.log's writer. The back end's
// progress lines (its memory blocks, the compile threads, the swapchain, the first presents) are queued and written and
// flushed by a thread of their own: each line was a write and a flush of the card on the thread that said it, the
// render thread's for the "memory block" lines of an area's new buffers (fnxf_xiii: 16 ms of such writes in one
// frame). Any other line (a failure, a guard, a fence that does not signal) is written and flushed at once on its own
// thread, after the queued ones, so a crash right after it still has it in the file.
class DiagLogWriter {
 public:
  static DiagLogWriter &Instance() {
    static DiagLogWriter w;
    return w;
  }
  void Open(FILE *file) { file_ = file; }
  void Queue(const char *line) {
    bool start = false;
    {
      std::lock_guard lock(queue_mutex_);
      queue_.emplace_back(line);
      start = !started_;
      started_ = true;
    }
    if (start)
      banjo::renut_gpu::StartLongLivedThread([] { Instance().Loop(); }, 32 * 1024);
    cv_.notify_one();
  }
  void WriteNow(const char *line) {
    std::vector<std::string> lines;
    {
      std::lock_guard lock(queue_mutex_);
      lines.swap(queue_);
    }
    std::lock_guard lock(file_mutex_);
    if (!file_)
      return;
    for (const std::string &l : lines)
      std::fprintf(file_, "%s\n", l.c_str());
    std::fprintf(file_, "%s\n", line);
    std::fflush(file_);
  }

 private:
  void Loop() {
    for (;;) {
      std::vector<std::string> lines;
      {
        std::unique_lock lock(queue_mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        lines.swap(queue_);
      }
      std::lock_guard lock(file_mutex_);
      if (!file_)
        continue;
      for (const std::string &l : lines)
        std::fprintf(file_, "%s\n", l.c_str());
      std::fflush(file_);
    }
  }
  std::mutex queue_mutex_, file_mutex_;
  std::condition_variable cv_;
  std::vector<std::string> queue_;
  bool started_ = false;
  FILE *file_ = nullptr;
};

bool Deko3DProgressLine(const char *message) {
  static constexpr const char *kProgress[] = {
      "device: creating", "image heap:", "buffer arena:", "device ready:", "memory block:", "blocks:", "uam thread:",
      "uam: compile", "uam: run-time variant", "uam background thread:", "uam queue:", "shader code: block",
      "trace ", "self-test", "swapchain:", "present ", "nvservices: a GPU submission",
  };
  for (const char *prefix : kProgress)
    if (std::strncmp(message, prefix, std::strlen(prefix)) == 0)
      return true;
  return false;
}

void InstallShaderCache() {
  // The back end's messages into this log (plume writes them to stderr otherwise), and into deko3d.log next to the NRO,
  // flushed at each line: the log's buffer is lost when the process dies. katamari-nx (2026-11-02): not with
  // log_level "off" (the user's PGO build: only the crash logs).
  static FILE *diag = REXCVAR_GET(log_level) == "off"
                          ? nullptr
                          : std::fopen(NextToExecutable("deko3d.log").string().c_str(), "w");
  static std::mutex diag_mutex;
  static const bool writer = REXCVAR_GET(banjo_deko3d_log_writer);  // port 161
  DiagLogWriter::Instance().Open(diag);
  plume::SetDeko3DLogCallback([](const char *message) {
    REXLOG_INFO("[deko3d] {}", message);
    if (writer) {  // port 161: see DiagLogWriter
      if (Deko3DProgressLine(message))
        DiagLogWriter::Instance().Queue(message);
      else
        DiagLogWriter::Instance().WriteNow(message);
      return;
    }
    std::lock_guard<std::mutex> lock(diag_mutex);
    if (diag) {
      std::fprintf(diag, "%s\n", message);
      std::fflush(diag);
    }
  });
  // banjo-nx (2026-10-18, GPU bandwidth): depth compression and Zcull, which the device and its textures take.
  plume::Deko3DOptions options;
  options.depthCompression = Deko3DDepthCompressionOn();
  options.zcull = Deko3DZcullOn();
  options.msaaPositions = Deko3DMsaaPositionsOn();  // 2026-10-30, from sasrt-nx
  options.alphaToCoverageDither = Deko3DA2cDitherOn();
  options.stripRestart = Deko3DStripRestartOn();
  options.vsOutputs = Deko3DVsOutputsOn();
  options.uamFlags = Deko3DUamFlags();
  options.logMisses = REXCVAR_GET(banjo_deko3d_log_misses);  // banjo-nx (2026-11-20)
  options.mergeBarriers = Deko3DBarrierMergeOn();  // banjo-nx (2026-10-19, GPU)
  options.leanBarriers = Deko3DBarrierLeanOn();
  options.copyRunBarriers = REXCVAR_GET(banjo_deko3d_copy_run_barriers);  // port 161 (from fnxf_xiii)
  options.swapInterval = uint32_t(std::clamp<int32_t>(REXCVAR_GET(banjo_deko3d_swap_interval), 1, 4));  // port 165
  options.asyncVariants = Deko3DAsyncVariantsOn();  // banjo-nx (2026-10-21)
  // The uam thread on the background compile core, below the game's threads (2026-10-18).
  options.compileCore = Deko3DCompileCore();
  options.placeCompileThread = Deko3DCompileThreadPlacer();  // sasrt-nx (2026-11-22): sasrt_compile_threads_core -1
  options.compilePriority = Deko3DCompilePriority();
  options.backgroundCompileShare = Deko3DBackgroundCompileShare();  // 2026-10-23
  options.compileWaitBoost = Deko3DCompileWaitBoostOn();  // 2026-10-23
  options.fastFirstCompile = Deko3DFastFirstCompileOn();  // port 56 (mk9-nx)
  options.asyncCacheReads = Deko3DAsyncCacheReadsOn();     // 2026-10-23
  options.userClipEnable = Deko3DUserClipEnable();  // renut-ultimate, from rr6-nx (2026-11-05)
  options.depthResolve = Deko3DDepthResolveOn();  // sasrt-nx (2026-10-21)
  options.depthResolveLazy = Deko3DDepthResolveLazyOn();  // renut-ultimate (2026-11-08)
  options.diagBarriers = REXCVAR_GET(sasrt_deko3d_diag_barriers);  // sasrt-nx (2026-11-08)
  options.descriptorCache = REXCVAR_GET(lo_deko3d_descriptor_cache);                       // port 59
  options.relaxedDescriptorCounters = REXCVAR_GET(lo_deko3d_relaxed_descriptor_counters);  // port 59
  plume::Deko3DSetOptions(options);
  sasrt::gpu_round::ApplyPlume();  // sasrt-nx (2026-11-08): the GPU round's items in plume (preciseDepthResolve)
  // banjo-nx (2026-10-28, the console's OOM): a block that finds no room in the heap gets the spare command buffers'.
  plume::Deko3DSetMemoryPressureCallback(+[] { ::renut::gpu::ReleaseSpareBuffersNow(); });
  plume::Deko3DSetGpuRegion(Deko3DGpuRegionOn());  // banjo-nx (2026-10-28, h10)
  plume::Deko3DSetVertexBufferDedupe(Deko3DVtxDedupeOn());  // banjo-nx (2026-10-26, Unleashed-style step 2)
  plume::Deko3DSetTextureSkip(Deko3DTextureSkipOn());       // banjo-nx (2026-10-27, render thread night)
  plume::Deko3DSetProvisionalCount(Deko3DProvisionalCountOn());  // banjo-nx (2026-10-28, render-thread round)
  LoadCache();
  // banjo-nx (2026-10-23): the index only (asyncCacheReads).
  plume::SetDeko3DShaderCacheContains([](uint64_t key) {
    DkshCache &c = g_cache;
    std::lock_guard<std::mutex> lock(c.mutex);
    return c.file != nullptr && c.index.count(key) != 0;
  });
  plume::SetDeko3DShaderCache(
      [](uint64_t key, std::vector<uint8_t> &dksh) {
        DkshCache &c = g_cache;
        std::lock_guard<std::mutex> lock(c.mutex);
        auto it = c.index.find(key);
        if (it == c.index.end() || !c.file) {
          c.misses++;
          return false;
        }
        dksh.resize(it->second.second);
        uint32_t magic = 0;
        if (std::fseek(c.file, it->second.first, SEEK_SET) != 0 ||
            std::fread(dksh.data(), 1, dksh.size(), c.file) != dksh.size() ||
            (std::memcpy(&magic, dksh.data(), sizeof(magic)), magic != kDkshMagic)) {
          c.misses++;
          c.index.erase(it);
          return false;
        }
        c.hits++;
        return true;
      },
      [](uint64_t key, const std::vector<uint8_t> &dksh) {
        DkshCache &c = g_cache;
        std::lock_guard<std::mutex> lock(c.mutex);
        if (!c.file || c.read_only || c.index.count(key) || dksh.size() < 64)
          return;
        // One write of the whole record at the end of the last whole one, then flushed: a record cut short (the game
        // closed while writing) is the last one, and the next start drops it (LoadCache).
        const uint32_t size = uint32_t(dksh.size());
        std::vector<uint8_t> record(12 + dksh.size());
        std::memcpy(record.data(), &key, sizeof(key));
        std::memcpy(record.data() + 8, &size, sizeof(size));
        std::memcpy(record.data() + 12, dksh.data(), dksh.size());
        if (std::fseek(c.file, c.end, SEEK_SET) == 0 &&
            std::fwrite(record.data(), 1, record.size(), c.file) == record.size() && std::fflush(c.file) == 0) {
          c.index[key] = {c.end + 12, size};
          c.end += long(record.size());
          c.added++;
          // banjo-nx (2026-10-20, Peggle 2's report): fflush only hands the record to the file system service, which
          // may keep it buffered; a process closed with HOME (or stopped in the emulator) lost the last records and
          // compiled them again at every start. fsync writes it to the card (libnx: fsFileFlush), once per compile.
          fsync(fileno(c.file));
        }
        else {
          // The SD card full or failing: the records so far keep being read; no more are written (a part of this
          // one may be in the file, after c.end, and the next start drops it).
          REXLOG_WARN("[deko3d] DKSH cache {}: a record could not be written; no more are", c.path);
          c.read_only = true;
        }
      });
}

// sasrt-nx (2026-11-08, sasrt_deko3d_diag_trace): one frame's command stream every 600 frames.
void TraceFrameBoundary() {
  static const bool on = REXCVAR_GET(sasrt_deko3d_diag_trace);
  if (!on)
    return;
  static uint64_t frame = 0;
  ++frame;
  if (plume::g_deko3dTrace) {
    plume::g_deko3dTrace = false;
    const std::string text = plume::Deko3DTraceTake();
    REXLOG_INFO("[deko3d trace] frame {} ({} characters; B barrier P/F/X + causes r read c copy C colour d depth o other "
                "b buffer k compute m merged + i texture cache, D draw, X dispatch, T colour:depth bind, Cc/Cd clear, Z "
                "depth decompression, K copy engine, I descriptor invalidation, E list end):",
                frame - 1, text.size());
    for (size_t at = 0; at < text.size(); at += 3000)
      REXLOG_INFO("[deko3d trace] {}", text.substr(at, 3000));
  } else if (frame % 600 == 0) {
    plume::Deko3DTraceTake();  // (empty)
    plume::g_deko3dTrace = true;
  }
}

// sasrt-nx (2026-11-08, sasrt_deko3d_diag_barriers): the commonest barrier causes so far.
std::string BarrierCauses() {
  if (!REXCVAR_GET(sasrt_deko3d_diag_barriers))
    return {};
  const plume::Deko3DStats &st = plume::GetDeko3DStats();
  std::vector<std::pair<uint64_t, uint32_t>> all;
  for (uint32_t m = 0; m < 3; ++m)
    for (uint32_t w = 0; w < 256; ++w)
      if (const uint64_t n = st.barrierWhy[m][w].load(std::memory_order_relaxed))
        all.emplace_back(n, (m << 8) | w);
  std::sort(all.rbegin(), all.rend());
  static const char *const kModes[3] = {"primitives", "fragments", "full"};
  static const char *const kWhy[8] = {"read", "copy", "colour", "depth", "other", "buffer", "compute", "merged"};
  std::string text = "; barrier causes (sasrt_deko3d_diag_barriers):";
  for (size_t i = 0; i < all.size() && i < 16; ++i) {
    text += fmt::format(" {} {}", all[i].first, kModes[all[i].second >> 8]);
    const uint32_t w = all[i].second & 255;
    if (!w)
      text += " (none)";
    for (uint32_t b = 0; b < 8; ++b)
      if ((w >> b) & 1)
        text += fmt::format("{}{}", (w & ((1u << b) - 1)) ? "+" : " ", kWhy[b]);
    text += ',';
  }
  // sasrt-nx (2026-11-08, the second GPU round's census).
  auto n = [](const std::atomic<uint64_t> &c) { return c.load(std::memory_order_relaxed); };
  text += fmt::format(" census: {} descriptor invalidations, {} code invalidations, {} framebuffer binds ({} depth "
                      "target changes), {} lists, {} dispatches, {} clears",
                      n(st.diagDescInvalidates), n(st.diagCodeInvalidates), n(st.diagFramebufferBinds),
                      n(st.diagDepthChanges), n(st.diagListEnds), n(st.diagDispatches), n(st.diagClears));
  return text;
}

std::string ReportBody(const plume::Deko3DStats &st, uint64_t compiles);

std::string Report() {
  const plume::Deko3DStats &st = plume::GetDeko3DStats();
  return ReportBody(st, st.shaderCompiles) + BarrierCauses();
}

std::string ReportBody(const plume::Deko3DStats &st, uint64_t compiles) {
  std::string r = fmt::format("deko3d: {} draws ({} through the address window), {} pipeline binds ({} state binds), bound "
                     "textures {} read, {} skipped (A/B 39); uam {} compiles (avg {:.0f} ms, {} failed; "
                     "{} stages given the generic variant meanwhile, {} compiled and {} read from the cache in the background, {} "
                     "swapped in; blocking: {} a draw "
                     "waited for ({:.0f} ms in all, boost mode {} times for {:.0f} ms), {} the prebuild waited for; "
                     "background rests {:.1f} s), "
                     "DKSH cache {} hits, {} added; memory: images {} MB, buffers {} MB, code {} KB, commands {} KB; "
                     "barriers: {} primitives, {} fragments, {} full ({} invalidating textures); copy and 2D engines: {} "
                     "buffer copies, {} texture uploads, {} readbacks, {} image copies, {} resolves, {} blits; multisampled pipelines {} ({} with alpha to coverage, {} given the standard sample positions: banjo_deko3d_msaa_positions); indexed strips drawn with primitive restart {} (banjo_deko3d_strip_restart); compiles a draw waited for made in one pass {} (banjo_deko3d_fast_first_compile)",
                     st.draws.load(), st.windowDraws.load(), st.pipelineBinds.load(), st.stateBinds.load(),
                     st.textureRuns.load(), st.textureSkips.load(), compiles,
                     compiles ? double(st.shaderCompileNs.load()) / 1e6 / double(compiles) : 0.0,
                     st.shaderCompileFailures.load(), st.variantFallbacks.load(), st.variantBackground.load() - st.variantCacheReadsBackground.load(),
                     st.variantCacheReadsBackground.load(),
                     st.variantUpgrades.load(), st.compileWaits.load(), double(st.compileWaitNs.load()) / 1e6,
                     st.compileBoosts.load(), double(st.compileBoostNs.load()) / 1e6, st.compileBackgroundWaits.load(),
                     double(st.backgroundRestNs.load()) / 1e9, g_cache.hits, g_cache.added, st.imageBytes.load() >> 20,
                     st.bufferBytes.load() >> 20, st.codeBytes.load() >> 10, st.commandBytes.load() >> 10,
                     st.barriersPrimitives.load(), st.barriersFragments.load(), st.barriersFull.load(),
                     st.barriersImageInvalidate.load(), st.copyBuffer.load(), st.copyUpload.load(), st.copyReadback.load(),
                     st.copyImage.load(), st.copyResolve.load(), st.copyBlit.load(), st.msaaPipelines.load(),
                     st.msaaAlphaToCoverage.load(), st.msaaPositioned.load(), st.stripRestartDraws.load(),
                     st.fastCompiles.load());
  // renut-ultimate (2026-11-08): the depth decompresses (sasrt_deko3d_depth_resolve, ultimate_deko3d_depth_resolve_lazy).
  if (Deko3DDepthResolveOn())
    r += fmt::format("; depth decompresses (sasrt_deko3d_depth_resolve): {} where the target was bound, {} left for "
                     "later, {} done later for a read", st.depthResolvesBound.load(), st.depthResolvesDeferred.load(),
                     st.depthResolvesLater.load());
  // renut-ultimate (2026-11-10, port 59): descriptor writes and the ones lo_deko3d_descriptor_cache served.
  r += fmt::format("; descriptor writes {} ({} from the cache: lo_deko3d_descriptor_cache)", st.descriptorWrites.load(),
                   st.descriptorCacheHits.load());
  return r;
}

#else

std::unique_ptr<plume::RenderShader> CreateGameShader(plume::RenderDevice *, uint64_t) { return nullptr; }
void InstallShaderCache() {}
std::string Report() { return {}; }

#endif

bool BoolSpecMask(uint64_t hash, uint32_t mask[8]) {
  const PackageEntry *e = Find(OpenInstalled(), hash);
  if (!e)
    e = Find(Open(), hash);
  if (!e)
    return false;
  std::memcpy(mask, e->boolMask, sizeof(e->boolMask));
  return true;
}

void SaveShaderCache() {
  std::lock_guard<std::mutex> lock(g_cache.mutex);
  if (g_cache.file) {
    std::fflush(g_cache.file);
    fsync(fileno(g_cache.file));  // banjo-nx (2026-10-20): to the card, not only to the file system service
  }
}

}  // namespace banjo::renut_gpu::deko3d
