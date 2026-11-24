// banjo-nx (2026-10-29): reNut upstream's DLC installer (ab60251, src/renut_engine/app/dlc_install.cpp; GPL-3.0, credit reNut),
// the DLC folder next to the NRO / exe (sdmc:/switch/banjo-nx/dlc on the Switch). katamari-nx (2026-10-31): the same
// file, game-agnostic (the running title's packages only): sdmc:/switch/katamari-nx/dlc, Beautiful Katamari's 16 packs.
// renut-ultimate (2026-11-24, the user: "also make it so that the game doesn't need to install the dlc it just reads it
// from the dlc folder instead"): rr6-nx's 2026-11-18 change (its commit a3c240ba, credit rr6-nx): the packages are read
// in place. An install writes the package's header only (XAM lists the DLC from it), and every start registers each
// package with the content manager, which mounts the package itself when the game opens it (StfsContainerDevice, read
// only). Before this, an install unpacked every file of every package into the user data folder
// (renut/0000000000000000/<title>/00000002/<package>/); those copies are no longer read (the log says where they are).
#include "renut_engine/dlc_install.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/opus_pcm.h>  // rr6-nx (2026-11-21): a package's P.opcm is not a package
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>


#include "renut_engine/renut_logging.h"

REXCVAR_DEFINE_STRING(renut_dlc_folder, "dlc", "Nuts&Bolts/DLC",
    "Folder of DLC packages (Xbox Live content files) reNut installs when the game starts, relative to "
    "reNut's folder.");

namespace renut::dlc {

namespace {

namespace fs = std::filesystem;

// An Xbox 360 package's header (big-endian): the magic, then at 0x344 the
// content type, 0x360 the title ID and 0x411 the display name (UTF-16).
constexpr size_t kHeaderBytes = 0x500;
constexpr size_t kContentTypeOffset = 0x344;
constexpr size_t kTitleIdOffset = 0x360;
constexpr size_t kDisplayNameOffset = 0x411;
constexpr size_t kDisplayNameChars = 64;
constexpr uint32_t kContentTypeMarketplace = 2;
// Where an installed package's header goes in the user data folder: the
// default profile's content, by title and content type.
constexpr const char kProfileFolder[] = "0000000000000000";
constexpr const char kContentTypeFolder[] = "00000002";
// Packages installed before, by file name and size, so an unchanged one is
// not installed again when the user data folder isn't known.
constexpr const char kInstalledList[] = "installed.txt";

fs::path g_user_data_root;

uint32_t LoadBe32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

struct Package {
    uint32_t content_type = 0;
    uint32_t title_id = 0;
    std::string name;  // display name (ASCII part)
};

// The package's header, or nothing if the file isn't an Xbox 360 package.
bool ReadPackage(const fs::path &path, Package &out) {
    std::ifstream f(path, std::ios::binary);
    std::array<uint8_t, kHeaderBytes> h{};
    if (!f.read(reinterpret_cast<char *>(h.data()), h.size()))
        return false;
    const std::string magic(reinterpret_cast<const char *>(h.data()), 4);
    if (magic != "LIVE" && magic != "PIRS" && magic != "CON ")
        return false;
    out.content_type = LoadBe32(h.data() + kContentTypeOffset);
    out.title_id = LoadBe32(h.data() + kTitleIdOffset);
    out.name.clear();
    for (size_t i = 0; i < kDisplayNameChars; ++i) {
        const uint8_t *c = h.data() + kDisplayNameOffset + 2 * i;
        const uint16_t ch = uint16_t(c[0] << 8 | c[1]);
        if (!ch)
            break;
        out.name.push_back(ch < 0x80 ? char(ch) : '?');
    }
    return true;
}

std::string HexTitle(uint32_t title_id) {
    char s[9];
    std::snprintf(s, sizeof(s), "%08X", title_id);
    return s;
}

// The header the install writes for 'file_name', if the user data folder is known.
fs::path InstalledHeader(uint32_t title_id, const std::string &file_name) {
    if (g_user_data_root.empty())
        return {};
    return g_user_data_root / kProfileFolder / HexTitle(title_id) / "Headers" / kContentTypeFolder /
           (file_name + ".header");
}

// rr6-nx (2026-11-18): where an earlier version unpacked 'file_name' (no longer read: the package is).
fs::path UnpackedFolder(uint32_t title_id, const std::string &file_name) {
    return g_user_data_root / kProfileFolder / HexTitle(title_id) / kContentTypeFolder / file_name;
}

std::vector<std::string> ReadInstalledList(const fs::path &list) {
    std::vector<std::string> lines;
    std::ifstream f(list);
    for (std::string line; std::getline(f, line);)
        if (!line.empty())
            lines.push_back(line);
    return lines;
}

std::string ListEntry(const std::string &file_name, uintmax_t size) {
    return file_name + " " + std::to_string(size);
}

} // namespace

void SetUserDataRoot(const fs::path &root) {
    g_user_data_root = root;
}

void InstallPackages(rex::Runtime *runtime) {
    if (!runtime || !runtime->kernel_state())
        return;
    fs::path folder = rex::to_path(REXCVAR_GET(renut_dlc_folder));
    if (folder.is_relative())
        folder = rex::filesystem::GetExecutableFolder() / folder;  // banjo-nx: next to the NRO / exe
    std::error_code ec;
    fs::create_directories(folder, ec);

    std::vector<fs::path> files;
    // rr6-nx: into subfolders: the packages may keep their Xbox 360 layout (<name>/content/<profile>/<title>/00000002/
    // <file>) and the folder is pointed at where they lie, not copied. rr6-nx (2026-11-21): a package's P.opcm (its music
    // as Opus, rex/filesystem/opus_pcm.h) is not a package: the package P beside it is registered, and its reads go
    // through P.opcm (StfsContainerDevice).
    for (const auto &entry : fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec))
        if (entry.is_regular_file(ec) && entry.path().filename() != kInstalledList &&
            !rex::filesystem::HasOpusPcmSuffix(rex::path_to_utf8(entry.path().filename())))
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    if (files.empty())
        return;

    const uint32_t title_id = runtime->kernel_state()->title_id();
    // rr6-nx: not in 'folder', which may point at the packages where they lie: nothing is written there.
    const fs::path list_path =
        (g_user_data_root.empty() ? rex::filesystem::GetExecutableFolder() : g_user_data_root) / kInstalledList;
    std::vector<std::string> installed = ReadInstalledList(list_path);
    bool list_changed = false;
    int newly = 0, present = 0;
    int registered = 0, unpacked_folders = 0;  // rr6-nx (2026-11-18): read in place; copies unpacked before
    uintmax_t unpacked_bytes = 0;

    for (const fs::path &path : files) {
        const std::string file_name = rex::path_to_utf8(path.filename());
        Package package;
        if (!ReadPackage(path, package)) {
            RNUT_WARN("DLC: {} is not an Xbox 360 package; left alone", file_name);
            continue;
        }
        if (package.title_id != title_id) {
            RNUT_WARN("DLC: {} ({}) is for another game (title {:08X}); left alone", file_name, package.name,
                      package.title_id);
            continue;
        }
        if (package.content_type != kContentTypeMarketplace) {
            RNUT_WARN("DLC: {} ({}) is not DLC (content type {:X}); left alone", file_name, package.name,
                      package.content_type);
            continue;
        }
        // rr6-nx (2026-11-18): read in place, whether installed now or before.
        runtime->kernel_state()->content_manager()->RegisterPackageFile(file_name, path);
        ++registered;
        if (!g_user_data_root.empty() && fs::is_directory(UnpackedFolder(title_id, file_name), ec)) {
            ++unpacked_folders;
            for (const auto &f : fs::recursive_directory_iterator(UnpackedFolder(title_id, file_name),
                                                                  fs::directory_options::skip_permission_denied, ec))
                if (f.is_regular_file(ec))
                    unpacked_bytes += f.file_size(ec);
        }
        const uintmax_t size = fs::file_size(path, ec);
        const std::string entry = ListEntry(file_name, size);
        const fs::path header = InstalledHeader(title_id, file_name);
        const bool listed = std::find(installed.begin(), installed.end(), entry) != installed.end();
        const bool header_there = !header.empty() && fs::exists(header, ec);
        if (header_there || (header.empty() && listed)) {
            ++present;
            continue;
        }

        RNUT_INFO("DLC: installing {} ({})...", package.name, file_name);
        const std::string utf8 = rex::path_to_utf8(path);
        const auto result =
            runtime->kernel_state()->content_manager()->InstallContent(utf8.c_str(), /*extract_files=*/false);
        if (result != 0) {
            RNUT_ERROR("DLC: {} ({}) did not install (error {:08X})", package.name, file_name, unsigned(result));
            continue;
        }
        RNUT_INFO("DLC: {} installed", package.name);
        ++newly;
        if (!listed) {
            installed.push_back(entry);
            list_changed = true;
        }
    }

    if (list_changed) {
        std::ofstream f(list_path, std::ios::trunc);
        for (const std::string &line : installed)
            f << line << "\n";
    }
    if (newly || present)
        RNUT_INFO("DLC: {} package(s) installed now, {} already installed", newly, present);
    if (registered)
        RNUT_INFO("DLC: {} package(s) read in place from {} (nothing unpacked)", registered, rex::path_to_utf8(folder));
    if (unpacked_folders)
        RNUT_INFO("DLC: {} folder(s) unpacked by an earlier version ({} MB) in {}: no longer read, safe to delete",
                  unpacked_folders, unpacked_bytes / 1000000,
                  rex::path_to_utf8(g_user_data_root / kProfileFolder / HexTitle(title_id) / kContentTypeFolder));
}

} // namespace renut::dlc
