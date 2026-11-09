#include "LuaBindings.h"
#include "gameplay/ScenePrefabs.h"
#include "scene/PrefabInstantiation.h"
#include "scene/Scene.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
/// <summary>
/// Lua APIを呼ばずに候補へ階層を生成し、所有するC++値をこのフレームで破棄する
/// </summary>
bool Perform(LuaInternal::Call &call, SceneObjectId owner, const char *name,
             std::size_t length, const float *position,
             SceneObjectId &created) noexcept {
	try {
		if (!call.executing)
			throw std::runtime_error(
			    "Instantiate Prefabs inside callbacks, not at module scope.");
		if (++call.instantiations > LuaScript::kMaximumInstantiations ||
		    (call.remainingInstantiations &&
		     *call.remainingInstantiations <= 0))
			throw std::runtime_error(
			    "Script Prefab instantiation limit exceeded.");
		if (call.remainingInstantiations)
			--*call.remainingInstantiations;
		const auto *o = call.scene ? call.scene->FindObject(owner) : nullptr;
		if (!o || !o->script || !call.prefabs)
			throw std::runtime_error(
			    "A script with prepared Prefab slots is required.");
		const auto found =
		    std::ranges::find_if(o->script->prefabs, [&](const auto &entry) {
			    return entry.first == std::string_view{name, length};
		    });
		if (found == o->script->prefabs.end() || found->second.empty())
			throw std::runtime_error(
			    "Assign the named Prefab slot before instantiating it.");
		const auto prefab = call.prefabs->Find(found->second);
		if (!prefab)
			throw std::runtime_error(
			    "The assigned Prefab has not been prepared.");
		const auto count = prefab->GetObjects().size();
		if (call.remainingSpawnedObjects &&
		    count > *call.remainingSpawnedObjects)
			throw std::runtime_error("Script Prefab object budget exceeded.");
		if (call.remainingSpawnedObjects)
			*call.remainingSpawnedObjects -= count;
		std::string error;
		created = PrefabInstantiation::Instantiate(
		    *prefab, *call.scene, {position[0], position[1], position[2]},
		    error);
		if (!created)
			throw std::runtime_error(error);
		call.physicsDirty = true;
		return true;
	} catch (const std::exception &exception) {
		call.error.assign(
		    exception.what(),
		    (std::min)(std::strlen(exception.what()), std::size_t(512)));
		return false;
	} catch (...) {
		call.error.assign("Prefab instantiation failed.");
		return false;
	}
}

/// <summary>
/// 所有値を持たないCフレームで生成引数を確認し、新しい根のuserdataを返す
/// </summary>
int Instantiate(lua_State *state) {
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	const auto owner = *static_cast<SceneObjectId *>(
	    luaL_checkudata(state, 1, "Engine.ScriptObject"));
	luaL_checktype(state, 2, LUA_TSTRING);
	std::size_t length = 0;
	const char *name = lua_tolstring(state, 2, &length);
	if (length == 0 || length > 64 || std::memchr(name, '\0', length))
		return luaL_error(state,
		                  "Prefab names require 1 to 64 bytes without NUL.");
	float position[3]{};

	for (int i = 0; i < 3; ++i) {
		luaL_checktype(state, i + 3, LUA_TNUMBER);
		const double value = lua_tonumber(state, i + 3);
		if (!std::isfinite(value) ||
		    std::abs(value) > (std::numeric_limits<float>::max)())
			return luaL_error(state, "Prefab positions require finite floats.");
		position[i] = static_cast<float>(value);
	}

	SceneObjectId created = 0;

	if (!Perform(call, owner, name, length, position, created)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}

	LuaInternal::PushObject(state, created);
	return 1;
}
} // namespace

void LuaInternal::AddPrefabMethods(lua_State *state) {
	lua_pushcfunction(state, Instantiate);
	lua_setfield(state, -2, "instantiate");
}
