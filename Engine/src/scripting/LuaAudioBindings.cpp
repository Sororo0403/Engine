#include "LuaBindings.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
enum class Op { PLAY, STOP, VOLUME, SET_VOLUME };

/// <summary>
/// Lua APIを呼ばずに音声要求を候補へ追加し、音量の問い合わせ・変更を行う
/// </summary>
/// <param name="call">候補シーン・音声要求と残り実行回数</param>
/// <param name="op">再生・停止・音量の取得または変更</param>
/// <param name="id">Audio Sourceを持つ物体</param>
/// <param name="volume">音量の入出力。0から4まで</param>
/// <param name="queued">再生・停止で、有効な音源への要求を追加したか</param>
/// <returns>true:成功 false:失敗理由をcall.errorへ設定する</returns>
bool Perform(LuaInternal::Call &call, Op op, SceneObjectId id, float &volume,
             bool &queued) noexcept {
	try {
		if (op != Op::VOLUME && !call.executing)
			throw std::runtime_error(
			    "Change audio inside callbacks, not at module scope.");
		if (++call.audioOperations > LuaScript::kMaximumAudioOperations ||
		    (call.remainingAudioOperations &&
		     *call.remainingAudioOperations <= 0))
			throw std::runtime_error("Script audio API call limit exceeded.");
		if (call.remainingAudioOperations)
			--*call.remainingAudioOperations;
		auto *object = call.scene ? call.scene->FindObject(id) : nullptr;
		if (!object || !object->audioSource)
			throw std::runtime_error("This object requires an Audio Source.");
		auto &source = *object->audioSource;
		if (!source.IsValid())
			throw std::runtime_error("Audio Source settings are invalid.");
		if (op == Op::VOLUME) {
			volume = source.volume;
			return true;
		}

		if (op == Op::SET_VOLUME) {
			if (!std::isfinite(volume) || volume < 0 ||
			    volume > AudioPlaybackSettings::kMaximumVolume)
				throw std::runtime_error(
				    "Audio volume must be between 0 and 4.");
			source.volume = volume;
			return true;
		}

		if (!call.audioActions)
			throw std::runtime_error(
			    "Audio playback requires a simulation command queue.");
		queued = !source.clipPath.empty() &&
		         SceneActivity::IsActive(*call.scene, id);
		if (queued)
			call.audioActions->push_back(
			    {id, op == Op::PLAY ? SceneAudioAction::Operation::PLAY
			                        : SceneAudioAction::Operation::STOP});
		return true;
	} catch (const std::exception &exception) {
		call.error.assign(
		    exception.what(),
		    (std::min)(std::strlen(exception.what()), std::size_t(512)));
		return false;
	} catch (...) {
		call.error.assign("Script audio operation failed.");
		return false;
	}
}

/// <summary>
/// 所有値を持たないCフレームで音声操作の引数を確認し、結果をLuaへ返す
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
/// <returns>再生・停止は真偽値、音量取得は数値、変更は戻り値なし</returns>
int Dispatch(lua_State *state) {
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	const auto op = static_cast<Op>(lua_tointeger(state, lua_upvalueindex(1)));
	const auto id = *static_cast<SceneObjectId *>(
	    luaL_checkudata(state, 1, "Engine.ScriptObject"));
	float volume = 0;

	if (op == Op::SET_VOLUME) {
		luaL_checktype(state, 2, LUA_TNUMBER);
		const double value = lua_tonumber(state, 2);
		if (!std::isfinite(value) || value < 0 ||
		    value > AudioPlaybackSettings::kMaximumVolume)
			return luaL_error(state, "Audio volume must be between 0 and 4.");
		volume = static_cast<float>(value);
	}

	bool queued = false;

	if (!Perform(call, op, id, volume, queued)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}

	if (op == Op::VOLUME) {
		lua_pushnumber(state, volume);
		return 1;
	}

	if (op == Op::PLAY || op == Op::STOP) {
		lua_pushboolean(state, queued);
		return 1;
	}

	return 0;
}
} // namespace

void LuaInternal::AddAudioMethods(lua_State *state) {
	const char *names[]{"play_audio", "stop_audio", "get_audio_volume",
	                    "set_audio_volume"};
	for (int i = 0; i < 4; ++i) {
		lua_pushinteger(state, i);
		lua_pushcclosure(state, Dispatch, 1);
		lua_setfield(state, -2, names[i]);
	}
}
