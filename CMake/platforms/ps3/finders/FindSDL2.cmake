# Find the PS3 SDL2 static library installed by the ps3dev-sdl2 environment.
#
# SDL_PSL1GHT also installs an SDL2 CMake config that declares SDL2::SDL2 as
# SHARED IMPORTED. CMake coerces that target to STATIC on PS3 but retains the
# .so IMPORTED_LOCATION, which makes Ninja require a nonexistent libSDL2.so.
# Do not load that config on PS3. Use sdl2-config --static-libs and expose an
# interface target so the linker resolves the actual static archive instead.

find_program(SDL2_CONFIG_EXECUTABLE NAMES sdl2-config)

if(NOT SDL2_CONFIG_EXECUTABLE)
  set(SDL2_FOUND FALSE)
  return()
endif()

execute_process(
  COMMAND "${SDL2_CONFIG_EXECUTABLE}" --cflags
  RESULT_VARIABLE _sdl2_cflags_result
  OUTPUT_VARIABLE _sdl2_cflags
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _sdl2_cflags_result EQUAL 0)
  set(SDL2_FOUND FALSE)
  return()
endif()

execute_process(
  COMMAND "${SDL2_CONFIG_EXECUTABLE}" --static-libs
  RESULT_VARIABLE _sdl2_libs_result
  OUTPUT_VARIABLE _sdl2_libs
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _sdl2_libs_result EQUAL 0 OR "${_sdl2_libs}" STREQUAL "")
  message(FATAL_ERROR "PS3 SDL2 must provide sdl2-config --static-libs")
endif()

execute_process(
  COMMAND "${SDL2_CONFIG_EXECUTABLE}" --prefix
  RESULT_VARIABLE _sdl2_prefix_result
  OUTPUT_VARIABLE _sdl2_prefix
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _sdl2_prefix_result EQUAL 0 OR "${_sdl2_prefix}" STREQUAL "")
  message(FATAL_ERROR "Could not determine the PS3 SDL2 installation prefix")
endif()

set(_sdl2_static_archive "${_sdl2_prefix}/lib/libSDL2.a")
if(NOT EXISTS "${_sdl2_static_archive}")
  message(FATAL_ERROR "PS3 SDL2 static archive not found: ${_sdl2_static_archive}")
endif()

separate_arguments(_sdl2_cflags_list NATIVE_COMMAND "${_sdl2_cflags}")
separate_arguments(_sdl2_libs_list NATIVE_COMMAND "${_sdl2_libs}")
set(SDL2_INCLUDE_DIRS "")
set(_sdl2_compile_definitions "")
set(_sdl2_compile_options "")

foreach(_flag IN LISTS _sdl2_cflags_list)
  if(_flag MATCHES "^-I(.+)$")
    list(APPEND SDL2_INCLUDE_DIRS "${CMAKE_MATCH_1}")
  elseif(_flag MATCHES "^-D(.+)$")
    list(APPEND _sdl2_compile_definitions "${CMAKE_MATCH_1}")
  else()
    list(APPEND _sdl2_compile_options "${_flag}")
  endif()
endforeach()

if(NOT SDL2_INCLUDE_DIRS)
  set(SDL2_FOUND FALSE)
  return()
endif()

# Replace any generic -lSDL2 token with the archive we built in CI. Other
# static link flags from sdl2-config (e.g. -lm, -lrt, -llv2) remain intact.
list(REMOVE_ITEM _sdl2_libs_list "-lSDL2")
list(PREPEND _sdl2_libs_list "${_sdl2_static_archive}")

if(NOT TARGET SDL2::SDL2)
  add_library(SDL2::SDL2 INTERFACE IMPORTED)
  set_target_properties(SDL2::SDL2 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
    INTERFACE_COMPILE_DEFINITIONS "${_sdl2_compile_definitions}"
    INTERFACE_COMPILE_OPTIONS "${_sdl2_compile_options}"
    INTERFACE_LINK_LIBRARIES "${_sdl2_libs_list}"
  )
endif()

# Match the SDL2 CMake package's optional main-wrapper target. Keep it static
# as well; the PS3 runtime cannot load a shared SDL2main library.
set(_sdl2_main_archive "${_sdl2_prefix}/lib/libSDL2main.a")
if(EXISTS "${_sdl2_main_archive}" AND NOT TARGET SDL2::SDL2main)
  add_library(SDL2::SDL2main STATIC IMPORTED)
  set_target_properties(SDL2::SDL2main PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${_sdl2_main_archive}"
  )
endif()

set(SDL2_LIBRARIES SDL2::SDL2)
set(SDL2_INCLUDE_DIR ${SDL2_INCLUDE_DIRS})
execute_process(
  COMMAND "${SDL2_CONFIG_EXECUTABLE}" --version
  OUTPUT_VARIABLE SDL2_VERSION_STRING
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(SDL2_VERSION "${SDL2_VERSION_STRING}")
set(SDL2_FOUND TRUE)
