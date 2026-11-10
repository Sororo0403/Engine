#include "LuaBindings.h"
#include "animation/SceneAnimations.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include <cmath>
#include <cstring>
#include <iterator>
#include <stdexcept>

namespace {
enum class Op { PLAY, PAUSE, STOP, SEEK, PLAYING, TIME, SET_CLIP_PAUSED };

/// <summary>
/// Lua APIを呼ばずに、作業中のアニメーションを操作・参照する
/// </summary>
/// <param name="call">候補の物体・再生状態・残り呼び出し回数</param>
/// <param name="op">操作</param>
/// <param name="id">コンポーネントを持つ物体</param>
/// <param name="value">シーク位置の入力、または時刻・再生状態の出力</param>
/// <param name="restart">PLAYを先頭から開始するか</param>
/// <returns>true:成功 false:失敗理由をcall.errorへ設定する</returns>
bool Perform(LuaInternal::Call &call, Op op, SceneObjectId id, double &value,
             bool restart) noexcept {
	try {
		if (!call.executing && op != Op::PLAYING && op != Op::TIME)
			throw std::runtime_error(
			    "Control animation inside callbacks, not at module scope.");
		if (++call.animationOperations > 128 ||
		    (call.remainingAnimationOperations &&
		     *call.remainingAnimationOperations <= 0))
			throw std::runtime_error(
			    "Script animation API call limit exceeded.");
		if (call.remainingAnimationOperations)
			--*call.remainingAnimationOperations;
		const auto *object = call.scene ? call.scene->FindObject(id) : nullptr;
		if (op == Op::SET_CLIP_PAUSED) {
			if (!object || !object->animationClip || !call.clipPauses)
				throw std::runtime_error("This object requires an Animation Player.");
			call.clipPauses->insert_or_assign(id, value != 0);
			value = 1;
			return true;
		}
		if (!object || !call.animations)
			throw std::runtime_error("Animation runtime is unavailable.");
		auto current = call.animations->GetState(id);
		// 同じコールバックで生成したPrefabも、その開始姿勢を基準に操作できるよう準備する。
		if (!current && call.executing && object->transformAnimation) {
			if (!call.animations->Configure(*call.scene, true, call.error))
				return false;
			call.physicsDirty = true;
			current = call.animations->GetState(id);
		}

		if (!current)
			throw std::runtime_error(
			    "This object requires Transform Animation.");
		if (op == Op::PLAYING) {
			value = current->playing;
			return true;
		}

		if (op == Op::TIME) {
			value = current->seconds;
			return true;
		}

		if (op == Op::PLAY && !SceneActivity::IsActive(*call.scene, id)) {
			value = 0;
			return true;
		}

		const auto operation =
		    op == Op::PLAY    ? SceneAnimations::Operation::PLAY
		    : op == Op::PAUSE ? SceneAnimations::Operation::PAUSE
		    : op == Op::STOP  ? SceneAnimations::Operation::STOP
		                      : SceneAnimations::Operation::SEEK;
		if (!call.animations->Control(*call.scene, id, operation, value,
		                              restart, call.error))
			return false;
		value = 1;
		call.physicsDirty = true;
		return true;
	} catch (const std::exception &e) {
		call.error = e.what();
		return false;
	} catch (...) {
		call.error = "Animation operation failed.";
		return false;
	}
}

/// <summary>
/// 所有するC++変数を残さず引数を検証し、保護された物体APIへ渡す
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
/// <returns>返した値の数</returns>
int Dispatch(lua_State *state) {
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	const auto op = static_cast<Op>(lua_tointeger(state, lua_upvalueindex(1)));
	const auto id = *static_cast<SceneObjectId *>(
	    luaL_checkudata(state, 1, "Engine.ScriptObject"));
	double value = 0;
	bool restart = true;

	if (op == Op::PLAY && !lua_isnoneornil(state, 2)) {
		luaL_checktype(state, 2, LUA_TBOOLEAN);
		restart = lua_toboolean(state, 2) != 0;
	}

	if (op == Op::SEEK) {
		luaL_checktype(state, 2, LUA_TNUMBER);
		value = lua_tonumber(state, 2);
		if (!std::isfinite(value) || value < 0)
			return luaL_error(
			    state, "Animation seek must use a finite non-negative time.");
	}
	if (op == Op::SET_CLIP_PAUSED) {
		luaL_checktype(state, 2, LUA_TBOOLEAN);
		value = lua_toboolean(state, 2);
	}

	if (!Perform(call, op, id, value, restart)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}

	if (op == Op::TIME)
		lua_pushnumber(state, value);
	else
		lua_pushboolean(state, value != 0);
	return 1;
}
} // namespace

void LuaInternal::AddAnimationMethods(lua_State *state) {
	const char *names[]{"play_animation",       "pause_animation",
	                    "stop_animation",       "seek_animation",
	                    "is_animation_playing", "get_animation_time",
	                    "set_animation_clip_paused"};
	for (std::size_t i = 0; i < std::size(names); ++i) {
		lua_pushinteger(state, i);
		lua_pushcclosure(state, Dispatch, 1);
		lua_setfield(state, -2, names[i]);
	}
}
