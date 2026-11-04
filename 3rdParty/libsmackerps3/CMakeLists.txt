if(NOT PS3)
  return()
endif()

add_library(libsmackerps3 STATIC
  src/smacker.c
  src/PS3SmackerAdapter.cpp
)
target_include_directories(libsmackerps3 PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/include
)
target_link_libraries(libsmackerps3 PUBLIC libsmackerdec)
