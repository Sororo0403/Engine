#include "LuaBindings.h"
#include "animation/SceneAnimators.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include <stdexcept>

namespace {
enum class Op {
	PLAY,
	PAUSE,
	STOP,
	SEEK,
	TIME,
	PLAYING,
	CLIP,
	DURATION,
	SPEED,
	SET_SPEED,
	SET_LOOP
};

/// <summary>
/// Lua APIを使わず、同じ候補のAnimatorだけを操作・参照する
/// </summary>
/// <param name="call">再生候補と残り操作数</param>
/// <param name="op">再生・停止・指定または参照</param>
/// <param name="id">Animatorを持つ物体</param>
/// <param name="clip">PLAYで指定する名前。空は現在のクリップ</param>
/// <param name="number">シーク位置または速度</param>
/// <param name="flag">先頭から再生するか、またはループ指定</param>
/// <param name="value">成功時の数値・真偽値。名前はcall.returnTextへ設定する</param>
/// <returns>true:成功 false:失敗理由をcall.errorへ設定する</returns>
bool Perform(LuaInternal::Call &call, Op op, SceneObjectId id,
             std::string_view clip, double number, bool flag,
             double &value) noexcept {
	try {
		const bool read = op == Op::TIME || op == Op::PLAYING ||
		                  op == Op::CLIP || op == Op::DURATION ||
		                  op == Op::SPEED;
		if (!read && !call.executing)
			throw std::runtime_error(
			    "Control Animator inside callbacks, not at module scope.");
		if (++call.animatorOperations > 128 ||
		    (call.remainingAnimatorOperations &&
		     *call.remainingAnimatorOperations <= 0))
			throw std::runtime_error(
			    "Script Animator API call limit exceeded.");
		if (call.remainingAnimatorOperations)
			--*call.remainingAnimatorOperations;
		if (!call.scene || !call.scene->Contains(id) || !call.animators)
			throw std::runtime_error("Animator runtime is unavailable.");
		auto state = call.animators->GetState(id);
		if (!state && call.executing && call.scene->FindObject(id)->animator) {
			// 生成直後のPrefabは開始前に準備した共有モデルだけを使用する。
			if (!call.animators->Configure(*call.scene, true, call.error,
			                               nullptr, {}, {}, false))
				return false;
			state = call.animators->GetState(id);
		}
		if (!state)
			throw std::runtime_error("This object requires Animator.");
		if (op == Op::TIME) {
			value = state->seconds;
			return true;
		}
		if (op == Op::DURATION) {
			value = state->duration;
			return true;
		}
		if (op == Op::SPEED) {
			value = state->speed;
			return true;
		}
		if (op == Op::PLAYING) {
			value = state->playing;
			return true;
		}
		if (op == Op::CLIP) {
			call.returnText = state->clip;
			return true;
		}
		if (op == Op::PLAY && !SceneActivity::IsActive(*call.scene, id)) {
			value = 0;
			return true;
		}
		const auto operation =
		    op == Op::PLAY        ? SceneAnimators::Operation::PLAY
		    : op == Op::PAUSE     ? SceneAnimators::Operation::PAUSE
		    : op == Op::STOP      ? SceneAnimators::Operation::STOP
		    : op == Op::SEEK      ? SceneAnimators::Operation::SEEK
		    : op == Op::SET_SPEED ? SceneAnimators::Operation::SPEED
		                          : SceneAnimators::Operation::LOOP;
		if (!call.animators->Control(*call.scene, id, operation, clip, number,
		                             flag, call.error))
			return false;
		value = 1;
		return true;
	} catch (const std::exception &e) {
		call.error = e.what();
		return false;
	} catch (...) {
		call.error = "Animator operation failed.";
		return false;
	}
}

/// <summary>
/// 所有するC++変数を残さず引数を検証し、保護した操作の結果をLuaへ返す
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
/// <returns>結果の数</returns>
int Dispatch(lua_State *state) {
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	const auto op = static_cast<Op>(lua_tointeger(state, lua_upvalueindex(1)));
	const auto id = *static_cast<SceneObjectId *>(
	    luaL_checkudata(state, 1, "Engine.ScriptObject"));
	const char *clip = "";
	std::size_t length = 0;
	double number = 0, value = 0;
	bool flag = true;
	if (op == Op::PLAY) {
		if (!lua_isnoneornil(state, 2)) {
			luaL_checktype(state, 2, LUA_TSTRING);
			clip = luaL_checklstring(state, 2, &length);
		}
		if (!lua_isnoneornil(state, 3)) {
			luaL_checktype(state, 3, LUA_TBOOLEAN);
			flag = lua_toboolean(state, 3) != 0;
		}
	}
	if (op == Op::SEEK || op == Op::SET_SPEED)
		number = luaL_checknumber(state, 2);
	if (op == Op::SET_LOOP) {
		luaL_checktype(state, 2, LUA_TBOOLEAN);
		flag = lua_toboolean(state, 2) != 0;
	}
	if (!Perform(call, op, id, {clip, length}, number, flag, value)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}
	if (op == Op::CLIP)
		lua_pushlstring(state, call.returnText.data(), call.returnText.size());
	else if (op == Op::TIME || op == Op::DURATION || op == Op::SPEED)
		lua_pushnumber(state, value);
	else
		lua_pushboolean(state, value != 0);
	return 1;
}
} // namespace

void LuaInternal::AddAnimatorMethods(lua_State *state) {
	const char *names[]{
	    "play_clip",          "pause_animator",        "stop_animator",
	    "seek_animator",      "get_animator_time",     "is_animator_playing",
	    "get_animator_clip",  "get_animator_duration", "get_animator_speed",
	    "set_animator_speed", "set_animator_loop"};
	for (int i = 0; i < 11; ++i) {
		lua_pushinteger(state, i);
		lua_pushcclosure(state, Dispatch, 1);
		lua_setfield(state, -2, names[i]);
	}
}
