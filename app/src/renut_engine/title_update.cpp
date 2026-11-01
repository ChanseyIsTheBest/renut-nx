// banjo-nx (2026-10-29): title update 3's files (title_update.h).
#include "renut_engine/title_update.h"
#include "banjo_switch_default.h"  // banjo-nx (2026-11-01): the shipped Switch defaults

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>
#include <thread>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(banjo_font_andy_bold, BANJO_SWITCH_DEFAULT(true, false), "Banjo/Title update",
                    "1.0.0's text font: Latin and Cyrillic text in Andy Bold (euro.sbp), which the update replaced with "
                    "a bold Comic Sans (russian.sbp). Same size")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_dialogue_boxes_original, BANJO_SWITCH_DEFAULT(true, false), "Banjo/Title update",
                    "1.0.0's dialogue boxes: the in-game and menu dialogue scenes from 1.0.0's packages (the old box "
                    "with its cogs) instead of the update's")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(banjo_text_style_log_s, 0, "Banjo/Title update",
                     "Diagnostics: every N seconds, log the text size inputs (the profile's choice, the default word) "
                     "and the four speech/info style prefixes (0 = never)")
    .range(0, 3600)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace banjo::title_update {

namespace fs = std::filesystem;

namespace {

// Writes bytes of the game's image. Its code and read-only data are read-only on the host as well
// (XexModule::LoadContinue's page descriptors), so the pages are made writable for the write.
void WriteImage(rex::memory::Memory *memory, uint32_t address, const void *data, uint32_t size) {
  rex::memory::BaseHeap *heap = memory->LookupHeap(address);
  uint32_t old_protect = 0;
  const bool unprotected =
      heap && heap->Protect(address, size, rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite,
                            &old_protect);
  std::memcpy(memory->TranslateVirtual<uint8_t *>(address), data, size);
  if (unprotected)
    heap->Protect(address, size, old_protect ? old_protect : rex::memory::kMemoryProtectRead);
}

uint32_t ReadBe32(rex::memory::Memory *memory, uint32_t address) {
  const uint8_t *p = memory->TranslateVirtual<const uint8_t *>(address);
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

void WriteBe32(rex::memory::Memory *memory, uint32_t address, uint32_t value) {
  const uint8_t bytes[4] = {uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)};
  WriteImage(memory, address, bytes, 4);
}

// sub_822F4EE8 picks the font cache's ini files by language. 1.0.0 sent English (0) to the euro ones; the update sends
// every Latin and Cyrillic language to the russian ones. Both .sbp files hold the same 33,974 glyphs (Latin, Cyrillic,
// CJK) in the same 48x48 cells, so the russian paths are rewritten in place to the (shorter) euro ones.
struct PathRewrite {
  uint32_t address;
  const char *update;  // what the update's image holds there
  const char *original;
};
constexpr PathRewrite kFontPaths[] = {
    {0x82112AF4, "GAME:\\xuifontcachemeta\\metafont_russian.ini", "GAME:\\xuifontcachemeta\\metafont_euro.ini"},
    {0x82112B20, "GAME:\\xuifontcachemeta\\precache_russian.ini", "GAME:\\xuifontcachemeta\\precache_euro.ini"},
};

// XUI scene descriptors (sub_823B7950 builds "<type>_<project>_<name>.<ext>" from them): +0 type (1 scene), +4 the
// package (the table at 0x82D90C50), +8 the name, +268 the project (0 banjox, 2 banjoxdlc). The update moved both
// dialogue scenes into its always-loaded BanjoX_TU15 package (11) as banjoxdlc scenes with the new box; 1.0.0's are
// still in the disc's bundles, in BanjoX_Ingame (2) and BanjoX_CoreMP (4), which the update keeps loading for its
// other scenes. 1.0.0's scenes have every element the update's code binds but the two scroll arrows, which it does
// without (an element that is not found is a null handle, which every XUI call refuses).
struct SceneRewrite {
  uint32_t address;
  const char *name;
  uint32_t original_package;
};
constexpr SceneRewrite kDialogueScenes[] = {
    {0x82D91D68, "ingame_dialogdisplay", 2},
    {0x82D979A8, "frontend_dialogdisplay", 4},
};
constexpr uint32_t kSceneType = 1, kPackageTU15 = 11, kProjectBanjoX = 0, kProjectBanjoXDLC = 2;

// sub_823B42D8 / sub_823B43D0 take big or normal text from the profile (+131: bit 0x2 set, bit 0x4 big) and, when the
// profile has not set it, from this word: 0 is big. The image has 0, but the game sets 1 itself during boot (PC,
// 2026-10-30), so on an HD screen the text is normal-size unless the profile ticks "Use big font".
constexpr uint32_t kBigTextDefault = 0x82E0BD58;

// banjo_text_style_log_s: what sub_823B42D8 reads (the game object 0x82E1B240: +182 byte, +64 the player, whose
// +276 -> +8 -> +131 byte is the profile's text size) and the four style prefixes sub_8255CDA0 loaded (0x82E1A0E0,
// 256 bytes each: speechbig, speech, infobig, infosmall).
void LogTextStyle(rex::memory::Memory *memory) {
  constexpr uint32_t kGame = 0x82E1B240, kPrefixes = 0x82E1A0E0;
  const uint8_t *g = memory->TranslateVirtual<const uint8_t *>(kGame);
  const uint32_t player = ReadBe32(memory, kGame + 64);
  int profile_byte = -1;
  if (g[182] && player) {
    const uint32_t a = ReadBe32(memory, player + 276);
    const uint32_t b = a ? ReadBe32(memory, a + 8) : 0;
    if (b)
      profile_byte = *memory->TranslateVirtual<const uint8_t *>(b + 131);
  }
  // Printable bytes as they are, others as '.': the prefixes may be UTF-16.
  std::string prefixes;
  for (int i = 0; i < 4; ++i) {
    const uint8_t *p = memory->TranslateVirtual<const uint8_t *>(kPrefixes + 256 * i);
    prefixes += " [";
    for (int k = 0; k < 96; ++k)
      prefixes += (p[k] >= 0x20 && p[k] < 0x7F) ? char(p[k]) : '.';
    prefixes += "]";
  }
  REXLOG_INFO("[title update] text style: game +182 {}, player 0x{:08X}, profile byte {} (0x2 set, 0x4 big), default "
              "word {}; prefixes speechbig/speech/infobig/infosmall:{}",
              g[182], player, profile_byte, ReadBe32(memory, kBigTextDefault), prefixes);
}

}  // namespace

void ConfigurePaths(rex::PathConfig &paths) {
  std::error_code ec;
  if (paths.update_data_root.empty()) {
    const fs::path candidates[] = {
        rex::filesystem::GetExecutableFolder() / "update",
        paths.game_data_root.empty() ? fs::path() : paths.game_data_root.parent_path() / "update",
        paths.game_data_root.empty() ? fs::path() : paths.game_data_root / "update",
    };
    for (const fs::path &c : candidates) {
      if (!c.empty() && fs::exists(c / "db_index.txt", ec)) {
        paths.update_data_root = c;
        break;
      }
    }
  }
  if (paths.update_data_root.empty())
    REXLOG_WARN("[title update] no update folder (update_data_root, or \"update\" next to banjo-nx or the game "
                "folder, holding title update 3's db_index.txt and bundles): the update's data is not there");
  else
    REXLOG_INFO("[title update] update data: {}", rex::path_to_utf8(paths.update_data_root));
  if (!paths.game_data_root.empty() && !fs::exists(paths.game_data_root / "default.xexp", ec)) {
    // The recompiled code is title update 3's: the base XEX's image would not match it at all.
    REXLOG_ERROR("[title update] {} has no default.xexp. banjo-nx is title update 3 of the game: put the update's "
                 "default.xexp next to default.xex (and its data files in the update folder)",
                 rex::path_to_utf8(paths.game_data_root));
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error,
                              "banjo-nx needs title update 3: put the update's default.xexp next to default.xex in the "
                              "game folder, and its data files (db_index.txt and the bundles) in an \"update\" folder "
                              "next to banjo-nx.");
  }
}

void ApplyStyleOptions() {
  const bool andy_bold = REXCVAR_GET(banjo_font_andy_bold);
  const bool original_boxes = REXCVAR_GET(banjo_dialogue_boxes_original);
  rex::memory::Memory *memory = REX_KERNEL_MEMORY();
  // A diagnostic for PC tests: it follows game pointers from another thread, so not for console packages.
  if (const int every = REXCVAR_GET(banjo_text_style_log_s); every > 0) {
    std::thread([memory, every]() {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(every));
        LogTextStyle(memory);
      }
    }).detach();
  }
  if (!andy_bold && !original_boxes)
    return;
  if (andy_bold) {
    int done = 0;
    for (const PathRewrite &r : kFontPaths) {
      const size_t length = std::strlen(r.update) + 1;
      if (std::memcmp(memory->TranslateVirtual<const char *>(r.address), r.update, length) != 0) {
        REXLOG_WARN("[title update] banjo_font_andy_bold: 0x{:08X} does not hold {}: left as it is", r.address,
                    r.update);
        continue;
      }
      char text[64] = {};
      std::strcpy(text, r.original);
      WriteImage(memory, r.address, text, uint32_t(length));
      ++done;
    }
    REXLOG_INFO("[title update] banjo_font_andy_bold: {} of 2 font paths now 1.0.0's (euro.sbp, Andy Bold)", done);
  }
  if (original_boxes) {
    int done = 0;
    for (const SceneRewrite &s : kDialogueScenes) {
      const size_t length = std::strlen(s.name) + 1;
      if (ReadBe32(memory, s.address) != kSceneType || ReadBe32(memory, s.address + 4) != kPackageTU15 ||
          ReadBe32(memory, s.address + 268) != kProjectBanjoXDLC ||
          std::memcmp(memory->TranslateVirtual<const char *>(s.address + 8), s.name, length) != 0) {
        REXLOG_WARN("[title update] banjo_dialogue_boxes_original: the scene at 0x{:08X} is not the update's {}: "
                    "left as it is", s.address, s.name);
        continue;
      }
      WriteBe32(memory, s.address + 4, s.original_package);
      WriteBe32(memory, s.address + 268, kProjectBanjoX);
      ++done;
    }
    REXLOG_INFO("[title update] banjo_dialogue_boxes_original: {} of 2 dialogue scenes now 1.0.0's", done);
  }
}

}  // namespace banjo::title_update
