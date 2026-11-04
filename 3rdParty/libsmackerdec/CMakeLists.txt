include(functions/FetchContent_ExcludeFromAll_backport)

include(FetchContent)
FetchContent_Declare_ExcludeFromAll(libsmackerdec
    URL https://github.com/diasurgical/libsmackerdec/archive/0aaaf8c94a097b009d844db0d44dd7cd0ff81922.tar.gz
    URL_HASH SHA256=c3a7fbc91531eee8989b1d281415adc0209d84ffc0437c6b0b4f677ae7fd3b41
)
FetchContent_MakeAvailable_ExcludeFromAll(libsmackerdec)

# The bundled Smacker decoder originally sized each audio track's decode
# buffer from frame 0 only. Later frames can contain larger audio packets,
# so scan all packet headers and allocate for the largest packet before
# compiling the decoder.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/patch_smacker_audio_buffers.py"
        "${libsmackerdec_SOURCE_DIR}/src/SmackerDecoder.cpp"
    RESULT_VARIABLE libsmackerdec_patch_result
    OUTPUT_VARIABLE libsmackerdec_patch_output
    ERROR_VARIABLE libsmackerdec_patch_error
)
if(NOT libsmackerdec_patch_result EQUAL 0)
  message(FATAL_ERROR
      "Failed to apply the libsmackerdec audio-buffer safety patch: ${libsmackerdec_patch_error}")
endif()
message(STATUS "${libsmackerdec_patch_output}")

add_library(libsmackerdec STATIC
  ${libsmackerdec_SOURCE_DIR}/src/BitReader.cpp
  ${libsmackerdec_SOURCE_DIR}/src/FileStream.cpp
  ${libsmackerdec_SOURCE_DIR}/src/LogError.cpp
  ${libsmackerdec_SOURCE_DIR}/src/SmackerDecoder.cpp)

target_include_directories(libsmackerdec PUBLIC ${libsmackerdec_SOURCE_DIR}/include)

if(USE_SDL1)
  target_link_libraries(libsmackerdec PUBLIC ${SDL_LIBRARY})
elseif(USE_SDL3)
  target_link_libraries(libsmackerdec PUBLIC SDL3::SDL3)
  target_compile_definitions(libsmackerdec PUBLIC USE_SDL3)
elseif(TARGET SDL2::SDL2 AND NOT (DEVILUTIONX_STATIC_SDL2 AND TARGET SDL2::SDL2-static))
  target_link_libraries(libsmackerdec PUBLIC SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
  target_link_libraries(libsmackerdec PUBLIC SDL2::SDL2-static)
endif()
