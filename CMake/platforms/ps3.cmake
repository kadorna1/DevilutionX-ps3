# PlayStation 3 / PSL1GHT platform configuration.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "PS3" AND NOT PS3)
  return()
endif()

set(PS3 TRUE CACHE BOOL "Building for PlayStation 3" FORCE)

# The PS3 toolchain sysroot does not automatically add PSL1GHT's PPU headers.
# Startup diagnostics use raw LV2 syscall wrappers from ppu-lv2.h.
set(_psl1ght_ppu_include "${PSL1GHT}/ppu/include")
if(NOT EXISTS "${_psl1ght_ppu_include}/ppu-lv2.h")
  message(FATAL_ERROR "PSL1GHT PPU headers not found: ${_psl1ght_ppu_include}")
endif()
include_directories("${_psl1ght_ppu_include}")
# The PS3 runtime does not support loading ordinary shared libraries. Make
# dependency selection default to static archives for this target.
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)

# Keep PSL1GHT's process-parameter record in Release builds. The global
# --gc-sections pass would otherwise discard this loader metadata.
add_link_options("LINKER:--undefined=__sys_process_param")

set(DEVILUTIONX_PS3_TITLE_ID "DEVILUT00" CACHE STRING "PS3 package TITLE_ID and writable game directory")
string(LENGTH "${DEVILUTIONX_PS3_TITLE_ID}" _ps3_title_id_length)
if(NOT _ps3_title_id_length EQUAL 9 OR NOT "${DEVILUTIONX_PS3_TITLE_ID}" MATCHES "^[A-Z0-9]+$")
  message(FATAL_ERROR "DEVILUTIONX_PS3_TITLE_ID must be exactly 9 uppercase letters/digits")
endif()

# PS3 SDL2 uses the PSL1GHT RSX renderer. Keep upscaling enabled (the
# renderer is the presentation path on this SDL backend).
set(DEVILUTIONX_DISPLAY_TEXTURE_FORMAT SDL_PIXELFORMAT_ARGB8888 CACHE STRING "PS3 RSX texture format" FORCE)
add_compile_definitions(
  __PS3__
  DEVILUTIONX_PS3_TITLE_ID="${DEVILUTIONX_PS3_TITLE_ID}"
  # PSL1GHT/Newlib provides POSIX file and directory operations through
  # startup-initialized LV2 syscall callbacks. Use those APIs instead of
  # libstdc++'s std::filesystem implementation, which calls unavailable
  # fchmod in this SDK's static libstdc++ archive.
  DVL_NO_FILESYSTEM
)

# Console build: single-player, no desktop or online integrations.
set(DISCORD_INTEGRATION OFF CACHE BOOL "" FORCE)
# Demo-mode save-copy helpers require std::filesystem directory iteration.
# The PS3 target uses the POSIX fallback, so omit this non-gameplay feature.
set(DISABLE_DEMOMODE ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ASAN OFF CACHE BOOL "" FORCE)
set(UBSAN OFF CACHE BOOL "" FORCE)
set(DISABLE_LTO ON CACHE BOOL "" FORCE)
set(NONET ON CACHE BOOL "" FORCE)
set(DISABLE_TCP ON CACHE BOOL "" FORCE)
set(DISABLE_ZERO_TIER ON CACHE BOOL "" FORCE)
set(DEVILUTIONX_DISABLE_STRIP ON CACHE BOOL "" FORCE)
set(DEVILUTIONX_STATIC_CXX_STDLIB ON CACHE BOOL "" FORCE)
set(DEVILUTIONX_GAMEPAD_TYPE PlayStation CACHE STRING "" FORCE)

# The PS3 portlibs image does not provide all CMake find_package metadata.
# Build zlib and bzip2 from the vendored sources as static archives; libpng
# uses the bundled zlib FindZLIB shim exposed by 3rdParty/zlib.
set(DEVILUTIONX_SYSTEM_ZLIB OFF CACHE BOOL "" FORCE)
set(DEVILUTIONX_STATIC_ZLIB ON CACHE BOOL "" FORCE)
set(DEVILUTIONX_SYSTEM_BZIP2 OFF CACHE BOOL "" FORCE)
set(DEVILUTIONX_STATIC_BZIP2 ON CACHE BOOL "" FORCE)

# Reuse SDL2 from the ps3dev-sdl2 portlibs. Build DevilutionX's image and
# audio helper libraries from its vendored sources to avoid mismatched
# system-library dependencies.
set(DEVILUTIONX_SYSTEM_SDL2 ON CACHE BOOL "" FORCE)
set(DEVILUTIONX_STATIC_SDL2 ON CACHE BOOL "" FORCE)
set(DEVILUTIONX_SYSTEM_SDL_IMAGE OFF CACHE BOOL "" FORCE)
set(DEVILUTIONX_SYSTEM_SDL_AUDIOLIB OFF CACHE BOOL "" FORCE)
set(DEVILUTIONX_RESAMPLER_SDL OFF CACHE BOOL "" FORCE)
set(DEVILUTIONX_RESAMPLER_SPEEX ON CACHE BOOL "" FORCE)

# Built-in engine assets are copied as loose files into USRDIR/assets.
# This avoids requiring the host-side smpq utility in the SDK image.
set(BUILD_ASSETS_MPQ OFF CACHE BOOL "" FORCE)
set(BUILD_MOD_MPQS OFF CACHE BOOL "" FORCE)

# Use unpacked save files on the PS3 HDD instead of MPQ-contained saves.
set(UNPACKED_SAVES ON CACHE BOOL "" FORCE)

# Cell PPU supports AltiVec, not VSX. libpng otherwise selects VSX kernels
# solely because the target processor is powerpc64; omit those sources.
set(PNG_POWERPC_VSX off CACHE STRING "Disable unsupported VSX on Cell PPU" FORCE)

# Save a little main-memory footprint on the PS3.
set(DEVILUTIONX_PALETTE_TRANSPARENCY_BLACK_16_LUT OFF CACHE BOOL "" FORCE)

# Some packages in the PSL1GHT environment do not ship a CMake config file
# for SDL2. The PS3 toolchain adds the portlibs prefix to CMAKE_FIND_ROOT_PATH.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/ps3/finders")

set(CMAKE_DL_LIBS "")
