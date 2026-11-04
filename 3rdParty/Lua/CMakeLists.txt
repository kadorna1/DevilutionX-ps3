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
  # Upgrade cached Lua state-construction tracing. Build-tree caches can
  # already contain the initial PS3 marker while predating the post-call
  # checkpoints below; do not use the top-level marker as an upgrade guard.
  file(READ "${_ps3_lua_state_source}" _ps3_lua_state_content)
  if(NOT _ps3_lua_state_content MATCHES "Lua core: protected f_luaopen returned LUA_OK" OR
     NOT _ps3_lua_state_content MATCHES "Lua core: lua_newstate returning valid state")
    # Handle both pristine and previously instrumented call sites.
    string(REPLACE
      [=[  if (luaD_rawrunprotected(L, f_luaopen, NULL) != LUA_OK) {]=]
      [=[  const int init_status = luaD_rawrunprotected(L, f_luaopen, NULL);
  if (init_status != LUA_OK) {]=]
      _ps3_lua_state_content "${_ps3_lua_state_content}")

    if(NOT _ps3_lua_state_content MATCHES "Lua core: protected f_luaopen returned LUA_OK")
      string(REPLACE
        [=[  const int init_status = luaD_rawrunprotected(L, f_luaopen, NULL);]=]
        [=[  const int init_status = luaD_rawrunprotected(L, f_luaopen, NULL);
  PS3_LUA_CORE_TRACE(init_status == LUA_OK ? "Lua core: protected f_luaopen returned LUA_OK" : "Lua core: protected f_luaopen returned error");]=]
        _ps3_lua_state_content "${_ps3_lua_state_content}")
    endif()

    # Report cleanup and the final state pointer before lua_newstate returns.
    string(REPLACE
      [=[    L = NULL;
  }
  return L;]=]
      [=[    L = NULL;
    PS3_LUA_CORE_TRACE("Lua core: partial state cleanup complete");
  }
  PS3_LUA_CORE_TRACE(L != NULL ? "Lua core: lua_newstate returning valid state" : "Lua core: lua_newstate returning null state");
  return L;]=]
      _ps3_lua_state_content "${_ps3_lua_state_content}")

    if(NOT _ps3_lua_state_content MATCHES "Lua core: protected f_luaopen returned LUA_OK" OR
       NOT _ps3_lua_state_content MATCHES "Lua core: lua_newstate returning valid state")
      message(FATAL_ERROR "Failed to upgrade cached PS3 Lua state-construction tracing")
    endif()
    file(WRITE "${_ps3_lua_state_source}" "${_ps3_lua_state_content}")
  endif()

  # FetchContent's restored source tree can be newer than the cached object
  # even when the source was upgraded during configuration. Touch this one
  # generated source after its instrumentation is validated so Ninja must
  # rebuild lstate.c and the linked Lua archive for this revision.
  file(TOUCH "${_ps3_lua_state_source}")

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
  file(TOUCH "${_ps3_lua_do_source}")
  endif()



  # Apply the protected-call diagnostics to pristine sources and older cached
  # build trees (including the previous one-argument status logger version).
  file(READ "${_ps3_lua_do_source}" _ps3_lua_do_content)

  # Newlib's PS3 setjmp.S saves VRSAVE and v20-v31 even though Lua's PPU
  # compile flags include -mno-altivec. Without __ALTIVEC__ while including
  # <setjmp.h>, machine/setjmp.h allocates only 32 doubles (256 bytes), but
  # the linked assembly writes a 448-byte context after alignment. That
  # overruns lua_longjmp::status and the caller's stack frame. Select the
  # AltiVec-sized jmp_buf layout only for this header; do not enable VMX
  # code generation in Lua itself.
  if(NOT _ps3_lua_do_content MATCHES "PS3 Newlib AltiVec jmp_buf sizing")
    string(REPLACE
      [=[#include <setjmp.h>]=]
      [=[#ifdef __PS3__
#ifndef __ALTIVEC__
#define PS3_LUA_UNDEFINE_ALTIVEC_AFTER_SETJMP_H 1
#define __ALTIVEC__ 1
#endif
#endif
#include <setjmp.h>
#ifdef PS3_LUA_UNDEFINE_ALTIVEC_AFTER_SETJMP_H
#undef __ALTIVEC__
#undef PS3_LUA_UNDEFINE_ALTIVEC_AFTER_SETJMP_H
#endif
#ifdef __PS3__
typedef char ps3_lua_jmp_buf_must_fit_newlib_vmx_context[
    sizeof(jmp_buf) >= 464 ? 1 : -1];
#endif
/* PS3 Newlib AltiVec jmp_buf sizing */]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
    if(NOT _ps3_lua_do_content MATCHES "ps3_lua_jmp_buf_must_fit_newlib_vmx_context")
      message(FATAL_ERROR "Failed to apply PS3 Newlib AltiVec jmp_buf sizing fix")
    endif()
  endif()

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

  # Snapshot status at each boundary after the callback has completed.
  if(NOT _ps3_lua_do_content MATCHES "PS3 Lua status checkpoint after LUAI_TRY")
    string(REPLACE
      [=[  PS3_LUA_CORE_TRACE("Lua core: LUAI_TRY completed");]=]
      [=[  PS3_LUA_CORE_TRACE_STATUS(lj.status, -3); /* PS3 Lua status checkpoint after LUAI_TRY */
  PS3_LUA_CORE_TRACE("Lua core: LUAI_TRY completed");]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
    string(REPLACE
      [=[  PS3_LUA_CORE_TRACE("Lua core: error handler restored");]=]
      [=[  PS3_LUA_CORE_TRACE_STATUS(lj.status, -4); /* PS3 Lua status checkpoint after handler restore */
  PS3_LUA_CORE_TRACE("Lua core: error handler restored");]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
    string(REPLACE
      [=[  PS3_LUA_CORE_TRACE("Lua core: C-call depth restored");]=]
      [=[  PS3_LUA_CORE_TRACE_STATUS(lj.status, -5); /* PS3 Lua status checkpoint after C-call restore */
  PS3_LUA_CORE_TRACE("Lua core: C-call depth restored");]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
  endif()

  # Snapshot the status at the first instruction after setjmp and
  # immediately after the callback, to separate jump-buffer setup from
  # code executed by f_luaopen (including PS3 diagnostic calls).
  if(NOT _ps3_lua_do_content MATCHES "PS3 Lua status checkpoint at callback entry")
    string(REPLACE
      [=[    PS3_LUA_CORE_TRACE("Lua core: protected callback body entered");]=]
      [=[    PS3_LUA_CORE_TRACE_STATUS(lj.status, -6); /* PS3 Lua status checkpoint at callback entry */
    PS3_LUA_CORE_TRACE("Lua core: protected callback body entered");]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
  endif()
  if(NOT _ps3_lua_do_content MATCHES "PS3 Lua status checkpoint at callback return")
    string(REPLACE
      [=[    (*f)(L, ud);]=]
      [=[    (*f)(L, ud);
    PS3_LUA_CORE_TRACE_STATUS(lj.status, -7); /* PS3 Lua status checkpoint at callback return */]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
  endif()

  # Do not call the LV2 logger between setting lj.status and setjmp().
  # The first post-setjmp probe at -6 determines whether the jump setup changed it.
  string(REPLACE
    [=[  PS3_LUA_CORE_TRACE_STATUS(lj.status, -2); /* initial status; -2 identifies this probe */]=]
    [=[]=]
    _ps3_lua_do_content "${_ps3_lua_do_content}")
  string(REPLACE
    [=[  PS3_LUA_CORE_TRACE("Lua core: rawrunprotected handler installed");]=]
    [=[]=]
    _ps3_lua_do_content "${_ps3_lua_do_content}")

  # Keep exactly one final status report. Older cached sources accumulated
  # duplicate probes as CMake reconfigured the FetchContent build tree.
  string(REPLACE
    [=[  PS3_LUA_CORE_TRACE_STATUS(lj.status, ps3_callback_returned);]=]
    [=[]=]
    _ps3_lua_do_content "${_ps3_lua_do_content}")
  string(REPLACE
    [=[#ifdef __PS3__
#endif
]=]
    [=[]=]
    _ps3_lua_do_content "${_ps3_lua_do_content}")
  string(REPLACE
    [=[  return lj.status;]=]
    [=[  PS3_LUA_CORE_TRACE_STATUS(lj.status, ps3_callback_returned);
  return lj.status;]=]
    _ps3_lua_do_content "${_ps3_lua_do_content}")

  # Capture the initialized status before setjmp, then snapshot the value
  # immediately after setjmp returns. On PPU, the observed status can be
  # clobbered across the jump-buffer setup even though the callback runs.
  # Restore LUA_OK only on the initial (setjmp == 0) path; a real longjmp
  # skips this block and preserves the error code written by luaD_throw.
  if(NOT _ps3_lua_do_content MATCHES "ps3_status_before_setjmp")
    string(REPLACE
      [=[  struct lua_longjmp lj;]=]
      [=[  struct lua_longjmp lj;
#ifdef __PS3__
  volatile int ps3_status_before_setjmp = LUA_OK;
  volatile int ps3_status_after_setjmp = LUA_OK;
#endif]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")

    string(REPLACE
      [=[  lj.status = LUA_OK;]=]
      [=[  lj.status = LUA_OK;
#ifdef __PS3__
  ps3_status_before_setjmp = lj.status;
#endif]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")

    string(REPLACE
      [=[    PS3_LUA_CORE_TRACE_STATUS(lj.status, -6); /* PS3 Lua status checkpoint at callback entry */]=]
      [=[    ps3_status_after_setjmp = lj.status;
    PS3_LUA_CORE_TRACE_STATUS((int)sizeof(jmp_buf), -10); /* effective Newlib jmp_buf size */
    PS3_LUA_CORE_TRACE_STATUS(ps3_status_before_setjmp, -8); /* status before setjmp */
    PS3_LUA_CORE_TRACE_STATUS(ps3_status_after_setjmp, -6); /* PS3 Lua status checkpoint at callback entry; immediately after setjmp */]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
    if(NOT _ps3_lua_do_content MATCHES "ps3_status_before_setjmp = lj.status" OR
       NOT _ps3_lua_do_content MATCHES "ps3_status_after_setjmp = lj.status")
      message(FATAL_ERROR "Failed to apply PS3 Lua post-setjmp status diagnostics")
    endif()
  endif()

  # Remove the previous workaround which hid the setjmp buffer overrun.
  string(REPLACE
    [=[    lj.status = LUA_OK; /* re-establish success status after PPU setjmp */
    PS3_LUA_CORE_TRACE_STATUS(lj.status, -9); /* status after normalization */]=]
    [=[]=]
    _ps3_lua_do_content "${_ps3_lua_do_content}")

  # Record every explicit Lua throw that writes to the active protected call.
  if(NOT _ps3_lua_do_content MATCHES "PS3_LUA_CORE_TRACE_STATUS\\(errcode, -1\\)")
    string(REPLACE
      [=[    L->errorJmp->status = errcode;  /* set status */]=]
      [=[    L->errorJmp->status = errcode;  /* set status */
#ifdef __PS3__
    PS3_LUA_CORE_TRACE_STATUS(errcode, -1); /* -1 identifies a luaD_throw write */
#endif]=]
      _ps3_lua_do_content "${_ps3_lua_do_content}")
  endif()

  if(NOT _ps3_lua_do_content MATCHES "PS3 Lua status checkpoint at callback entry" OR
     NOT _ps3_lua_do_content MATCHES "PS3 Lua status checkpoint at callback return" OR
     NOT _ps3_lua_do_content MATCHES "errcode, -1")
    message(FATAL_ERROR "Failed to apply PS3 Lua protected-call status probes")
  endif()

  file(WRITE "${_ps3_lua_do_source}" "${_ps3_lua_do_content}")
  file(TOUCH "${_ps3_lua_do_source}")

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
