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
  # Trace and harden the protected-call boundary separately from f_luaopen.
  # CMake bracket arguments keep the C source fragments literal.
  set(_ps3_lua_do_source "${lua_SOURCE_DIR}/lua-5.4.7/src/ldo.c")
  if(NOT EXISTS "${_ps3_lua_do_source}")
    message(FATAL_ERROR "Could not find fetched Lua source: ${_ps3_lua_do_source}")
  endif()

  file(READ "${_ps3_lua_do_source}" _ps3_lua_do_content)
  if(NOT _ps3_lua_do_content MATCHES "PS3 protected-call trace")
    # Add the trace hook on an untouched Lua source tree.
    if(NOT _ps3_lua_do_content MATCHES "PS3 protected-call trace")
      string(REPLACE
        [=[#include "ldo.h"]=]
        [=[#include "ldo.h"

#ifdef __PS3__
extern void PS3LuaRawBootLogNoSync(const char *message);
#define PS3_LUA_CORE_TRACE(message) PS3LuaRawBootLogNoSync(message)
#else
#define PS3_LUA_CORE_TRACE(message) ((void)0)
#endif /* PS3 protected-call trace */]=]
        _ps3_lua_do_content "${_ps3_lua_do_content}")
    endif()

    # Extend both a freshly patched tree and a build tree restored from the
    # previous CI cache with a numeric status logger.
    string(REPLACE
      [=[#define PS3_LUA_CORE_TRACE(message) PS3LuaRawBootLogNoSync(message)]=]
      [=[#define PS3_LUA_CORE_TRACE(message) PS3LuaRawBootLogNoSync(message)
extern void PS3LuaRawBootLogStatus(int status);
#define PS3_LUA_CORE_TRACE_STATUS(status) PS3LuaRawBootLogStatus(status)]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
    string(REPLACE
      [=[#define PS3_LUA_CORE_TRACE(message) ((void)0)]=]
      [=[#define PS3_LUA_CORE_TRACE(message) ((void)0)
#define PS3_LUA_CORE_TRACE_STATUS(status) ((void)0)]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")

    # Add the existing boundary checkpoints only when this is an unpatched
    # upstream file. A cached instrumented file already contains them.
    if(NOT _ps3_lua_do_content MATCHES "Lua core: rawrunprotected handler installed")
      string(REPLACE
        [=[  L->errorJmp = &lj;]=]
        [=[  L->errorJmp = &lj;
  PS3_LUA_CORE_TRACE("Lua core: rawrunprotected handler installed");]=]
        _ps3_lua_do_content "${_ps3_lua_do_content}")

      string(REPLACE
        [=[    (*f)(L, ud);]=]
        [=[    PS3_LUA_CORE_TRACE("Lua core: protected callback body entered");
    (*f)(L, ud);
    PS3_LUA_CORE_TRACE("Lua core: protected callback returned normally");]=]
        _ps3_lua_do_content "${_ps3_lua_do_content}")

      string(REPLACE
        [=[  L->errorJmp = lj.previous;  /* restore old error handler */]=]
        [=[  PS3_LUA_CORE_TRACE("Lua core: LUAI_TRY completed");
  L->errorJmp = lj.previous;  /* restore old error handler */
  PS3_LUA_CORE_TRACE("Lua core: error handler restored");]=]
        _ps3_lua_do_content "${_ps3_lua_do_content}")

      string(REPLACE
        [=[  L->nCcalls = oldnCcalls;]=]
        [=[  L->nCcalls = oldnCcalls;
  PS3_LUA_CORE_TRACE("Lua core: C-call depth restored");]=]
        _ps3_lua_do_content "${_ps3_lua_do_content}")
    endif()

    # A normal return from f must be LUA_OK. Keep a volatile flag because
    # the function also has a setjmp/longjmp path; a real Lua throw skips the
    # assignment below and retains lj.status.
    string(REPLACE
      [=[  struct lua_longjmp lj;]=]
      [=[  struct lua_longjmp lj;
#ifdef __PS3__
  volatile int ps3_callback_returned = 0; /* PS3 protected-call status normalization */
  int ps3_final_status;
#endif]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")

    string(REPLACE
      [=[    (*f)(L, ud);]=]
      [=[    (*f)(L, ud);
#ifdef __PS3__
    ps3_callback_returned = 1;
#endif]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")

    # Preserve a numeric report of the original lj.status, then derive the
    # return value from whether the callback actually completed normally.
    if(NOT _ps3_lua_do_content MATCHES "Lua core: rawrunprotected returning")
      string(REPLACE
        [=[  return lj.status;]=]
        [=[  PS3_LUA_CORE_TRACE(lj.status == LUA_OK ? "Lua core: rawrunprotected returning LUA_OK" : "Lua core: rawrunprotected returning error");
  return lj.status;]=]
        _ps3_lua_do_content "${_ps3_lua_do_content}")
    endif()

    string(REPLACE
      [=[  PS3_LUA_CORE_TRACE(lj.status == LUA_OK ? "Lua core: rawrunprotected returning LUA_OK" : "Lua core: rawrunprotected returning error");
  return lj.status;]=]
      [=[#ifdef __PS3__
  PS3_LUA_CORE_TRACE_STATUS(lj.status);
  ps3_final_status = ps3_callback_returned ? LUA_OK : lj.status;
  return ps3_final_status;
#else
  return lj.status;
#endif]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")

    if(NOT _ps3_lua_do_content MATCHES "ps3_callback_returned = 1" OR
       NOT _ps3_lua_do_content MATCHES "PS3_LUA_CORE_TRACE_STATUS")
      message(FATAL_ERROR "Failed to apply PS3 Lua protected-call status normalization")
    endif()

    file(WRITE "${_ps3_lua_do_source}" "${_ps3_lua_do_content}")
  endif()



  # Apply the protected-call diagnostics to pristine sources and older cached
  # build trees (including the previous one-argument status logger version).
  file(READ "${_ps3_lua_do_source}" _ps3_lua_do_content)

  # Keep the C and C++ sides of the diagnostic function signature identical.
  string(REPLACE
    [=[extern void PS3LuaRawBootLogStatus(int status);
#define PS3_LUA_CORE_TRACE_STATUS(status) PS3LuaRawBootLogStatus(status)]=]
    [=[extern void PS3LuaRawBootLogStatus(int status, int callbackReturned);
#define PS3_LUA_CORE_TRACE_STATUS(status, callbackReturned) PS3LuaRawBootLogStatus(status, callbackReturned)]=]
    _ps3_lua_do_content "${_ps3_lua_do_content}")
  if(NOT _ps3_lua_do_content MATCHES "PS3_LUA_CORE_TRACE_STATUS\\(status, callbackReturned\\)")
    string(REPLACE
      [=[#define PS3_LUA_CORE_TRACE(message) PS3LuaRawBootLogNoSync(message)]=]
      [=[#define PS3_LUA_CORE_TRACE(message) PS3LuaRawBootLogNoSync(message)
extern void PS3LuaRawBootLogStatus(int status, int callbackReturned);
#define PS3_LUA_CORE_TRACE_STATUS(status, callbackReturned) PS3LuaRawBootLogStatus(status, callbackReturned)]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
    string(REPLACE
      [=[#define PS3_LUA_CORE_TRACE(message) ((void)0)]=]
      [=[#define PS3_LUA_CORE_TRACE(message) ((void)0)
#define PS3_LUA_CORE_TRACE_STATUS(status, callbackReturned) ((void)0)]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
  endif()
  string(REPLACE
    [=[#define PS3_LUA_CORE_TRACE_STATUS(status) ((void)0)]=]
    [=[#define PS3_LUA_CORE_TRACE_STATUS(status, callbackReturned) ((void)0)]=]
    _ps3_lua_do_content "${_ps3_lua_do_content}")

  # Mark the actual compile-time exception backend selected by Lua.
  # Anchor this to luaD_rawrunprotected's status initialization, not to a
  # diagnostic line that may differ across restored/incrementally patched trees.
  if(NOT _ps3_lua_do_content MATCHES "PS3 protected backend is ISO C longjmp")
    string(REPLACE
      [=[  lj.status = LUA_OK;]=]
      [=[  lj.status = LUA_OK;
#if defined(__cplusplus) && !defined(LUA_USE_LONGJMP)
  PS3_LUA_CORE_TRACE("Lua core: protected backend is C++ exceptions");
#elif defined(LUA_USE_POSIX)
  PS3_LUA_CORE_TRACE("Lua core: protected backend is POSIX longjmp");
#else
  PS3_LUA_CORE_TRACE("Lua core: protected backend is ISO C longjmp");
#endif]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
  endif()

  # Older cached source files predate the callback-return flag.
  if(NOT _ps3_lua_do_content MATCHES "ps3_callback_returned")
    string(REPLACE
      [=[  struct lua_longjmp lj;]=]
      [=[  struct lua_longjmp lj;
#ifdef __PS3__
  volatile int ps3_callback_returned = 0;
#endif]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
    string(REPLACE
      [=[    (*f)(L, ud);]=]
      [=[    (*f)(L, ud);
#ifdef __PS3__
    ps3_callback_returned = 1;
#endif]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
  endif()

  # Remove the temporary status override added during diagnosis. The VM must
  # return Lua's real status; a successful callback should naturally return 0.
  string(REPLACE
    [=[  volatile int ps3_callback_returned = 0; /* PS3 protected-call status normalization */
  int ps3_final_status;]=]
    [=[  volatile int ps3_callback_returned = 0;]=]
    _ps3_lua_do_content "${_ps3_lua_do_content}")
  string(REPLACE
    [=[#ifdef __PS3__
  PS3_LUA_CORE_TRACE_STATUS(lj.status);
  ps3_final_status = ps3_callback_returned ? LUA_OK : lj.status;
  return ps3_final_status;
#else
  return lj.status;
#endif]=]
    [=[#ifdef __PS3__
  PS3_LUA_CORE_TRACE_STATUS(lj.status, ps3_callback_returned);
#endif
  return lj.status;]=]
    _ps3_lua_do_content "${_ps3_lua_do_content}")

  # Upgrade older trace builds that returned lj.status without printing it.
  if(NOT _ps3_lua_do_content MATCHES "PS3_LUA_CORE_TRACE_STATUS\(lj.status, ps3_callback_returned\)")
    string(REPLACE
      [=[  return lj.status;]=]
      [=[  PS3_LUA_CORE_TRACE_STATUS(lj.status, ps3_callback_returned);
  return lj.status;]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
  endif()

  if(NOT _ps3_lua_do_content MATCHES "PS3 protected backend is ISO C longjmp" OR
     NOT _ps3_lua_do_content MATCHES "PS3_LUA_CORE_TRACE_STATUS")
    message(FATAL_ERROR "Failed to apply PS3 Lua protected-call backend diagnostics")
  endif()

  file(WRITE "${_ps3_lua_do_source}" "${_ps3_lua_do_content}")

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
  # LUA_USE_LONGJMP explicitly disables Lua's C++ catch-all path for PS3.
  # PUBLIC keeps sol2 and all Lua consumers on the same integer ABI.
  target_compile_definitions(lua_static PUBLIC -DLUA_USE_C89 -DLUA_USE_LONGJMP)
elseif(IOS)
  target_compile_definitions(lua_static PUBLIC -DLUA_USE_IOS)
endif()
