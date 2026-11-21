add_library(rexfilesystem OBJECT
    device.cpp
    entry.cpp
    virtual_file_system.cpp
    devices/disc_image_device.cpp
    devices/disc_image_entry.cpp
    devices/disc_image_file.cpp
    devices/host_path_device.cpp
    devices/host_path_entry.cpp
    devices/host_path_file.cpp
    devices/host_path_index.cpp
    devices/host_path_listed_switch.cpp
    devices/null_device.cpp
    devices/null_entry.cpp
    devices/null_file.cpp
    devices/startup_trace.cpp
    devices/stfs_container_device.cpp
    devices/stfs_container_entry.cpp
    devices/stfs_container_file.cpp
    lz4_blocks.cpp
    opus_decoder.cpp  # rr6-nx (2026-11-21): NAME.opcm, the music as Opus
    opus_pcm.cpp
    sfd_hevc.cpp      # rr6-nx (2026-11-21): NAME.hevc, the movies as H.265
)
add_library(rex::filesystem ALIAS rexfilesystem)

rex_target_pch(rexfilesystem
    <mutex>
    <rex/math.h>
    <rex/string.h>
    <rex/memory.h>
    <rex/filesystem.h>
    <rex/thread/mutex.h>
    <rex/system/xtypes.h>
    <rex/system/xmemory.h>
)

target_link_libraries(rexfilesystem PUBLIC rexcore)

# rr6-nx (2026-11-11, the user's hybrid): lz4_blocks.cpp's block decoders: LZ4 1.10.0 (lib/lz4.c, BSD-2) for version-1
# .lz4b files (the RRM archives) and ZXC 0.15.0 (BSD-3; its rapidhash header MIT) for version-2 ones (every other game
# file). The same sources as the installer's WebAssembly (rr6-nx-installer's wasm/src). C sources, built apart from the
# C++ PCH. ZXC's block API needs no threads (zxc_driver.c, the FILE pipeline, is left out) and no runtime dispatch
# (ZXC_ONLY_DEFAULT: the portable path on x86; on AArch64 NEON is ZXC's baseline and is used).
set(_rex_zxc "${PROJECT_SOURCE_DIR}/thirdparty/zxc")
add_library(rexblockcodecs STATIC
    ${_rex_zxc}/src/lib/zxc_common.c
    ${_rex_zxc}/src/lib/zxc_pivco_tables.c
    ${_rex_zxc}/src/lib/zxc_dict.c
    ${_rex_zxc}/src/lib/zxc_dispatch.c
    ${_rex_zxc}/src/lib/zxc_pstream.c
    ${_rex_zxc}/src/lib/zxc_seekable.c
    ${_rex_zxc}/src/lib/zxc_compress.c
    ${_rex_zxc}/src/lib/zxc_decompress.c
    ${_rex_zxc}/src/lib/zxc_huffman.c
    ${PROJECT_SOURCE_DIR}/thirdparty/lz4/lz4.c
)
set_target_properties(rexblockcodecs PROPERTIES C_STANDARD 11)
target_compile_definitions(rexblockcodecs PUBLIC ZXC_STATIC_DEFINE PRIVATE ZXC_ONLY_DEFAULT ZXC_FUNCTION_SUFFIX=_default)
target_include_directories(rexblockcodecs
    PUBLIC ${_rex_zxc}/include ${PROJECT_SOURCE_DIR}/thirdparty/lz4
    PRIVATE ${_rex_zxc}/src/lib ${_rex_zxc}/src/lib/vendors)
target_link_libraries(rexfilesystem PRIVATE rexblockcodecs)
target_include_directories(rexfilesystem PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(rexfilesystem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/devices)

# rr6-nx (2026-11-21, the user: the music re-encoded to Opus, as culdcept-nx does): opus_decoder.cpp's libopus (BSD-3):
# devkitPro's portlibs build (switch-libopus) on the Switch; a PC loads one at run time (none: the .opcm music is silent).
if(REXGLUE_PLATFORM_SWITCH)
    set(_rex_opus_pl "$ENV{DEVKITPRO}/portlibs/switch")
    if(DEFINED DEVKITPRO)
        set(_rex_opus_pl "${DEVKITPRO}/portlibs/switch")
    endif()
    if(NOT EXISTS "${_rex_opus_pl}/lib/libopus.a")
        message(FATAL_ERROR "libopus not found in ${_rex_opus_pl} (devkitPro's switch-libopus)")
    endif()
    target_include_directories(rexfilesystem PRIVATE "${_rex_opus_pl}/include/opus")
    target_link_libraries(rexfilesystem PUBLIC "${_rex_opus_pl}/lib/libopus.a")
endif()
