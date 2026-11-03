# CMake platform metadata for the PSL1GHT PowerPC 64-bit PPU.
#
# Keep the GNU dialects explicit: the project uses C11 and C++23, and the
# PS3 build uses the GNU extensions required by the PSL1GHT/newlib runtime.

set(CMAKE_C11_STANDARD_COMPILE_OPTION "-std=gnu11")
set(CMAKE_C11_EXTENSION_COMPILE_OPTION "-std=gnu11")
set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std=gnu++23")
set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "-std=gnu++23")

set(CMAKE_DL_LIBS "")
set(CMAKE_SHARED_LIBRARY_SUPPORTED FALSE)
