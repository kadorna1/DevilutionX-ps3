# PlayStation 3 / PSL1GHT platform configuration.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "PS3" AND NOT PS3)
  return()
endif()

set(PS3 TRUE CACHE BOOL "Building for PlayStation 3" FORCE)

set(DEVILUTIONX_PS3_TITLE_ID "DEVILUT00" CACHE STRING "PS3 package TITLE_ID and writable game directory")
string(LENGTH "${DEVILUTIONX_PS3_TITLE_ID}" _ps3_title_id_length)
if(NOT _ps3_title_id_length EQUAL 9 OR NOT "${DEVILUTIONX_PS3_TITLE_ID}" MATCHES "^[A-Z0-9]+$")
  message(FATAL_ERROR "DEVILUTIONX_PS3_TITLE_ID must be exactly 9 uppercase letters/digits")
endif()

# PS3 SDL2 uses the PSL1GHT RSX renderer. Keep upscaling enabled (the
# renderer is the presentation path on this SDL backend).
add_compile_definitions(
  __PS3__
  DEVILUTIONX_PS3_TITLE_ID="${DEVILUTIONX_PS3_TITLE_ID}"
)

# Console build: single-player, no desktop or online integrations.
set(DISCORD_INTEGRATION OFF CACHE BOOL "" FORCE)
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

# Save a little main-memory footprint on the PS3.
set(DEVILUTIONX_PALETTE_TRANSPARENCY_BLACK_16_LUT OFF CACHE BOOL "" FORCE)

# Some packages in the PSL1GHT environment do not ship a CMake config file
# for SDL2. The PS3 toolchain adds the portlibs prefix to CMAKE_FIND_ROOT_PATH.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/ps3/finders")

set(CMAKE_DL_LIBS "")
