#include "LuaBindings.h"
#include "physics/SceneRaycast.h"
#include "scene/Scene.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
/// <summary>
/// Lua APIを呼ばずに候補シーンを検索し、C++の所有値をこのフレームで破棄する
/// </summary>
bool Perform(LuaInternal::Call &call, const Ray &ray, std::uint32_t mask,
             bool triggers, SceneObjectId ignored, RaycastHit &hit) noexcept {
	try {
		if (++call.physicsOperations > LuaScript::kMaximumPhysicsOperations ||
		    (call.remainingPhysicsOperations &&
		     *call.remainingPhysicsOperations <= 0))
			throw std::runtime_error("Script physics API call limit exceeded.");
		if (call.remainingPhysicsOperations)
			--*call.remainingPhysicsOperations;
		if (!call.scene)
			throw std::runtime_error("Raycast requires a scene.");
		if (ignored && !call.scene->Contains(ignored))
			throw std::runtime_error(
			    "The ignored raycast object no longer exists.");
		RaycastOptions options;
		options.layerMask = mask;
		options.includeTriggers = triggers;
		options.ignored = {&ignored, 1};
		if (!SceneRaycast::Cast(*call.scene, ray, hit, options))
			throw std::runtime_error(
			    "Raycast requires valid collider transforms, a finite nonzero "
			    "direction and a distance from 0 to 1000000.");
		return true;
	} catch (const std::exception &e) {
		call.error.assign(e.what(),
		                  (std::min)(std::strlen(e.what()), std::size_t(512)));
		return false;
	} catch (...) {
		call.error.assign("Raycast failed.");
		return false;
	}
}

/// <summary>
/// 所有値のないCフレームで引数を検証し、交点のテーブルまたはnilを返す
/// </summary>
int Cast(lua_State *state) {
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	float values[7]{};
	for (int i = 0; i < 7; ++i) {
		luaL_checktype(state, i + 1, LUA_TNUMBER);
		const double value = lua_tonumber(state, i + 1);
		if (!std::isfinite(value) ||
		    std::abs(value) > (std::numeric_limits<float>::max)())
			return luaL_error(state,
			                  "Raycast arguments require finite float values.");
		values[i] = static_cast<float>(value);
	}
	std::uint32_t mask = UINT32_MAX;
	if (!lua_isnoneornil(state, 8)) {
		luaL_checktype(state, 8, LUA_TNUMBER);
		int valid = 0;
		const auto value = lua_tointegerx(state, 8, &valid);
		if (!valid || value < 0 ||
		    static_cast<unsigned long long>(value) > UINT32_MAX)
			return luaL_error(
			    state,
			    "Raycast layer mask requires an unsigned 32-bit integer.");
		mask = static_cast<std::uint32_t>(value);
	}
	bool triggers = false;
	if (!lua_isnoneornil(state, 9)) {
		luaL_checktype(state, 9, LUA_TBOOLEAN);
		triggers = lua_toboolean(state, 9) != 0;
	}
	SceneObjectId ignored = call.self;
	if (!lua_isnoneornil(state, 10))
		ignored = *static_cast<SceneObjectId *>(
		    luaL_checkudata(state, 10, "Engine.ScriptObject"));
	const Ray ray{{values[0], values[1], values[2]},
	              {values[3], values[4], values[5]},
	              values[6]};
	RaycastHit hit;
	if (!Perform(call, ray, mask, triggers, ignored, hit)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}
	if (!hit.objectId) {
		lua_pushnil(state);
		return 1;
	}
	lua_createtable(state, 0, 8);
	LuaInternal::PushObject(state, hit.objectId);
	lua_setfield(state, -2, "object");
	const char *names[]{"distance", "x",        "y",       "z",
	                    "normal_x", "normal_y", "normal_z"};
	const float numbers[]{hit.distance,   hit.position.x, hit.position.y,
	                      hit.position.z, hit.normal.x,   hit.normal.y,
	                      hit.normal.z};
	for (int i = 0; i < 7; ++i) {
		lua_pushnumber(state, numbers[i]);
		lua_setfield(state, -2, names[i]);
	}
	return 1;
}
} // namespace

void LuaInternal::AddRaycastMethods(lua_State *state) {
	lua_pushcfunction(state, Cast);
	lua_setfield(state, -2, "raycast");
}
