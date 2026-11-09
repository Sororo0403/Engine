#include "LuaBindings.h"
#include "physics/CollisionWorld.h"
#include "physics/SceneRaycast.h"
#include "scene/Scene.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
struct QueryOptions {
	std::uint32_t layerMask = UINT32_MAX;
	bool includeTriggers = false;
	SceneObjectId ignored = 0;
};

/// <summary>
/// 空間問い合わせの予算と除外する物体を確認する
/// </summary>
void CheckQuery(LuaInternal::Call &call, SceneObjectId ignored) {
	if (++call.physicsOperations > LuaScript::kMaximumPhysicsOperations ||
	    (call.remainingPhysicsOperations &&
	     *call.remainingPhysicsOperations <= 0))
		throw std::runtime_error("Script physics API call limit exceeded.");
	if (call.remainingPhysicsOperations)
		--*call.remainingPhysicsOperations;
	if (!call.scene)
		throw std::runtime_error("Spatial query requires a scene.");
	if (ignored && !call.scene->Contains(ignored))
		throw std::runtime_error(
		    "The ignored spatial query object no longer exists.");
}

/// <summary>
/// Lua APIを呼ばずに候補シーンを検索し、C++の所有値をこのフレームで破棄する
/// </summary>
bool Perform(LuaInternal::Call &call, const Ray &ray,
             const std::optional<float> &radius, const QueryOptions &query,
             RaycastHit &hit) noexcept {
	try {
		CheckQuery(call, query.ignored);
		RaycastOptions options;
		options.layerMask = query.layerMask;
		options.includeTriggers = query.includeTriggers;
		options.ignored = {&query.ignored, 1};
		const bool ok =
		    radius ? SceneRaycast::CastSphere(*call.scene, ray, *radius, hit,
		                                      options)
		           : SceneRaycast::Cast(*call.scene, ray, hit, options);
		if (!ok)
			throw std::runtime_error(
			    radius ? "Sphere cast requires a valid center, radius, "
			             "direction, distance and collider transforms."
			           : "Raycast requires valid collider transforms, a finite "
			             "nonzero direction and a distance from 0 to 1000000.");
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
/// 空間問い合わせに渡す有限な座標・距離を読み取る
/// </summary>
void ReadNumbers(lua_State *state, std::span<float> values) {
	for (int i = 0; i < static_cast<int>(values.size()); ++i) {
		luaL_checktype(state, i + 1, LUA_TNUMBER);
		const double value = lua_tonumber(state, i + 1);
		if (!std::isfinite(value) ||
		    std::abs(value) > (std::numeric_limits<float>::max)())
			luaL_error(state,
			           "Spatial query arguments require finite float values.");
		values[i] = static_cast<float>(value);
	}
}

/// <summary>
/// レイヤー・Trigger・除外物体の任意引数を読み取る
/// </summary>
QueryOptions ReadOptions(lua_State *state, int first, SceneObjectId self) {
	QueryOptions options;
	options.ignored = self;

	if (!lua_isnoneornil(state, first)) {
		luaL_checktype(state, first, LUA_TNUMBER);
		int valid = 0;
		const auto value = lua_tointegerx(state, first, &valid);
		if (!valid || value < 0 ||
		    static_cast<unsigned long long>(value) > UINT32_MAX)
			luaL_error(state, "Spatial query layer mask requires an unsigned "
			                  "32-bit integer.");
		options.layerMask = static_cast<std::uint32_t>(value);
	}

	if (!lua_isnoneornil(state, first + 1)) {
		luaL_checktype(state, first + 1, LUA_TBOOLEAN);
		options.includeTriggers = lua_toboolean(state, first + 1) != 0;
	}

	if (!lua_isnoneornil(state, first + 2))
		options.ignored = *static_cast<SceneObjectId *>(
		    luaL_checkudata(state, first + 2, "Engine.ScriptObject"));
	return options;
}

/// <summary>
/// 所有値のないCフレームで引数を検証し、交点のテーブルまたはnilを返す
/// </summary>
template <bool sphere> int Cast(lua_State *state) {
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	float values[sphere ? 8 : 7]{};
	ReadNumbers(state, values);
	const auto query = ReadOptions(state, sphere ? 9 : 8, call.self);
	const Ray ray{{values[0], values[1], values[2]},
	              {values[3], values[4], values[5]},
	              values[6]};
	const auto radius = sphere ? std::optional<float>{values[7]} : std::nullopt;
	RaycastHit hit;

	if (!Perform(call, ray, radius, query, hit)) {
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

constexpr int kMaximumSphereResults = 256;

/// <summary>
/// 球と重なる物体を検索し、Luaへ渡す件数を制限する
/// </summary>
bool FindInSphere(LuaInternal::Call &call, const DirectX::XMFLOAT3 &center,
                  float radius, const QueryOptions &query,
                  std::span<SceneObjectId> objects, int &count,
                  bool &truncated) noexcept {
	try {
		CheckQuery(call, query.ignored);
		std::vector<SceneObjectId> matches;
		if (!CollisionWorld::FindSphereOverlaps(
		        *call.scene, center, radius, matches, query.layerMask,
		        query.includeTriggers, query.ignored))
			throw std::runtime_error("Sphere overlap requires a valid center, "
			                         "radius and collider transforms.");
		const auto size = (std::min)(matches.size(), objects.size());
		std::copy_n(matches.begin(), size, objects.begin());
		count = static_cast<int>(size);
		truncated = size < matches.size();
		return true;
	} catch (const std::exception &e) {
		call.error.assign(e.what(),
		                  (std::min)(std::strlen(e.what()), std::size_t(512)));
		return false;
	} catch (...) {
		call.error.assign("Sphere overlap failed.");
		return false;
	}
}

/// <summary>
/// 重なる物体の配列と、件数上限で省略されたかを返す
/// </summary>
int OverlapSphere(lua_State *state) {
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	float values[4]{};
	ReadNumbers(state, values);
	const auto query = ReadOptions(state, 5, call.self);
	std::array<SceneObjectId, kMaximumSphereResults> objects{};
	int count = 0;
	bool truncated = false;

	if (!FindInSphere(call, {values[0], values[1], values[2]}, values[3], query,
	                  objects, count, truncated)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}

	lua_createtable(state, count, 0);

	for (int i = 0; i < count; ++i) {
		LuaInternal::PushObject(state, objects[i]);
		lua_rawseti(state, -2, i + 1);
	}

	lua_pushboolean(state, truncated);
	return 2;
}
} // namespace

void LuaInternal::AddSpatialQueryMethods(lua_State *state) {
	lua_pushcfunction(state, Cast<false>);
	lua_setfield(state, -2, "raycast");
	lua_pushcfunction(state, Cast<true>);
	lua_setfield(state, -2, "cast_sphere");
	lua_pushcfunction(state, OverlapSphere);
	lua_setfield(state, -2, "overlap_sphere");
}
