# Find the PS3 SDL2 library installed by the ps3dev-sdl2 environment.
#
# Prefer SDL2's exported CMake package. Older PSL1GHT SDL2 builds only install
# sdl2-config, so fall back to its flags and expose a compatible SDL2::SDL2
# interface target for DevilutionX's dependency setup.

find_package(SDL2 QUIET CONFIG NO_MODULE)

if(TARGET SDL2::SDL2-static AND NOT TARGET SDL2::SDL2)
  add_library(SDL2::SDL2 ALIAS SDL2::SDL2-static)
endif()

if(TARGET SDL2::SDL2 OR TARGET SDL2::SDL2-static)
  set(SDL2_FOUND TRUE)
  return()
endif()

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
  execute_process(
    COMMAND "${SDL2_CONFIG_EXECUTABLE}" --libs
    RESULT_VARIABLE _sdl2_libs_result
    OUTPUT_VARIABLE _sdl2_libs
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()
if(NOT _sdl2_libs_result EQUAL 0)
  set(SDL2_FOUND FALSE)
  return()
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

if(NOT TARGET SDL2::SDL2)
  add_library(SDL2::SDL2 INTERFACE IMPORTED)
  set_target_properties(SDL2::SDL2 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
    INTERFACE_COMPILE_DEFINITIONS "${_sdl2_compile_definitions}"
    INTERFACE_COMPILE_OPTIONS "${_sdl2_compile_options}"
    INTERFACE_LINK_LIBRARIES "${_sdl2_libs_list}"
  )
endif()

set(SDL2_LIBRARIES SDL2::SDL2)
set(SDL2_INCLUDE_DIR ${SDL2_INCLUDE_DIRS})
set(SDL2_VERSION_STRING "2.31.0")
set(SDL2_VERSION "${SDL2_VERSION_STRING}")
set(SDL2_FOUND TRUE)
