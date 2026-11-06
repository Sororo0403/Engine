#include "LuaBindings.h"
#include "particles/SceneParticles.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include <stdexcept>

namespace {
enum class Op { PLAY, STOP, CLEAR, EMIT, COUNT, EMITTING };

/// <summary>
/// Lua APIを呼ばず、同じ候補の粒子だけを操作・参照する
/// </summary>
/// <param name="call">シーン・粒子と残り呼び出し回数</param>
/// <param name="op">発生・停止・消去または参照</param>
/// <param name="id">Particle Emitterを持つ物体</param>
/// <param name="count">任意に発生させる数</param>
/// <param name="restart">PLAYで時計・乱数・生存粒子を初期化するか</param>
/// <param name="value">成功した結果の出力</param>
/// <returns>true:成功 false:失敗理由をcall.errorへ設定する</returns>
bool Perform(LuaInternal::Call &call, Op op, SceneObjectId id, int count,
             bool restart, double &value) noexcept {
	try {
		const bool read = op == Op::COUNT || op == Op::EMITTING;
		if (!read && !call.executing)
			throw std::runtime_error(
			    "Control particles inside callbacks, not at module scope.");
		if (++call.particleOperations > 128 ||
		    (call.remainingParticleOperations &&
		     *call.remainingParticleOperations <= 0))
			throw std::runtime_error(
			    "Script particle API call limit exceeded.");
		if (call.remainingParticleOperations)
			--*call.remainingParticleOperations;
		if (!call.scene || !call.scene->Contains(id) || !call.particles)
			throw std::runtime_error("Particle runtime is unavailable.");
		auto state = call.particles->GetState(id);
		if (!state && call.executing &&
		    call.scene->FindObject(id)->particleEmitter) {
			if (!call.particles->Configure(*call.scene, true, call.error))
				return false;
			state = call.particles->GetState(id);
		}
		if (!state)
			throw std::runtime_error("This object requires Particle Emitter.");
		if (op == Op::COUNT) {
			value = static_cast<double>(state->count);
			return true;
		}
		if (op == Op::EMITTING) {
			value = state->emitting;
			return true;
		}
		if ((op == Op::PLAY || op == Op::EMIT) &&
		    !SceneActivity::IsActive(*call.scene, id)) {
			value = 0;
			return true;
		}
		const auto operation =
		    op == Op::PLAY    ? SceneParticles::Operation::PLAY
		    : op == Op::STOP  ? SceneParticles::Operation::STOP
		    : op == Op::CLEAR ? SceneParticles::Operation::CLEAR
		                      : SceneParticles::Operation::EMIT;
		if (!call.particles->Control(*call.scene, id, operation, count, restart,
		                             call.error))
			return false;
		value = 1;
		return true;
	} catch (const std::exception &e) {
		call.error = e.what();
		return false;
	} catch (...) {
		call.error = "Particle operation failed.";
		return false;
	}
}

/// <summary>
/// 所有するC++変数を残さず引数を検証し、保護した粒子処理の結果を返す
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
/// <returns>結果の数</returns>
int Dispatch(lua_State *state) {
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	const auto op = static_cast<Op>(lua_tointeger(state, lua_upvalueindex(1)));
	const auto id = *static_cast<SceneObjectId *>(
	    luaL_checkudata(state, 1, "Engine.ScriptObject"));
	int count = 0;
	bool restart = true;
	double value = 0;
	if (op == Op::PLAY && !lua_isnoneornil(state, 2)) {
		luaL_checktype(state, 2, LUA_TBOOLEAN);
		restart = lua_toboolean(state, 2) != 0;
	}
	if (op == Op::EMIT) {
		const auto n = luaL_checkinteger(state, 2);
		if (n < 0 || n > ParticleEmitterComponent::kMaximumParticles)
			return luaL_error(state, "Emit count must be between 0 and 4096.");
		count = static_cast<int>(n);
	}
	if (!Perform(call, op, id, count, restart, value)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}
	if (op == Op::COUNT)
		lua_pushinteger(state, static_cast<lua_Integer>(value));
	else
		lua_pushboolean(state, value != 0);
	return 1;
}
} // namespace

void LuaInternal::AddParticleMethods(lua_State *state) {
	const char *names[]{"play_particles",     "stop_particles",
	                    "clear_particles",    "emit_particles",
	                    "get_particle_count", "is_emitting_particles"};
	for (int i = 0; i < 6; ++i) {
		lua_pushinteger(state, i);
		lua_pushcclosure(state, Dispatch, 1);
		lua_setfield(state, -2, names[i]);
	}
}
