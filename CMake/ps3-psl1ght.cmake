# Cross-compilation toolchain for PlayStation 3 homebrew using PSL1GHT.
#
# The SDK is supplied by the ps3dev / ps3dev-sdl2 environment:
#   PS3DEV=/usr/local/ps3dev
#   PSL1GHT=$PS3DEV

set(CMAKE_SYSTEM_NAME PS3)
set(CMAKE_SYSTEM_PROCESSOR powerpc64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# CMake loads CMake/Platform/PS3.cmake while detecting this target.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

if(NOT DEFINED ENV{PS3DEV} OR "$ENV{PS3DEV}" STREQUAL "")
  message(FATAL_ERROR "PS3DEV is not set. Use the ps3dev-sdl2 environment or export PS3DEV.")
endif()

set(PS3DEV "$ENV{PS3DEV}")
if(DEFINED ENV{PSL1GHT} AND NOT "$ENV{PSL1GHT}" STREQUAL "")
  set(PSL1GHT "$ENV{PSL1GHT}")
else()
  set(PSL1GHT "${PS3DEV}")
endif()

foreach(_tool gcc g++ ar ranlib strip ld nm objcopy)
  if(_tool STREQUAL "g++")
    set(_var CMAKE_CXX_COMPILER)
  elseif(_tool STREQUAL "gcc")
    set(_var CMAKE_C_COMPILER)
  elseif(_tool STREQUAL "ar")
    set(_var CMAKE_AR)
  elseif(_tool STREQUAL "ranlib")
    set(_var CMAKE_RANLIB)
  elseif(_tool STREQUAL "strip")
    set(_var CMAKE_STRIP)
  elseif(_tool STREQUAL "ld")
    set(_var CMAKE_LINKER)
  elseif(_tool STREQUAL "nm")
    set(_var CMAKE_NM)
  else()
    set(_var CMAKE_OBJCOPY)
  endif()
  set(${_var} "${PS3DEV}/ppu/bin/ppu-${_tool}" CACHE FILEPATH "PS3 PPU ${_tool}" FORCE)
endforeach()

if(NOT EXISTS "${CMAKE_C_COMPILER}" OR NOT EXISTS "${CMAKE_CXX_COMPILER}")
  message(FATAL_ERROR "PPU compiler not found under ${PS3DEV}/ppu/bin")
endif()

set(CMAKE_FIND_ROOT_PATH
  "${PS3DEV}/ppu"
  "${PSL1GHT}"
  "${PS3DEV}/portlibs/ppu"
  CACHE STRING "PS3 SDK search roots" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# PSL1GHT programs use the Cell PPU ABI and its small TOC model.
set(_ps3_cpu_flags "-mcpu=cell -mno-altivec -fno-strict-aliasing -fno-common -fno-gnu-unique -mminimal-toc")
set(CMAKE_C_FLAGS_INIT "${_ps3_cpu_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_ps3_cpu_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT
  "-mminimal-toc -Wl,--no-multi-toc -L${PSL1GHT}/ppu/lib -L${PS3DEV}/ppu/lib -L${PS3DEV}/portlibs/ppu/lib")

# SDL2 is installed by the ps3dev-sdl2 portlibs build.
if(EXISTS "${PS3DEV}/portlibs/ppu/lib/cmake/SDL2/SDL2Config.cmake")
  set(SDL2_DIR "${PS3DEV}/portlibs/ppu/lib/cmake/SDL2" CACHE PATH "PS3 SDL2 CMake package" FORCE)
endif()

find_program(PS3_CCACHE_EXECUTABLE ccache)
if(PS3_CCACHE_EXECUTABLE)
  set(CMAKE_C_COMPILER_LAUNCHER "${PS3_CCACHE_EXECUTABLE}" CACHE STRING "C compiler launcher")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${PS3_CCACHE_EXECUTABLE}" CACHE STRING "C++ compiler launcher")
endif()

set(PS3 TRUE CACHE BOOL "Build for PlayStation 3" FORCE)
