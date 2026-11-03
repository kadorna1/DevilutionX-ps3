include(functions/FetchContent_ExcludeFromAll_backport)

# Workaround for deprecation of older CMake versions
set(CMAKE_POLICY_VERSION_MINIMUM 3.22)

set(LUA_ENABLE_TESTING OFF)
set(LUA_BUILD_COMPILER OFF)
if(DEVILUTIONX_STATIC_LUA)
  set(LUA_ENABLE_SHARED OFF)
else()
  set(LUA_ENABLE_SHARED ON)
endif()

if(ANDROID AND NOT CMAKE_CROSSCOMPILING)
  # Compiling on an Android device itself, e.g. via Termux.
  # Lua CMake does `find_library(LIBM m)` but it cannot find it there on Android.
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(LIBM /system/lib64/libm.so)
  else()
    set(LIBM /system/lib/libm.so)
  endif()
endif()

include(FetchContent)
FetchContent_Declare_ExcludeFromAll(Lua
    URL https://github.com/walterschell/Lua/archive/3ed55a56eaa05c9221f40b3c07d0e908eb1067b0.tar.gz
    URL_HASH MD5=77907b8209f77c65cb681a5012f2d804
)
FetchContent_MakeAvailable_ExcludeFromAll(Lua)

# PS3-only Lua startup tracing. Patch the fetched Lua 5.4.7 source in the
# build tree so we can identify which stage of lua_newstate() stops.
if(CMAKE_SYSTEM_NAME STREQUAL "PS3")
  set(_ps3_lua_state_source "${lua_SOURCE_DIR}/lua-5.4.7/src/lstate.c")
  if(NOT EXISTS "${_ps3_lua_state_source}")
    message(FATAL_ERROR "Could not find fetched Lua source: ${_ps3_lua_state_source}")
  endif()

  file(READ "${_ps3_lua_state_source}" _ps3_lua_state_content)
  if(NOT _ps3_lua_state_content MATCHES "PS3 Lua startup trace")
    string(REPLACE
      "#include \"lstring.h\""
      "#include \"lstring.h\"\n\n#ifdef __PS3__\nextern void PS3LuaRawBootLogNoSync(const char *message);\n#define PS3_LUA_CORE_TRACE(message) PS3LuaRawBootLogNoSync(message)\n#else\n#define PS3_LUA_CORE_TRACE(message) ((void)0)\n#endif /* PS3 Lua startup trace */"
      _ps3_lua_state_content "${_ps3_lua_state_content}")

    string(REPLACE
      "static void f_luaopen (lua_State *L, void *ud) {\n  global_State *g = G(L);\n  UNUSED(ud);\n  stack_init(L, L);  /* init stack */\n  init_registry(L, g);\n  luaS_init(L);\n  luaT_init(L);\n  luaX_init(L);\n  g->gcstp = 0;  /* allow gc */\n  setnilvalue(&g->nilvalue);  /* now state is complete */\n  luai_userstateopen(L);\n}"
      "static void f_luaopen (lua_State *L, void *ud) {\n  global_State *g = G(L);\n  UNUSED(ud);\n  PS3_LUA_CORE_TRACE(\"Lua core: f_luaopen entered\");\n  stack_init(L, L);  /* initialize stack */\n  PS3_LUA_CORE_TRACE(\"Lua core: stack_init complete\");\n  init_registry(L, g);\n  PS3_LUA_CORE_TRACE(\"Lua core: init_registry complete\");\n  luaS_init(L);\n  PS3_LUA_CORE_TRACE(\"Lua core: luaS_init complete\");\n  luaT_init(L);\n  PS3_LUA_CORE_TRACE(\"Lua core: luaT_init complete\");\n  luaX_init(L);\n  PS3_LUA_CORE_TRACE(\"Lua core: luaX_init complete\");\n  g->gcstp = 0;  /* allow gc */\n  PS3_LUA_CORE_TRACE(\"Lua core: GC enabled\");\n  setnilvalue(&g->nilvalue);  /* now state is complete */\n  PS3_LUA_CORE_TRACE(\"Lua core: state marked complete\");\n  luai_userstateopen(L);\n  PS3_LUA_CORE_TRACE(\"Lua core: f_luaopen returning\");\n}"
      _ps3_lua_state_content "${_ps3_lua_state_content}")

    string(REPLACE
      "  if (luaD_rawrunprotected(L, f_luaopen, NULL) != LUA_OK) {\n    /* memory allocation error: free partial state */\n    close_state(L);\n    L = NULL;\n  }\n  return L;"
      "  PS3_LUA_CORE_TRACE(\"Lua core: protected f_luaopen begin\");\n  const int init_status = luaD_rawrunprotected(L, f_luaopen, NULL);\n  PS3_LUA_CORE_TRACE(init_status == LUA_OK ? \"Lua core: protected f_luaopen returned LUA_OK\" : \"Lua core: protected f_luaopen returned error\");\n  if (init_status != LUA_OK) {\n    /* memory allocation error: free partial state */\n    close_state(L);\n    L = NULL;\n    PS3_LUA_CORE_TRACE(\"Lua core: partial state cleanup complete\");\n  }\n  PS3_LUA_CORE_TRACE(L != NULL ? \"Lua core: lua_newstate returning valid state\" : \"Lua core: lua_newstate returning null state\");\n  return L;"
      _ps3_lua_state_content "${_ps3_lua_state_content}")

    if(NOT _ps3_lua_state_content MATCHES "Lua core: luaX_init complete" OR
       NOT _ps3_lua_state_content MATCHES "Lua core: protected f_luaopen begin")
      message(FATAL_ERROR "Failed to apply PS3 Lua startup trace patch")
    endif()

    file(WRITE "${_ps3_lua_state_source}" "${_ps3_lua_state_content}")
  endif()
  # Trace the protected-call boundary separately from f_luaopen. The last
  # hardware log reached the end of f_luaopen but not the caller.
  set(_ps3_lua_do_source "\${lua_SOURCE_DIR}/lua-5.4.7/src/ldo.c")
  if(NOT EXISTS "\${_ps3_lua_do_source}")
    message(FATAL_ERROR "Could not find fetched Lua source: \${_ps3_lua_do_source}")
  endif()

  file(READ "\${_ps3_lua_do_source}" _ps3_lua_do_content)
  if(NOT _ps3_lua_do_content MATCHES "Lua core: rawrunprotected returning")
    string(REPLACE
      "#include \"ldo.h\""
      "#include \"ldo.h\"\n\n#ifdef __PS3__\nextern void PS3LuaRawBootLogNoSync(const char *message);\n#define PS3_LUA_CORE_TRACE(message) PS3LuaRawBootLogNoSync(message)\n#else\n#define PS3_LUA_CORE_TRACE(message) ((void)0)\n#endif /* PS3 protected-call trace */"
      _ps3_lua_do_content "\${_ps3_lua_do_content}")

    string(REPLACE
      "int luaD_rawrunprotected (lua_State *L, Pfunc f, void *ud) {\n  l_uint32 oldnCcalls = L->nCcalls;\n  struct lua_longjmp lj;\n  lj.status = LUA_OK;\n  lj.previous = L->errorJmp;  /* chain new error handler */\n  L->errorJmp = &lj;\n  LUAI_TRY(L, &lj,\n    (*f)(L, ud);\n  );\n  L->errorJmp = lj.previous;  /* restore old error handler */\n  L->nCcalls = oldnCcalls;\n  return lj.status;\n}"
      "int luaD_rawrunprotected (lua_State *L, Pfunc f, void *ud) {\n  PS3_LUA_CORE_TRACE(\"Lua core: rawrunprotected entered\");\n  l_uint32 oldnCcalls = L->nCcalls;\n  struct lua_longjmp lj;\n  lj.status = LUA_OK;\n  lj.previous = L->errorJmp;  /* chain new error handler */\n  L->errorJmp = &lj;\n  PS3_LUA_CORE_TRACE(\"Lua core: rawrunprotected handler installed\");\n  LUAI_TRY(L, &lj,\n    PS3_LUA_CORE_TRACE(\"Lua core: protected callback body entered\");\n    (*f)(L, ud);\n    PS3_LUA_CORE_TRACE(\"Lua core: protected callback returned normally\");\n  );\n  PS3_LUA_CORE_TRACE(\"Lua core: LUAI_TRY completed\");\n  L->errorJmp = lj.previous;  /* restore old error handler */\n  PS3_LUA_CORE_TRACE(\"Lua core: error handler restored\");\n  L->nCcalls = oldnCcalls;\n  PS3_LUA_CORE_TRACE(\"Lua core: C-call depth restored\");\n  PS3_LUA_CORE_TRACE(lj.status == LUA_OK ? \"Lua core: rawrunprotected returning LUA_OK\" : \"Lua core: rawrunprotected returning error\");\n  return lj.status;\n}";
    if(NOT _ps3_lua_do_content MATCHES "Lua core: protected callback returned normally" OR
       NOT _ps3_lua_do_content MATCHES "Lua core: LUAI_TRY completed")
      message(FATAL_ERROR "Failed to apply PS3 Lua protected-call trace patch")
    endif()

    file(WRITE "\${_ps3_lua_do_source}" "\${_ps3_lua_do_content}")
  endif()

endif()

# Needed for sol2
set(LUA_VERSION_STRING "5.4.7" PARENT_SCOPE)

if(CMAKE_SYSTEM_NAME MATCHES "Darwin" AND DARWIN_MAJOR_VERSION VERSION_EQUAL 8)
  # We need legacy-support from MacPorts for:
  # localtime_r gmtime_r
  find_package(MacportsLegacySupport REQUIRED)
  target_link_libraries(lua_static PRIVATE MacportsLegacySupport::MacportsLegacySupport)
elseif(EMSCRIPTEN)
  # Enable pthread support for Emscripten to match SDL2's USE_PTHREADS=1
  target_compile_options(lua_static PUBLIC -pthread)
  target_link_options(lua_static PUBLIC -pthread)
elseif(TARGET_PLATFORM STREQUAL "dos")
  target_compile_definitions(lua_static PUBLIC -DLUA_USE_C89)
elseif(ANDROID AND ("${ANDROID_ABI}" STREQUAL "armeabi-v7a" OR "${ANDROID_ABI}" STREQUAL "x86"))
  target_compile_definitions(lua_static PUBLIC -DLUA_USE_C89)
elseif(NINTENDO_3DS OR VITA OR NINTENDO_SWITCH OR NXDK)
  target_compile_definitions(lua_static PUBLIC -DLUA_USE_C89)
elseif(CMAKE_SYSTEM_NAME STREQUAL "PS3")
  # The PSL1GHT PPU toolchain's limits headers do not expose LLONG_MAX.
  # Lua's C89-number mode uses 'long', which is 64-bit on the PPU ABI.
  # PUBLIC keeps sol2 and all Lua consumers on the same integer ABI.
  target_compile_definitions(lua_static PUBLIC -DLUA_USE_C89)
elseif(IOS)
  target_compile_definitions(lua_static PUBLIC -DLUA_USE_IOS)
endif()
