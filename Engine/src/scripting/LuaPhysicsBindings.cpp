#include "LuaBindings.h"
#include "gameplay/ScenePhysics.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace {
constexpr const char *kObjectType = "Engine.ScriptObject";
enum class Op {
	VELOCITY,
	SET_VELOCITY,
	ANGULAR_VELOCITY,
	SET_ANGULAR_VELOCITY,
	IMPULSE,
	SLEEPING,
	DYNAMIC
};

/// <summary>
/// Lua APIを呼ばずに候補の物理状態を読み書きし、所有するC++値をここで破棄する
/// </summary>
/// <param name="call">候補シーン・物理状態と、残りの実行回数</param>
/// <param name="op">問い合わせ・変更の種類</param>
/// <param name="id">操作する剛体のID</param>
/// <param name="numbers">3成分の入出力。休止状態では先頭だけ使用する</param>
/// <returns>true:成功 false:失敗理由をcall.errorへ設定する</returns>
bool Perform(LuaInternal::Call &call, Op op, SceneObjectId id,
             float *numbers) noexcept {
	try {
		const bool mutation = op == Op::SET_VELOCITY ||
		                      op == Op::SET_ANGULAR_VELOCITY ||
		                      op == Op::IMPULSE;
		if (mutation && !call.executing)
			throw std::runtime_error("Change Rigid Body values inside "
			                         "callbacks, not at module scope.");
		if (++call.physicsOperations > LuaScript::kMaximumPhysicsOperations ||
		    (call.remainingPhysicsOperations &&
		     *call.remainingPhysicsOperations <= 0))
			throw std::runtime_error("Script physics API call limit exceeded.");
		if (call.remainingPhysicsOperations)
			--*call.remainingPhysicsOperations;
		if (!call.scene)
			throw std::runtime_error("Rigid Body APIs require a scene.");
		const auto *object = call.scene->FindObject(id);
		if (!object)
			throw std::runtime_error("The Rigid Body object no longer exists.");
		if (op == Op::DYNAMIC) {
			numbers[0] = object->rigidBody && object->rigidBody->enabled &&
			                     !object->rigidBody->isKinematic &&
			                     SceneActivity::IsActive(*call.scene, id)
			                 ? 1.0f
			                 : 0.0f;
			return true;
		}
		if (!call.physics)
			throw std::runtime_error(
			    "Rigid Body APIs require a physical runtime.");
		if (!object->rigidBody || !object->rigidBody->enabled ||
		    !SceneActivity::IsActive(*call.scene, id))
			throw std::runtime_error(
			    "This object needs an active, enabled Rigid Body.");
		std::string error;
		if (call.physicsDirty) {
			if (!call.physics->Configure(*call.scene, true, error))
				throw std::runtime_error(error);
			call.physicsDirty = false;
		}
		const auto *world = call.physics->GetWorld();
		const auto state = world ? world->GetState(id) : std::nullopt;
		if (!state)
			throw std::runtime_error(
			    "The Rigid Body runtime no longer exists.");
		if (op == Op::SLEEPING)
			numbers[0] = state->active ? 0.0f : 1.0f;
		else if (op == Op::VELOCITY || op == Op::ANGULAR_VELOCITY) {
			const auto &value = op == Op::VELOCITY ? state->linearVelocity
			                                       : state->angularVelocity;
			numbers[0] = value.x;
			numbers[1] = value.y;
			numbers[2] = value.z;
		} else {
			const DirectX::XMFLOAT3 value{numbers[0], numbers[1], numbers[2]};
			const bool applied =
			    op == Op::SET_VELOCITY
			        ? call.physics->SetLinearVelocity(id, value, error)
			    : op == Op::SET_ANGULAR_VELOCITY
			        ? call.physics->SetAngularVelocity(id, value, error)
			        : call.physics->AddImpulse(id, value, error);
			if (!applied)
				throw std::runtime_error(error);
		}
		return true;
	} catch (const std::exception &exception) {
		call.error.assign(
		    exception.what(),
		    (std::min)(std::strlen(exception.what()), std::size_t(512)));
		return false;
	} catch (...) {
		call.error.assign("Rigid Body API failed.");
		return false;
	}
}

/// <summary>
/// 所有値を持たないCフレームで、Luaの引数検証・結果・エラーを接続する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
/// <returns>返した値の数。無効な型や範囲ではLuaエラーを送る</returns>
int Dispatch(lua_State *state) {
	const auto op = static_cast<Op>(lua_tointeger(state, lua_upvalueindex(1)));
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	const auto id =
	    *static_cast<SceneObjectId *>(luaL_checkudata(state, 1, kObjectType));
	float numbers[3]{};
	if (op == Op::SET_VELOCITY || op == Op::SET_ANGULAR_VELOCITY ||
	    op == Op::IMPULSE)
		for (int i = 0; i < 3; ++i) {
			luaL_checktype(state, i + 2, LUA_TNUMBER);
			const double value = lua_tonumber(state, i + 2);
			if (!std::isfinite(value) ||
			    std::abs(value) > (std::numeric_limits<float>::max)())
				return luaL_error(
				    state, "Rigid Body vectors require finite float values.");
			numbers[i] = static_cast<float>(value);
		}
	if (!Perform(call, op, id, numbers)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}
	if (op == Op::SLEEPING || op == Op::DYNAMIC) {
		lua_pushboolean(state, numbers[0] != 0);
		return 1;
	}
	if (op == Op::VELOCITY || op == Op::ANGULAR_VELOCITY) {
		for (int i = 0; i < 3; ++i)
			lua_pushnumber(state, numbers[i]);
		return 3;
	}
	return 0;
}
} // namespace

void LuaInternal::AddPhysicsMethods(lua_State *state) {
	const char *names[]{"get_velocity",
	                    "set_velocity",
	                    "get_angular_velocity",
	                    "set_angular_velocity",
	                    "add_impulse",
	                    "is_sleeping",
	                    "is_dynamic"};
	static_assert(std::size(names) ==
	              static_cast<std::size_t>(Op::DYNAMIC) + 1);
	for (int i = 0; i < static_cast<int>(std::size(names)); ++i) {
		lua_pushinteger(state, i);
		lua_pushcclosure(state, Dispatch, 1);
		lua_setfield(state, -2, names[i]);
	}
}
