#include "LuaBindings.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
/// <summary>
/// Lua APIを呼ばずに名前付きシーンを解決し、次回更新への遷移を要求する
/// </summary>
/// <param name="call">候補シーンと未確定の遷移要求</param>
/// <param name="id">名前付きシーンを持つ物体</param>
/// <param name="name">Luaが保持する割り当て名</param>
/// <param name="length">名前のバイト数</param>
/// <returns>true:予約成功 false:失敗理由をcall.errorへ設定する</returns>
bool Perform(LuaInternal::Call &call, SceneObjectId id, const char *name,
             std::size_t length) noexcept {
	try {
		if (!call.executing)
			throw std::runtime_error(
			    "Request scene changes inside callbacks, not at module scope.");
		if (!call.sceneRequest)
			throw std::runtime_error(
			    "Scene changes require a simulation request queue.");
		if (++call.sceneRequests > LuaScript::kMaximumSceneRequests ||
		    (call.remainingSceneRequests && *call.remainingSceneRequests <= 0))
			throw std::runtime_error("Script scene request limit exceeded.");
		if (call.remainingSceneRequests)
			--*call.remainingSceneRequests;
		const auto *o = call.scene ? call.scene->FindObject(id) : nullptr;
		if (!o || !o->script)
			throw std::runtime_error(
			    "This object requires Lua Script Scene slots.");
		const auto entry =
		    std::ranges::find_if(o->script->scenes, [&](const auto &slot) {
			    return slot.first == std::string_view{name, length};
		    });
		if (entry == o->script->scenes.end() || entry->second.empty())
			throw std::runtime_error(
			    "Assign the named Scene slot before requesting a transition.");
		*call.sceneRequest =
		    PathUtils::ResolveFromExecutable(entry->second).lexically_normal();
		return true;
	} catch (const std::exception &exception) {
		call.error.assign(
		    exception.what(),
		    (std::min)(std::strlen(exception.what()), std::size_t(512)));
		return false;
	} catch (...) {
		call.error.assign("Script scene change failed.");
		return false;
	}
}

/// <summary>
/// 所有値を持たないCフレームで割り当て名を確認し、遷移要求をLuaから受け取る
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
/// <returns>戻り値なし。実際の読み込みは次の正の時間の更新で行う</returns>
int LoadScene(lua_State *state) {
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	const auto id = *static_cast<SceneObjectId *>(
	    luaL_checkudata(state, 1, "Engine.ScriptObject"));
	luaL_checktype(state, 2, LUA_TSTRING);
	std::size_t length = 0;
	const char *name = lua_tolstring(state, 2, &length);
	if (!length || length > ScriptComponent::kMaximumReferenceNameBytes ||
	    std::memchr(name, '\0', length))
		return luaL_error(state,
		                  "Scene names require 1 to 64 bytes without NUL.");
	if (!Perform(call, id, name, length)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}
	return 0;
}
} // namespace

void LuaInternal::AddSceneMethods(lua_State *state) {
	lua_pushcfunction(state, LoadScene);
	lua_setfield(state, -2, "load_scene");
}
