// banjo-nx (2026-10-29): title update 3's files (title_update.h).
#include "renut_engine/title_update.h"
#include "banjo_switch_default.h"  // banjo-nx (2026-11-01): the shipped Switch defaults

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system.h>
#include <rex/system/flags.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/platform.h>

#if REX_PLATFORM_SWITCH
#include <switch.h>  // setGetSystemLanguage
#endif

REXCVAR_DEFINE_BOOL(banjo_font_andy_bold, BANJO_SWITCH_DEFAULT(true, false), "Banjo/Title update",
                    "1.0.0's text font: Latin and Cyrillic text in Andy Bold (euro.sbp), which the update replaced with "
                    "a bold Comic Sans (russian.sbp). Same size")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_dialogue_boxes_original, BANJO_SWITCH_DEFAULT(true, false), "Banjo/Title update",
                    "1.0.0's dialogue boxes: the in-game and menu dialogue scenes from 1.0.0's packages (the old box "
                    "with its cogs) instead of the update's")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(language, "system", "Banjo/Language",
                      "The game's language: system (the Switch's system language; English elsewhere, and when the game "
                      "has no text for it), english, french, german, spanish, italian, japanese, korean, tradchinese, "
                      "simpchinese, russian, polish, czech, dutch, norwegian or swedish")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(banjo_text_style_log_s, 0, "Banjo/Title update",
                     "Diagnostics: every N seconds, log the text size inputs (the profile's choice, the default word) "
                     "and the four speech/info style prefixes (0 = never)")
    .range(0, 3600)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace banjo::title_update {

namespace fs = std::filesystem;

namespace {

// OnConfigurePaths's folders, for ApplyLanguage.
fs::path g_config_path, g_game_root;

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
  // What sub_82363288 chose (0x82D99F68: the name, +128 the text folder, +256 the game's language number).
  REXLOG_INFO("[title update] the game's language: {} ({}), text folder {}",
              memory->TranslateVirtual<const char *>(0x82D99F68), ReadBe32(memory, 0x82D99F68 + 256),
              memory->TranslateVirtual<const char *>(0x82D99F68 + 128));
}

// renut-nx 1.0.1 (2026-11-02, the user's request): the language key. The game chooses its language once at the start
// (sub_8223ADB4's call of sub_82363288) from what a console's settings say: the XConfig language (XTLGetLanguage) gives
// the language, except that the XConfig country decides for five countries whatever the language is (XGetLocale's
// locales 9 and 29 Czech and Slovak Republics, 16 Hungary, 23 Netherlands, 25 Norway, 32 Sweden). So a language is the
// pair a console would have, and the game then does what it does on that console: loctext\<name>\ for the text, its
// font files (sub_822F4EE8). Hungarian and Portuguese are in the game's table, but the disc has no text for them.
struct Language {
  const char *name;
  uint32_t xlanguage;  // XConfig's language: 1 English, 2 Japanese, 3 German, 4 French, 5 Spanish, 6 Italian, 7 Korean,
                       // 8 traditional Chinese, 10 simplified Chinese, 11 Polish, 12 Russian
  uint32_t country;    // XConfig's country: 103 the United States (the default; the game looks at it for no language
                       // but these), 35 Great Britain, 23 Czech Republic, 74 Netherlands, 75 Norway, 90 Sweden
};
constexpr uint32_t kCountryDefault = 103;
constexpr Language kLanguages[] = {
    {"english", 1, kCountryDefault}, {"englishuk", 1, 35},                // the game's "english" and "englishus"
    {"french", 4, kCountryDefault},      {"german", 3, kCountryDefault},
    {"spanish", 5, kCountryDefault}, {"italian", 6, kCountryDefault},     {"japanese", 2, kCountryDefault},
    {"korean", 7, kCountryDefault},  {"tradchinese", 8, kCountryDefault}, {"simpchinese", 10, kCountryDefault},
    {"russian", 12, kCountryDefault}, {"polish", 11, kCountryDefault},    {"czech", 1, 23},
    {"dutch", 1, 74},                {"norwegian", 1, 75},                {"swedish", 1, 90},
};

// language = "system": the Switch's system language as the name of one of the game's. The language codes are the
// system's (setGetSystemLanguage: "en-US", "fr-CA", ...), so a language a newer firmware adds is just not found. The
// game has no text for Portuguese: English. Elsewhere (the PC build): English.
const char *SystemLanguage() {
#if REX_PLATFORM_SWITCH
  struct Code {
    const char *code, *name;
  };
  static constexpr Code kCodes[] = {
      {"ja", "japanese"},       {"en-US", "english"},     {"en-GB", "englishuk"},  {"fr", "french"},
      {"fr-CA", "french"},      {"de", "german"},         {"it", "italian"},       {"es", "spanish"},
      {"es-419", "spanish"},    {"zh-CN", "simpchinese"}, {"zh-Hans", "simpchinese"}, {"zh-TW", "tradchinese"},
      {"zh-Hant", "tradchinese"}, {"ko", "korean"},       {"nl", "dutch"},         {"ru", "russian"},
      {"pl", "polish"},
  };
  u64 value = 0;
  const Result init = setInitialize();
  if (R_FAILED(init)) {
    REXLOG_WARN("[language] the system language could not be read (0x{:X}): English", init);
    return "english";
  }
  const Result got = setGetSystemLanguage(&value);
  setExit();
  if (R_FAILED(got)) {
    REXLOG_WARN("[language] the system language could not be read (0x{:X}): English", got);
    return "english";
  }
  char code[9] = {};
  std::memcpy(code, &value, 8);
  for (const Code &c : kCodes)
    if (std::strcmp(code, c.code) == 0) {
      REXLOG_INFO("[language] the Switch's system language is {}", code);
      return c.name;
    }
  REXLOG_INFO("[language] the game has no text for the Switch's system language {}: English", code);
#endif
  return "english";
}

// A renut.toml from before 1.0.1 gets the key, with its explanation, so that a player who only replaces the NRO can find
// it: in front of the first [section] (renut.toml's input tables: a key after them would belong to them). A key the file
// already has, or has commented out, is left alone.
void AddLanguageKeyToToml(const fs::path &path) {
  std::string text;
  {
    std::ifstream in(path, std::ios::binary);
    if (!in)
      return;
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  size_t section = std::string::npos;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos)
      end = text.size();
    size_t k = at;
    while (k < end && (text[k] == ' ' || text[k] == '\t' || text[k] == '#'))
      ++k;
    if (text.compare(k, 8, "language") == 0) {
      k += 8;
      while (k < end && (text[k] == ' ' || text[k] == '\t'))
        ++k;
      if (k < end && text[k] == '=')
        return;
    }
    if (section == std::string::npos && at < end && text[at] == '[')
      section = at;
    at = end + 1;
  }
  const char *nl = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
  std::string block;
  for (const char *line :
       {"# ---- Language ----",
        "# The game's language. \"system\" follows the Switch's system language (English if the game has no text for it).",
        "# Or one of: \"english\", \"french\", \"german\", \"spanish\", \"italian\", \"japanese\", \"korean\", \"tradchinese\",",
        "# \"simpchinese\", \"russian\", \"polish\", \"czech\", \"dutch\", \"norwegian\", \"swedish\".",
        "language = \"system\""}) {
    block += line;
    block += nl;
  }
  if (section != std::string::npos) {
    text.insert(section, block + nl);
  } else {
    if (!text.empty() && text.back() != '\n')
      text += nl;
    text += nl + block;
  }
  fs::path tmp = path;
  tmp += ".tmp";
  std::error_code ec;
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(text.data(), std::streamsize(text.size()));
    out.flush();
    if (!out) {
      out.close();
      fs::remove(tmp, ec);
      REXLOG_WARN("[language] could not add the language key to {}", rex::path_to_utf8(path));
      return;
    }
  }
  fs::remove(path, ec);
  fs::rename(tmp, path, ec);
  REXLOG_INFO("[language] language key added to {}", rex::path_to_utf8(path));
}

}  // namespace

void ConfigurePaths(rex::PathConfig &paths) {
  std::error_code ec;
  g_config_path = paths.config_path;
  g_game_root = paths.game_data_root;
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

void ApplyLanguage() {
  if (!g_config_path.empty())
    AddLanguageKeyToToml(g_config_path);
  std::string name = REXCVAR_GET(language);
  for (char &c : name)
    c = (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
  if (name == "system")
    name = SystemLanguage();
  const Language *language = nullptr;
  for (const Language &l : kLanguages)
    if (name == l.name)
      language = &l;
  if (!language) {
    REXLOG_WARN("[language] language = \"{}\" is not one of the game's: the game stays in English", name);
    return;
  }
  if (language->xlanguage == 1 && language->country == kCountryDefault)
    return;  // English: as the console defaults are
  std::error_code ec;
  // (English's text is not in loctext.)
  if (language->country != 35 && !g_game_root.empty() &&
      !fs::is_directory(g_game_root / "loctext" / language->name, ec)) {
    REXLOG_WARN("[language] the game folder has no loctext/{}: the game stays in English", language->name);
    return;
  }
  REXCVAR_SET(user_language, language->xlanguage);
  REXCVAR_SET(user_country, language->country);
  REXLOG_INFO("[language] {}: the console's language {}, country {}", language->name, language->xlanguage,
              language->country);
}

void ApplyStyleOptions() {
  const bool andy_bold = REXCVAR_GET(banjo_font_andy_bold);
  const bool original_boxes = REXCVAR_GET(banjo_dialogue_boxes_original);
  rex::memory::Memory *memory = REX_KERNEL_MEMORY();
  // A diagnostic for PC tests: it follows game pointers from another thread, so not for console packages.
  if (const int every = REXCVAR_GET(banjo_text_style_log_s); every > 0) {
    // Never joined: detach() throws on the Switch.
    new std::thread([memory, every]() {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(every));
        LogTextStyle(memory);
      }
    });
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
