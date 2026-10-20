// banjo-nx (2026-10-17): Renut's renderer on plume's deko3d back end (banjo_renut_deko3d.h, docs/deko3d-renderer.md).
#include "renut_engine/gpu/banjo_renut_deko3d.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <unistd.h>  // ftruncate (LoadCache)

#include <fmt/format.h>
#include <rex/logging.h>
#include <zstd.h>

#include "renut_engine/gpu/banjo_renut_tuning.h"

#if defined(PLUME_DEKO3D)
#include <plume_deko3d.h>
#endif

// The GLSL package (tools/deko3d_shader_package.py: deko3d_glsl_switch.cpp embeds it); absent from builds made without
// one, hence weak.
extern const uint8_t g_deko3dGlslPackage[] __attribute__((weak));
extern const size_t g_deko3dGlslPackageSize __attribute__((weak));

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

Package &Open() {
  std::call_once(g_package.once, [] {
    Package &p = g_package;
    if (&g_deko3dGlslPackageSize == nullptr || g_deko3dGlslPackageSize < sizeof(PackageHeader)) {
      REXLOG_ERROR("[deko3d] this NRO has no GLSL package (tools/deko3d_shader_package.py): no game shaders");
      return;
    }
    p.header = reinterpret_cast<const PackageHeader *>(g_deko3dGlslPackage);
    if (std::memcmp(p.header->magic, "BDKG", 4) != 0 || p.header->version != 1) {
      REXLOG_ERROR("[deko3d] the GLSL package is not one this build reads");
      return;
    }
    p.entries = reinterpret_cast<const PackageEntry *>(g_deko3dGlslPackage + p.header->tableOffset);
    p.data = g_deko3dGlslPackage + p.header->dataOffset;
    p.dict = ZSTD_createDDict(g_deko3dGlslPackage + sizeof(PackageHeader), p.header->dictSize);
    p.dctx = ZSTD_createDCtx();
    p.ok = p.dict && p.dctx;
    REXLOG_INFO("[deko3d] GLSL package: {} shaders, {} KB in the NRO", p.header->count, g_deko3dGlslPackageSize >> 10);
  });
  return g_package;
}

const PackageEntry *Find(Package &p, uint64_t hash) {
  if (!p.ok)
    return nullptr;
  const PackageEntry *begin = p.entries, *end = p.entries + p.header->count;
  const PackageEntry *it = std::lower_bound(begin, end, hash, [](const PackageEntry &e, uint64_t h) { return e.hash < h; });
  return (it != end && it->hash == hash) ? it : nullptr;
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
      long at = sizeof(head);
      for (;;) {
        uint64_t key;
        uint32_t size, magic;
        if (at + 16 > length || std::fseek(c.file, at, SEEK_SET) != 0 || std::fread(&key, sizeof(key), 1, c.file) != 1 ||
            std::fread(&size, sizeof(size), 1, c.file) != 1 || std::fread(&magic, sizeof(magic), 1, c.file) != 1)
          break;
        if (size < 64 || size > (16u << 20) || at + 12 + long(size) > length || magic != kDkshMagic)
          break;
        c.index[key] = {at + 12, size};
        at += 12 + long(size);
      }
      c.end = at;
      if (at != length) {
        REXLOG_WARN("[deko3d] DKSH cache {}: {} bytes after the last whole record ({} of them) dropped", c.path,
                    length - at, c.index.size());
        std::fflush(c.file);
        if (ftruncate(fileno(c.file), at) != 0)
          REXLOG_WARN("[deko3d] DKSH cache {}: could not be cut; new records overwrite the rest", c.path);
      }
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

bool PackageAvailable() { return Open().ok; }

#if defined(PLUME_DEKO3D)

std::unique_ptr<plume::RenderShader> CreateGameShader(plume::RenderDevice *device, uint64_t hash) {
  Package &p = Open();
  const PackageEntry *e = Find(p, hash);
  if (!e || !device) {
    static int logged = 0;
    if (p.ok && logged++ < 16)
      REXLOG_ERROR("[deko3d] shader {:016X} is not in the GLSL package", hash);
    return nullptr;
  }
  std::vector<uint8_t> blob(sizeof(plume::Deko3DShaderBlob) + e->size);
  plume::Deko3DShaderBlob header;
  header.stage = e->stage;
  header.glslSize = e->size;
  header.dkshSize = 0;
  std::memcpy(blob.data(), &header, sizeof(header));
  {
    std::lock_guard<std::mutex> lock(p.mutex);
    const size_t n = ZSTD_decompress_usingDDict(p.dctx, blob.data() + sizeof(header), e->size, p.data + e->offset,
                                                e->compressedSize, p.dict);
    if (ZSTD_isError(n) || n != e->size) {
      REXLOG_ERROR("[deko3d] shader {:016X}: the GLSL did not unpack ({})", hash,
                   ZSTD_isError(n) ? ZSTD_getErrorName(n) : "short");
      return nullptr;
    }
  }
  auto shader = device->createShader(blob.data(), blob.size(), "main", plume::RenderShaderFormat::DEKO3D);
  if (shader)
    shader->setName(fmt::format("{}_{:016X}", e->stage == 4 ? "ps" : "vs", hash));
  return shader;
}

void InstallShaderCache() {
  // The back end's messages into this log (plume writes them to stderr otherwise), and into deko3d.log next to the NRO,
  // flushed at each line: the log's buffer is lost when the process dies.
  static FILE *diag = std::fopen(NextToExecutable("deko3d.log").string().c_str(), "w");
  static std::mutex diag_mutex;
  plume::SetDeko3DLogCallback([](const char *message) {
    REXLOG_INFO("[deko3d] {}", message);
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
  options.vsOutputs = Deko3DVsOutputsOn();
  options.uamFlags = Deko3DUamFlags();
  options.mergeBarriers = Deko3DBarrierMergeOn();  // banjo-nx (2026-10-19, GPU)
  options.leanBarriers = Deko3DBarrierLeanOn();
  // The uam thread on the background compile core, below the game's threads (2026-10-18).
  options.compileCore = Deko3DCompileCore();
  options.compilePriority = Deko3DCompilePriority();
  plume::Deko3DSetOptions(options);
  LoadCache();
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

std::string Report() {
  const plume::Deko3DStats &st = plume::GetDeko3DStats();
  const uint64_t compiles = st.shaderCompiles;
  return fmt::format("deko3d: {} draws ({} through the address window), {} pipeline binds ({} state binds); uam {} compiles (avg {:.0f} ms, {} failed), "
                     "DKSH cache {} hits, {} added; memory: images {} MB, buffers {} MB, code {} KB, commands {} KB; "
                     "barriers: {} primitives, {} fragments, {} full ({} invalidating textures); copy and 2D engines: {} "
                     "buffer copies, {} texture uploads, {} readbacks, {} image copies, {} resolves, {} blits",
                     st.draws.load(), st.windowDraws.load(), st.pipelineBinds.load(), st.stateBinds.load(), compiles,
                     compiles ? double(st.shaderCompileNs.load()) / 1e6 / double(compiles) : 0.0,
                     st.shaderCompileFailures.load(), g_cache.hits, g_cache.added, st.imageBytes.load() >> 20,
                     st.bufferBytes.load() >> 20, st.codeBytes.load() >> 10, st.commandBytes.load() >> 10,
                     st.barriersPrimitives.load(), st.barriersFragments.load(), st.barriersFull.load(),
                     st.barriersImageInvalidate.load(), st.copyBuffer.load(), st.copyUpload.load(), st.copyReadback.load(),
                     st.copyImage.load(), st.copyResolve.load(), st.copyBlit.load());
}

#else

std::unique_ptr<plume::RenderShader> CreateGameShader(plume::RenderDevice *, uint64_t) { return nullptr; }
void InstallShaderCache() {}
std::string Report() { return {}; }

#endif

bool BoolSpecMask(uint64_t hash, uint32_t mask[8]) {
  Package &p = Open();
  const PackageEntry *e = Find(p, hash);
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
