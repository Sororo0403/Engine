#include "LuaBindings.h"
#include "LuaStateData.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scripting/LuaScript.h"
#include "scripting/ScriptMailbox.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
/// <summary>
/// Lua APIを呼ばずに通知を複製し、次回更新へのキューへ追加する
/// </summary>
/// <param name="call">候補シーン・通知キューと、読み取ったJSON</param>
/// <param name="receiver">受信する物体ID</param>
/// <param name="name">Luaが保持する名前</param>
/// <param name="length">名前のバイト数</param>
/// <param name="queued">有効なLua Scriptへ通知を追加したか</param>
/// <returns>true:処理成功 false:失敗理由をcall.errorへ設定する</returns>
bool Perform(LuaInternal::Call &call, SceneObjectId receiver, const char *name,
             std::size_t length, bool &queued) noexcept {
	try {
		if (!call.executing)
			throw std::runtime_error(
			    "Send messages inside callbacks, not at module scope.");
		if (!call.mailbox)
			throw std::runtime_error("Messages require a simulation mailbox.");
		if (++call.messagesSent > LuaScript::kMaximumMessages)
			throw std::runtime_error("Script callback message limit exceeded.");
		const auto *o = call.scene ? call.scene->FindObject(receiver) : nullptr;
		if (!o || !o->script || !o->script->enabled ||
		    o->script->asset.empty() ||
		    !SceneActivity::IsActive(*call.scene, receiver)) {
			queued = false;
			return true;
		}
		if (!call.mailbox->Enqueue(receiver, call.self, {name, length},
		                           call.messageData.empty()
		                               ? std::string_view("{}")
		                               : std::string_view(call.messageData),
		                           call.error))
			return false;
		queued = true;
		return true;
	} catch (const std::exception &exception) {
		call.error.assign(
		    exception.what(),
		    (std::min)(std::strlen(exception.what()), std::size_t(512)));
		return false;
	} catch (...) {
		call.error.assign("Script message could not be queued.");
		return false;
	}
}

/// <summary>
/// 所有値を持たないCフレームで名前とデータを検証し、追加結果を返す
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
/// <returns>true:予約成功 false:対象に実行可能なLua Scriptがない</returns>
int SendMessage(lua_State *state) {
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	const auto receiver = *static_cast<SceneObjectId *>(
	    luaL_checkudata(state, 1, "Engine.ScriptObject"));
	luaL_checktype(state, 2, LUA_TSTRING);
	std::size_t length = 0;
	const char *name = lua_tolstring(state, 2, &length);
	if (!length || length > ScriptMessage::kMaximumNameBytes ||
	    std::memchr(name, '\0', length))
		return luaL_error(state,
		                  "Message names require 1 to 64 bytes without NUL.");
	call.messageData.clear();
	if (!lua_isnoneornil(state, 3)) {
		luaL_checktype(state, 3, LUA_TTABLE);
		if (!lua_checkstack(state, LuaScript::kMaximumStateDepth * 4 + 32))
			return luaL_error(state, "Message data stack limit exceeded.");
		if (!LuaInternal::ReadState(state, 3, call.messageData, call.error)) {
			lua_pushlstring(state, call.error.data(), call.error.size());
			return lua_error(state);
		}
	}
	bool queued = false;
	if (!Perform(call, receiver, name, length, queued)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}
	lua_pushboolean(state, queued);
	return 1;
}
} // namespace

void LuaInternal::AddMessageMethods(lua_State *state) {
	lua_pushcfunction(state, SendMessage);
	lua_setfield(state, -2, "send_message");
}
