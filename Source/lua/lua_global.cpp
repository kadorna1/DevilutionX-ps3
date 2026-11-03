#include "lua/lua_global.hpp"

#include <algorithm>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ankerl/unordered_dense.h>
#include <function_ref.hpp>
#include <sol/bytecode.hpp>
#include <sol/debug.hpp>
#include <sol/environment.hpp>
#include <sol/forward.hpp>
#include <sol/function_types_templated.hpp>
#include <sol/protected_function.hpp>
#include <sol/stack_push.hpp>
#include <sol/state.hpp>
#include <sol/table.hpp>
#include <sol/types.hpp>

#include <config.h>

#include "appfat.h"
#include "effects.h"
#include "engine/assets.hpp"
#include "lua/lua_event.hpp"
#include "lua/modules/audio.hpp"
#include "lua/modules/floatingnumbers.hpp"
#include "lua/modules/hellfire.hpp"
#include "lua/modules/i18n.hpp"
#include "lua/modules/items.hpp"
#include "lua/modules/log.hpp"
#include "lua/modules/monsters.hpp"
#include "lua/modules/player.hpp"
#include "lua/modules/render.hpp"
#include "lua/modules/system.hpp"
#include "lua/modules/towners.hpp"
#include "options.h"
#include "plrmsg.h"
#include "stores.h"
#include "utils/console.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"

#ifdef _DEBUG
#include "lua/modules/dev.hpp"
#include "lua/repl.hpp"
#endif

namespace devilution {

#ifdef __PS3__
#define PS3_LUA_BOOT_LOG(message) LogInfo("PS3 boot: Lua: {}", message)
#else
#define PS3_LUA_BOOT_LOG(message) ((void)0)
#endif

namespace {

void LuaPanic(const std::optional<std::string> &message)
{
	LogError("Lua is in a panic state and will now abort() the application:\n{}",
	    message.value_or("unknown error"));
}

struct LuaState {
	sol::state sol;
	sol::table commonPackages;
	ankerl::unordered_dense::segmented_map<std::string, sol::bytecode> compiledScripts;
	sol::environment sandbox;
	sol::table events;

	LuaState()
	    : sol(sol::c_call<decltype(&LuaPanic), &LuaPanic>)
	{
	}
};

std::optional<LuaState> CurrentLuaState;

std::vector<tl::function_ref<void()>> IsModChangeHandlers;

// A Lua function that we use to generate a `require` implementation.
constexpr std::string_view RequireGenSrc = R"lua(
function requireGen(env, loaded, loadFn)
  return function(packageName)
      local p = loaded[packageName]
      if p == nil then
          local loader = loadFn(packageName)
          if type(loader) == "string" then
            error(loader)
          end
          setEnvironment(env, loader)
          p = loader(packageName)
          loaded[packageName] = p
      end
      return p
  end
end
)lua";

sol::object LuaLoadScriptFromAssets(std::string_view packageName)
{
	PS3_LUA_BOOT_LOG(StrCat("script asset loading begin: ", packageName));
	if (!CurrentLuaState.has_value()) {
		app_fatal("Lua state is not initialized");
	}
	LuaState &luaState = *CurrentLuaState;
	constexpr std::string_view PathPrefix = "lua\\";
	constexpr std::string_view PathSuffix = ".lua";
	std::string path;
	path.reserve(PathPrefix.size() + packageName.size() + PathSuffix.size());
	StrAppend(path, PathPrefix, packageName, PathSuffix);
	std::replace(path.begin() + PathPrefix.size(), path.end() - PathSuffix.size(), '.', '\\');

	auto iter = luaState.compiledScripts.find(path);
	if (iter != luaState.compiledScripts.end()) {
		return luaState.sol.load(iter->second.as_string_view(), path, sol::load_mode::binary);
	}

	std::expected<AssetData, std::string> assetData = LoadIntegralAsset(path);
	if (!assetData.has_value()) {
		PS3_LUA_BOOT_LOG(StrCat("script asset loading failed: ", path, ": ", assetData.error()));
		sol::stack::push(luaState.sol.lua_state(), assetData.error());
		return sol::stack_object(luaState.sol.lua_state(), -1);
	}
	const sol::load_result result = luaState.sol.load(std::string_view(*assetData), path, sol::load_mode::text);
	if (!result.valid()) {
		sol::stack::push(luaState.sol.lua_state(),
		    StrCat("Lua error when loading ", path, ": ", result.get<std::string>()));
		return sol::stack_object(luaState.sol.lua_state(), -1);
	}
	const sol::function fn = result;
	PS3_LUA_BOOT_LOG(StrCat("script bytecode compilation begin: ", path));
	luaState.compiledScripts[path] = fn.dump();
	PS3_LUA_BOOT_LOG(StrCat("script asset loading complete: ", path));
	return result;
}

int LuaPrint(lua_State *state)
{
	const int n = lua_gettop(state);
	for (int i = 1; i <= n; i++) {
		size_t l;
		const char *s = luaL_tolstring(state, i, &l);
		if (i > 1)
			printInConsole("\t");
		printInConsole(std::string_view(s, l));
		lua_pop(state, 1);
	}
	printNewlineInConsole();
	return 0;
}

void LuaWarn(void * /*userData*/, const char *message, int continued)
{
	static std::string warnBuffer;
	warnBuffer.append(message);
	if (continued != 0)
		return;
	LogWarn("{}", warnBuffer);
	warnBuffer.clear();
}

sol::object RunScript(std::optional<sol::environment> env, std::string_view packageName, bool optional)
{
	const sol::object result = LuaLoadScriptFromAssets(packageName);
	// We return a string on error:
	if (result.get_type() == sol::type::string) {
		if (!optional)
			app_fatal(result.as<std::string>());
		LogInfo("{}", result.as<std::string>());
		return sol::lua_nil;
	}
	auto fn = result.as<sol::protected_function>();
	if (env.has_value()) {
		sol::set_environment(*env, fn);
	}
	return SafeCallResult(fn(), optional);
}

} // namespace

void Sol2DebugPrintStack(lua_State *state)
{
	LogDebug("{}", sol::detail::debug::dump_types(state));
}

void Sol2DebugPrintSection(const std::string &message, lua_State *state)
{
	LogDebug("-- {} -- [ {} ]", message, sol::detail::debug::dump_types(state));
}

sol::environment CreateLuaSandbox()
{
	sol::state &lua = CurrentLuaState->sol;
	sol::environment sandbox(lua, sol::create);

	// Registering globals
	sandbox.set(
	    "print", LuaPrint,
	    "_DEBUG",
#ifdef _DEBUG
	    true,
#else
	    false,
#endif
	    "_VERSION", LUA_VERSION);

	// Register safe built-in globals.
	for (const std::string_view global : {
	         // Built-ins:
	         "assert", "warn", "error", "ipairs", "next", "pairs", "pcall",
	         "select", "tonumber", "tostring", "type", "xpcall",
	         "rawequal", "rawget", "rawset", "setmetatable",
	// Built-in packages:
#ifdef _DEBUG
	         "debug",
#endif
	         "base", "coroutine", "table", "string", "math", "utf8" }) {
		const sol::object obj = lua[global];
		if (obj.get_type() == sol::type::lua_nil) {
			app_fatal(StrCat("Missing Lua global [", global, "]"));
		}
		sandbox[global] = obj;
	}

	// We only allow datetime-related functions from `os`:
	const sol::table os = lua["os"];
	sandbox.create_named("os",
	    "date", os["date"],
	    "difftime", os["difftime"],
	    "time", os["time"]);

	sandbox["require"] = lua["requireGen"](sandbox, CurrentLuaState->commonPackages, LuaLoadScriptFromAssets);

	// Expose commonly used enums globally for mods
	sandbox["SfxID"] = lua["SfxID"];

	return sandbox;
}

void AddModsChangedHandler(tl::function_ref<void()> callback)
{
	IsModChangeHandlers.push_back(callback);
}

void LuaReloadActiveMods()
{
	PS3_LUA_BOOT_LOG("active-mod reload begin");
	// Loaded without a sandbox.
	PS3_LUA_BOOT_LOG("loading devilutionx.events begin");
	CurrentLuaState->events = RunScript(/*env=*/std::nullopt, "devilutionx.events", /*optional=*/false);
	PS3_LUA_BOOT_LOG("loading devilutionx.events complete");
	CurrentLuaState->commonPackages["devilutionx.events"] = CurrentLuaState->events;

	ClearTownerDialogOptions();

	gbIsHellfire = false;
	PS3_LUA_BOOT_LOG("unloading mod archives begin");
	UnloadModArchives();
	PS3_LUA_BOOT_LOG("unloading mod archives complete");

	std::vector<std::string_view> modnames = GetOptions().Mods.GetActiveModList();
	PS3_LUA_BOOT_LOG("loading active mod archives begin");
	LoadModArchives(modnames);
	PS3_LUA_BOOT_LOG("loading active mod archives complete");

	PS3_LUA_BOOT_LOG(StrCat("active mods found: ", modnames.size()));
	for (const std::string_view modname : modnames) {
		PS3_LUA_BOOT_LOG(StrCat("loading mod script begin: ", modname));
		const std::string packageName = StrCat("mods.", modname, ".init");
		RunScript(CreateLuaSandbox(), packageName, /*optional=*/true);
		PS3_LUA_BOOT_LOG(StrCat("loading mod script complete: ", modname));
	}

	for (const tl::function_ref<void()> handler : IsModChangeHandlers) {
		handler();
	}
	PS3_LUA_BOOT_LOG("mod change handlers complete");

	// Reload sound effects in case a mod archive overrides effects.tsv
	PS3_LUA_BOOT_LOG("sound effect cleanup begin");
	effects_cleanup_sfx();
	PS3_LUA_BOOT_LOG("sound effect cleanup complete");
	PS3_LUA_BOOT_LOG("sound initialization begin");
	if (gbRunGame)
		sound_init();
	else
		ui_sound_init();
	PS3_LUA_BOOT_LOG("sound initialization complete");

	// Reload game data (this can probably be done later in the process to avoid having to reload it)
	PS3_LUA_BOOT_LOG("game data reload begin");
	LoadTextData();
	LoadPlayerDataFiles();
	LoadSpellData();
	LoadMissileData();
	LoadMonsterData();
	LoadItemData();
	LoadObjectData();
	LoadQuestData();
	PS3_LUA_BOOT_LOG("game data reload complete");

	lua::LoadModsComplete();
	PS3_LUA_BOOT_LOG("active-mod reload complete");
}

void LuaInitialize()
{
	PS3_LUA_BOOT_LOG("state construction begin");
	CurrentLuaState.emplace();
	PS3_LUA_BOOT_LOG("state construction complete");
	sol::state &lua = CurrentLuaState->sol;
	lua_setwarnf(lua.lua_state(), LuaWarn, /*ud=*/nullptr);
	PS3_LUA_BOOT_LOG("opening standard libraries begin");
	lua.open_libraries(
	    sol::lib::base,
	    sol::lib::coroutine,
	    sol::lib::debug,
	    sol::lib::math,
	    sol::lib::os,
	    sol::lib::package,
	    sol::lib::string,
	    sol::lib::table,
	    sol::lib::utf8);
	PS3_LUA_BOOT_LOG("opening standard libraries complete");

	// Registering devilutionx object table
	PS3_LUA_BOOT_LOG("registering require helper begin");
	SafeCallResult(lua.safe_script(RequireGenSrc), /*optional=*/false);
	PS3_LUA_BOOT_LOG("registering require helper complete");

	CurrentLuaState->commonPackages = lua.create_table();
#ifdef _DEBUG
	PS3_LUA_BOOT_LOG("registering dev module begin");
	CurrentLuaState->commonPackages["devilutionx.dev"] = LuaDevModule(lua);
	PS3_LUA_BOOT_LOG("registering dev module complete");
#endif
	CurrentLuaState->commonPackages["devilutionx.version"] = PROJECT_VERSION;
	PS3_LUA_BOOT_LOG("registering i18n module begin");
	CurrentLuaState->commonPackages["devilutionx.i18n"] = LuaI18nModule(lua);
	PS3_LUA_BOOT_LOG("registering i18n module complete");
	PS3_LUA_BOOT_LOG("registering items module begin");
	CurrentLuaState->commonPackages["devilutionx.items"] = LuaItemModule(lua);
	PS3_LUA_BOOT_LOG("registering items module complete");
	PS3_LUA_BOOT_LOG("registering log module begin");
	CurrentLuaState->commonPackages["devilutionx.log"] = LuaLogModule(lua);
	PS3_LUA_BOOT_LOG("registering log module complete");
	PS3_LUA_BOOT_LOG("registering audio module begin");
	CurrentLuaState->commonPackages["devilutionx.audio"] = LuaAudioModule(lua);
	PS3_LUA_BOOT_LOG("registering audio module complete");
	PS3_LUA_BOOT_LOG("registering monsters module begin");
	CurrentLuaState->commonPackages["devilutionx.monsters"] = LuaMonstersModule(lua);
	PS3_LUA_BOOT_LOG("registering monsters module complete");
	PS3_LUA_BOOT_LOG("registering player module begin");
	CurrentLuaState->commonPackages["devilutionx.player"] = LuaPlayerModule(lua);
	PS3_LUA_BOOT_LOG("registering player module complete");
	PS3_LUA_BOOT_LOG("registering render module begin");
	CurrentLuaState->commonPackages["devilutionx.render"] = LuaRenderModule(lua);
	PS3_LUA_BOOT_LOG("registering render module complete");
	PS3_LUA_BOOT_LOG("registering towners module begin");
	CurrentLuaState->commonPackages["devilutionx.towners"] = LuaTownersModule(lua);
	PS3_LUA_BOOT_LOG("registering towners module complete");
	PS3_LUA_BOOT_LOG("registering hellfire module begin");
	CurrentLuaState->commonPackages["devilutionx.hellfire"] = LuaHellfireModule(lua);
	PS3_LUA_BOOT_LOG("registering hellfire module complete");
	PS3_LUA_BOOT_LOG("registering system module begin");
	CurrentLuaState->commonPackages["devilutionx.system"] = LuaSystemModule(lua);
	PS3_LUA_BOOT_LOG("registering system module complete");
	PS3_LUA_BOOT_LOG("registering floating numbers module begin");
	CurrentLuaState->commonPackages["devilutionx.floatingnumbers"] = LuaFloatingNumbersModule(lua);
	PS3_LUA_BOOT_LOG("registering floating numbers module complete");
	CurrentLuaState->commonPackages["devilutionx.message"] = [](std::string_view text) { EventPlrMsg(text, UiFlags::ColorRed); };

	// This package is loaded without a sandbox:
	PS3_LUA_BOOT_LOG("inspect script initialization begin");
	CurrentLuaState->commonPackages["inspect"] = RunScript(/*env=*/std::nullopt, "inspect", /*optional=*/false);
	PS3_LUA_BOOT_LOG("inspect script initialization complete");

	// Used by the custom require implementation.
	PS3_LUA_BOOT_LOG("registering environment helper begin");
	lua["setEnvironment"] = [](const sol::environment &env, const sol::function &fn) { sol::set_environment(env, fn); };
	PS3_LUA_BOOT_LOG("registering environment helper complete");

	PS3_LUA_BOOT_LOG("registering mod callbacks begin");
	for (OptionEntryBase *mod : GetOptions().Mods.GetEntries()) {
		mod->SetValueChangedCallback(LuaReloadActiveMods);
	}
	PS3_LUA_BOOT_LOG("registering mod callbacks complete");

	PS3_LUA_BOOT_LOG("active-mod reload dispatch begin");
	LuaReloadActiveMods();
	PS3_LUA_BOOT_LOG("LuaInitialize complete");
}

void LuaShutdown()
{
#ifdef _DEBUG
	LuaReplShutdown();
#endif
	// Must clear before destroying the Lua state: registered callbacks
	// capture sol::function handles that reference CurrentLuaState.
	ClearTownerDialogOptions();
	CurrentLuaState = std::nullopt;
}

sol::table *GetLuaEvents()
{
	return CurrentLuaState ? &CurrentLuaState->events : nullptr;
}

sol::state &GetLuaState()
{
	return CurrentLuaState->sol;
}

sol::object SafeCallResult(sol::protected_function_result result, bool optional)
{
	const bool valid = result.valid();
	if (!valid) {
		const std::string error = result.get_type() == sol::type::string
		    ? StrCat("Lua error: ", result.get<std::string>())
		    : "Unknown Lua error";
		if (!optional)
			app_fatal(error);
		LogError(error);
	}
	return result;
}

} // namespace devilution
