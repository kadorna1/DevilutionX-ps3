#pragma once

#include <expected>
#include <string_view>

#include <function_ref.hpp>
#include <sol/state_view.hpp>

namespace devilution {

void LuaInitialize();
void LuaReloadActiveMods();
void LuaShutdown();

sol::state_view &GetLuaState();
sol::environment CreateLuaSandbox();
sol::object SafeCallResult(sol::protected_function_result result, bool optional);
sol::table *GetLuaEvents();

/** Adds a handler to be called when mods status changes after the initial startup. */
void AddModsChangedHandler(tl::function_ref<void()> callback);

} // namespace devilution
