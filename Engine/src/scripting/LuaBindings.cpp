#include "LuaBindings.h"
#include "math/TransformUtils.h"
#include "platform/windows/StringConversion.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
constexpr const char *kObjectType = "Engine.ScriptObject";
enum class Op {
	NAME,
	TAG,
	SET_NAME,
	ID,
	VALID,
	DESTROY,
	PARENT,
	SET_PARENT,
	FIND_CHILD,
	REFERENCE,
	ACTIVE,
	SET_ACTIVE,
	POSITION,
	SET_POSITION,
	LOCAL_POSITION,
	SET_LOCAL_POSITION,
	ROTATION,
	SET_ROTATION,
	LOOK_AT,
	LOCAL_TO_WORLD,
	WORLD_TO_LOCAL,
	LOCAL_VECTOR_TO_WORLD,
	WORLD_VECTOR_TO_LOCAL,
	SCALE,
	SET_SCALE,
	FIND,
	FIND_ID,
	LOG
};

struct Result {
	SceneObjectId id = 0;
	float numbers[4]{};
	bool boolean = false;
};

/// <summary>
/// Lua状態に結び付けた実行情報を取得する
/// </summary>
/// <param name="state">Engineが作成したLua状態</param>
/// <returns>呼び出しより長く保持する実行情報</returns>
LuaInternal::Call &Context(lua_State *state) {
	return **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
}

/// <summary>
/// Lua APIを呼ばずに物体の問い合わせ・変更を行い、C++例外を境界内で処理する
/// </summary>
/// <param name="call">作業シーンと結果文字列の保持先</param>
/// <param name="op">処理種別</param>
/// <param name="id">対象ID。検索・ログは0</param>
/// <param name="text">Luaが保持している文字列</param>
/// <param name="length">文字列のバイト数</param>
/// <param name="numbers">有限な入力数値</param>
/// <param name="flag">有効設定またはワールド姿勢の維持</param>
/// <param name="parent">親の設定で指定するID。0はルート</param>
/// <param name="result">成功時の数値・ID・真偽値</param>
/// <returns>true:処理成功 false:理由をcall.errorへ設定。Luaのエラー送出は呼び出し側で行う</returns>
bool Perform(LuaInternal::Call &call, Op op, SceneObjectId id, const char *text,
             std::size_t length, const float *numbers, bool flag,
             SceneObjectId parent, Result &result) noexcept {
	try {
		if (!call.scene)
			throw std::runtime_error("A scene is required for object APIs.");
		const bool mutation =
		    op == Op::DESTROY || op == Op::SET_NAME || op == Op::SET_ACTIVE ||
		    op == Op::SET_PARENT || op == Op::SET_POSITION ||
		    op == Op::SET_LOCAL_POSITION || op == Op::SET_ROTATION ||
		    op == Op::LOOK_AT || op == Op::SET_SCALE || op == Op::LOG;
		if (mutation && !call.executing)
			throw std::runtime_error(
			    "Change objects inside callbacks, not at module scope.");
		if (op == Op::LOG) {
			if (length > LuaScript::kMaximumLogBytes || call.logs.size() >= 32)
				throw std::runtime_error("Script log limit exceeded.");
			call.returnText.assign(text, length);
			if (call.returnText.find('\0') != std::string::npos ||
			    (!call.returnText.empty() &&
			     StringConversion::Utf8ToWide(call.returnText).empty()))
				throw std::runtime_error("Script logs must use UTF-8 text.");
			call.logs.push_back(call.returnText);
			return true;
		}
		if (op == Op::FIND) {
			const std::string_view name(text, length);
			for (const auto &[candidate, object] : call.scene->GetObjects())
				if (object.name == name) {
					result.id = candidate;
					break;
				}
			return true;
		}
		if (op == Op::FIND_ID) {
			SceneObjectId parsed = 0;
			const auto converted = std::from_chars(text, text + length, parsed);
			if (converted.ec != std::errc{} || converted.ptr != text + length)
				throw std::runtime_error(
				    "Object IDs are unsigned decimal strings.");
			result.id = call.scene->Contains(parsed) ? parsed : 0;
			return true;
		}
		if (op == Op::ID) {
			call.returnText = std::to_string(id);
			return true;
		}
		if (op == Op::VALID) {
			result.boolean = call.scene->Contains(id);
			return true;
		}
		if (op == Op::DESTROY) {
			if (!call.pendingDestructions)
				throw std::runtime_error(
				    "Object destruction requires a deferred simulation queue.");
			if (++call.destructions > LuaScript::kMaximumDestructions ||
			    (call.remainingDestructions &&
			     *call.remainingDestructions <= 0))
				throw std::runtime_error(
				    "Script destruction request limit exceeded.");
			if (call.remainingDestructions)
				--*call.remainingDestructions;
			result.boolean = call.scene->Contains(id) &&
			                 std::ranges::find(*call.pendingDestructions, id) ==
			                     call.pendingDestructions->end();
			if (result.boolean)
				call.pendingDestructions->push_back(id);
			return true;
		}
		auto *object = call.scene->FindObject(id);
		if (!object)
			throw std::runtime_error("The referenced object no longer exists.");
		switch (op) {
		case Op::NAME:
			call.returnText = object->name;
			break;
		case Op::TAG:
			call.returnText = object->tag;
			break;
		case Op::SET_NAME:
			if (!length || length > 256)
				throw std::runtime_error(
				    "Object names need 1 to 256 UTF-8 bytes.");
			call.returnText.assign(text, length);
			if (call.returnText.find('\0') != std::string::npos ||
			    StringConversion::Utf8ToWide(call.returnText).empty())
				throw std::runtime_error("Object name is not valid UTF-8.");
			object->name = call.returnText;
			break;
		case Op::ID:
			call.returnText = std::to_string(id);
			break;
		case Op::PARENT:
			result.id = object->parentId;
			break;
		case Op::SET_PARENT:
			if (!SceneHierarchy::SetParent(*call.scene, id, parent, flag))
				throw std::runtime_error(
				    "Parent change would create a cycle or cannot preserve the "
				    "world transform.");
			break;
		case Op::FIND_CHILD: {
			const std::string_view name(text, length);
			for (const auto &[childId, child] : call.scene->GetObjects()) {
				if (child.parentId == id && child.name == name) {
					result.id = childId;
					break;
				}
			}
			break;
		}
		case Op::REFERENCE:
			if (object->script)
				for (const auto &[name, target] : object->script->references) {
					if (name == std::string_view(text, length)) {
						result.id = call.scene->Contains(target) ? target : 0;
						break;
					}
				}
			break;
		case Op::ACTIVE:
			result.boolean = SceneActivity::IsActive(*call.scene, id);
			break;
		case Op::SET_ACTIVE:
			object->active = flag;
			break;
		case Op::POSITION: {
			SceneWorldTransform world;
			if (!SceneHierarchy::GetWorldTransform(*call.scene, id, world))
				throw std::runtime_error("Object world position is invalid.");
			const auto value = world.GetPosition();
			result.numbers[0] = value.x;
			result.numbers[1] = value.y;
			result.numbers[2] = value.z;
			break;
		}
		case Op::LOCAL_TO_WORLD:
		case Op::WORLD_TO_LOCAL:
		case Op::LOCAL_VECTOR_TO_WORLD:
		case Op::WORLD_VECTOR_TO_LOCAL: {
			SceneWorldTransform world;
			if (!SceneHierarchy::GetWorldTransform(*call.scene, id, world))
				throw std::runtime_error("Object world transform is invalid.");
			auto matrix = world.matrix;
			if ((op == Op::WORLD_TO_LOCAL || op == Op::WORLD_VECTOR_TO_LOCAL) &&
			    !TransformUtils::TryInverseWorldMatrix(world.matrix, matrix))
				throw std::runtime_error(
				    "Object world transform cannot be inverted.");
			const bool point =
			    op == Op::LOCAL_TO_WORLD || op == Op::WORLD_TO_LOCAL;
			for (int axis = 0; axis < 3; ++axis) {
				double value = point ? matrix.m[3][axis] : 0.;
				for (int component = 0; component < 3; ++component)
					value +=
					    double(numbers[component]) * matrix.m[component][axis];
				if (!std::isfinite(value) ||
				    std::abs(value) > (std::numeric_limits<float>::max)())
					throw std::runtime_error(
					    "Converted coordinates cannot be represented.");
				result.numbers[axis] = static_cast<float>(value);
			}
			break;
		}
		case Op::SET_POSITION:
			if (!SceneHierarchy::SetWorldPosition(
			        *call.scene, id, {numbers[0], numbers[1], numbers[2]}))
				throw std::runtime_error(
				    "World position cannot be represented in this hierarchy.");
			break;
		case Op::LOOK_AT:
			if (!SceneHierarchy::LookAt(*call.scene, id,
			                            {numbers[0], numbers[1], numbers[2]}))
				throw std::runtime_error(
				    "Look-at target or object hierarchy is invalid.");
			break;
		case Op::LOCAL_POSITION:
			result.numbers[0] = object->transform.translation.x;
			result.numbers[1] = object->transform.translation.y;
			result.numbers[2] = object->transform.translation.z;
			break;
		case Op::SET_LOCAL_POSITION:
			object->transform.translation = {numbers[0], numbers[1],
			                                 numbers[2]};
			break;
		case Op::ROTATION:
			result.numbers[0] = object->transform.rotation.x;
			result.numbers[1] = object->transform.rotation.y;
			result.numbers[2] = object->transform.rotation.z;
			result.numbers[3] = object->transform.rotation.w;
			break;
		case Op::SET_ROTATION: {
			DirectX::XMFLOAT4 value;
			if (!TransformUtils::TryNormalizeRotation(
			        {numbers[0], numbers[1], numbers[2], numbers[3]}, value))
				throw std::runtime_error(
				    "Rotation needs a nonzero finite quaternion.");
			object->transform.rotation = value;
			break;
		}
		case Op::SCALE:
			result.numbers[0] = object->transform.scale.x;
			result.numbers[1] = object->transform.scale.y;
			result.numbers[2] = object->transform.scale.z;
			break;
		case Op::SET_SCALE: {
			auto value = object->transform;
			value.scale = {numbers[0], numbers[1], numbers[2]};
			if (!TransformUtils::IsValid(value))
				throw std::runtime_error("Object scale is invalid.");
			object->transform = value;
			break;
		}
		case Op::FIND:
		case Op::FIND_ID:
		case Op::LOG:
			break;
		}
		if (op == Op::SET_ACTIVE || op == Op::SET_PARENT ||
		    op == Op::SET_POSITION || op == Op::SET_LOCAL_POSITION ||
		    op == Op::SET_ROTATION ||
		    (op == Op::SET_SCALE || op == Op::LOOK_AT))
			call.physicsDirty = true;
		return true;
	} catch (const std::exception &error) {
		call.error.assign(error.what(), (std::min)(std::strlen(error.what()),
		                                           std::size_t(512)));
		return false;
	} catch (...) {
		call.error.assign("Object API failed.");
		return false;
	}
}

/// <summary>
/// Luaの引数を検証し、C++の所有値を持たないフレームからAPIを呼び出す
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
/// <returns>Luaへ返した値の数。無効な引数や処理失敗はLuaエラー</returns>
int Dispatch(lua_State *state) {
	const auto op = static_cast<Op>(lua_tointeger(state, lua_upvalueindex(1)));
	auto &call = Context(state);
	SceneObjectId id = 0;
	const char *text = nullptr;
	std::size_t length = 0;
	float numbers[4]{};
	bool flag = false;
	SceneObjectId parent = 0;
	if (op != Op::FIND && op != Op::FIND_ID && op != Op::LOG)
		id = *static_cast<SceneObjectId *>(
		    luaL_checkudata(state, 1, kObjectType));
	if (op == Op::NAME || op == Op::TAG || op == Op::POSITION ||
	    op == Op::LOCAL_POSITION || op == Op::ROTATION || op == Op::SCALE ||
	    op == Op::ACTIVE || op == Op::PARENT || op == Op::ID ||
	    op == Op::VALID || op == Op::DESTROY) {
		// 引数はselfだけ。値はPerformで取得する。
	} else if (op == Op::SET_NAME || op == Op::REFERENCE ||
	           op == Op::FIND_CHILD || op == Op::FIND || op == Op::FIND_ID ||
	           op == Op::LOG) {
		const int index =
		    (op == Op::SET_NAME || op == Op::REFERENCE || op == Op::FIND_CHILD)
		        ? 2
		        : 1;
		luaL_checktype(state, index, LUA_TSTRING);
		text = lua_tolstring(state, index, &length);
	} else if (op == Op::SET_PARENT) {
		if (!lua_isnil(state, 2))
			parent = *static_cast<SceneObjectId *>(
			    luaL_checkudata(state, 2, kObjectType));
		flag = true;
		if (!lua_isnoneornil(state, 3)) {
			luaL_checktype(state, 3, LUA_TBOOLEAN);
			flag = lua_toboolean(state, 3) != 0;
		}
	} else if (op == Op::SET_ACTIVE) {
		luaL_checktype(state, 2, LUA_TBOOLEAN);
		flag = lua_toboolean(state, 2) != 0;
	} else {
		const int count = op == Op::SET_ROTATION ? 4 : 3;
		for (int i = 0; i < count; ++i) {
			luaL_checktype(state, i + 2, LUA_TNUMBER);
			const double value = lua_tonumber(state, i + 2);
			if (!std::isfinite(value) ||
			    std::abs(value) > (std::numeric_limits<float>::max)())
				return luaL_error(state,
				                  "Transform values must be finite floats.");
			numbers[i] = static_cast<float>(value);
		}
	}
	Result result;
	if (!Perform(call, op, id, text, length, numbers, flag, parent, result)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}
	switch (op) {
	case Op::NAME:
	case Op::TAG:
	case Op::ID:
		lua_pushlstring(state, call.returnText.data(), call.returnText.size());
		return 1;
	case Op::PARENT:
	case Op::FIND_CHILD:
	case Op::REFERENCE:
	case Op::FIND:
	case Op::FIND_ID:
		LuaInternal::PushObject(state, result.id);
		return 1;
	case Op::ACTIVE:
	case Op::VALID:
	case Op::DESTROY:
		lua_pushboolean(state, result.boolean);
		return 1;
	case Op::POSITION:
	case Op::LOCAL_TO_WORLD:
	case Op::WORLD_TO_LOCAL:
	case Op::LOCAL_VECTOR_TO_WORLD:
	case Op::WORLD_VECTOR_TO_LOCAL:
	case Op::LOCAL_POSITION:
	case Op::SCALE:
		for (int i = 0; i < 3; ++i)
			lua_pushnumber(state, result.numbers[i]);
		return 3;
	case Op::ROTATION:
		for (int i = 0; i < 4; ++i)
			lua_pushnumber(state, result.numbers[i]);
		return 4;
	default:
		return 0;
	}
}

constexpr std::size_t kMaximumObjectResults = 256;

/// <summary>
/// タグが完全一致する物体を、指定件数までID順に集める
/// </summary>
bool CollectTagged(LuaInternal::Call &call, std::string_view tag,
                   bool includeInactive, std::span<SceneObjectId> matches,
                   std::size_t &count, bool &truncated) noexcept {
	try {
		if (!call.scene || tag.empty() || !SceneObject::IsValidTag(tag))
			throw std::runtime_error(
			    "Tag search requires a scene and 1 to 64 UTF-8 bytes.");
		SceneActivity::States active;
		if (!includeInactive &&
		    !SceneActivity::BuildStates(*call.scene, active))
			throw std::runtime_error("Tag search requires a valid hierarchy.");
		for (const auto &[id, object] : call.scene->GetObjects()) {
			if (object.tag != tag || (!includeInactive && !active.at(id)))
				continue;
			if (count == matches.size()) {
				truncated = true;
				break;
			}
			matches[count++] = id;
		}
		return true;
	} catch (const std::exception &e) {
		call.error = e.what();
		return false;
	} catch (...) {
		call.error = "Tag search failed.";
		return false;
	}
}

/// <summary>
/// 物体の配列と、件数上限で省略されたかをLuaへ返す
/// </summary>
int PushObjectResults(lua_State *state, std::span<const SceneObjectId> objects,
                      bool truncated) {
	lua_createtable(state, static_cast<int>(objects.size()), 0);
	for (std::size_t i = 0; i < objects.size(); ++i) {
		LuaInternal::PushObject(state, objects[i]);
		lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
	}
	lua_pushboolean(state, truncated);
	return 2;
}

/// <summary>
/// 直接の子をID順に列挙する。無効な子も含める
/// </summary>
int GetChildren(lua_State *state) {
	const auto id =
	    *static_cast<SceneObjectId *>(luaL_checkudata(state, 1, kObjectType));
	const auto *scene = Context(state).scene;
	if (!scene || !scene->Contains(id))
		return luaL_error(state, "The referenced object no longer exists.");
	std::array<SceneObjectId, kMaximumObjectResults> children{};
	std::size_t count = 0;
	for (const auto &[childId, object] : scene->GetObjects()) {
		if (object.parentId != id)
			continue;
		if (count == children.size())
			return PushObjectResults(state, children, true);
		children[count++] = childId;
	}
	return PushObjectResults(state, std::span{children}.first(count), false);
}

/// <summary>
/// タグが一致した物体の配列と、上限を超えたかを返す
/// </summary>
int FindAllByTag(lua_State *state) {
	luaL_checktype(state, 1, LUA_TSTRING);
	std::size_t length = 0;
	const char *tag = lua_tolstring(state, 1, &length);
	if (!lua_isnoneornil(state, 2))
		luaL_checktype(state, 2, LUA_TBOOLEAN);
	const bool includeInactive = lua_toboolean(state, 2) != 0;
	std::array<SceneObjectId, kMaximumObjectResults> matches{};
	std::size_t count = 0;
	bool truncated = false;
	auto &call = Context(state);
	if (!CollectTagged(call, {tag, length}, includeInactive, matches, count,
	                   truncated)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}
	return PushObjectResults(state, std::span{matches}.first(count), truncated);
}

/// <summary>
/// 同じLua状態にある物体参照を、アドレスではなくIDで比較する
/// </summary>
/// <param name="state">比較するuserdataを持つLua状態</param>
/// <returns>比較結果1個</returns>
int EqualObjects(lua_State *state) {
	const auto *left =
	    static_cast<SceneObjectId *>(luaL_testudata(state, 1, kObjectType));
	const auto *right =
	    static_cast<SceneObjectId *>(luaL_testudata(state, 2, kObjectType));
	lua_pushboolean(state, left && right && *left == *right);
	return 1;
}

/// <summary>
/// 操作番号を保持するC関数を、直前のテーブルへ登録する
/// </summary>
/// <param name="state">Lua状態</param>
/// <param name="name">公開名</param>
/// <param name="op">処理種別</param>
void Method(lua_State *state, const char *name, Op op) {
	lua_pushinteger(state, static_cast<lua_Integer>(op));
	lua_pushcclosure(state, Dispatch, 1);
	lua_setfield(state, -2, name);
}

/// <summary>
/// 標準ライブラリから明示した関数と定数だけを環境へ複製する
/// </summary>
/// <param name="state">Lua状態</param>
/// <param name="environment">環境テーブルの位置</param>
/// <param name="name">公開するライブラリ名</param>
/// <param name="open">標準ライブラリの初期化処理</param>
/// <param name="names">許可する名前。nullptrで終える</param>
void Library(lua_State *state, int environment, const char *name,
             lua_CFunction open, const char *const *names) {
	luaL_requiref(state, name, open, 0);
	const int library = lua_gettop(state);
	lua_newtable(state);
	for (int i = 0; names[i]; ++i) {
		lua_getfield(state, library, names[i]);
		lua_setfield(state, -2, names[i]);
	}
	lua_setfield(state, environment, name);
	lua_pop(state, 1);
}
} // namespace

void LuaInternal::PushObject(lua_State *state, SceneObjectId id) {
	if (!id) {
		lua_pushnil(state);
		return;
	}
	*static_cast<SceneObjectId *>(
	    lua_newuserdatauv(state, sizeof(SceneObjectId), 0)) = id;
	luaL_getmetatable(state, kObjectType);
	lua_setmetatable(state, -2);
}

int LuaInternal::OpenEnvironment(lua_State *state, Call &call) {
	*static_cast<Call **>(lua_getextraspace(state)) = &call;
	luaL_requiref(state, "_G", luaopen_base, 1);
	lua_pop(state, 1);
	lua_newtable(state);
	const int environment = lua_gettop(state);
	const char *const base[]{"assert", "error",    "ipairs",   "next",
	                         "pairs",  "rawequal", "rawget",   "rawlen",
	                         "rawset", "select",   "tonumber", "tostring",
	                         "type",   "_VERSION", nullptr};
	for (int i = 0; base[i]; ++i) {
		lua_getglobal(state, base[i]);
		lua_setfield(state, environment, base[i]);
	}
	lua_pushvalue(state, environment);
	lua_setfield(state, environment, "_G");
	const char *const math[]{
	    "abs", "acos",       "asin", "atan", "ceil", "cos", "deg",
	    "exp", "floor",      "fmod", "huge", "log",  "max", "maxinteger",
	    "min", "mininteger", "modf", "pi",   "rad",  "sin", "sqrt",
	    "tan", "tointeger",  "type", "ult",  nullptr};
	const char *const string[]{"byte", "char",    "format", "len",   "lower",
	                           "rep",  "reverse", "sub",    "upper", nullptr};
	const char *const table[]{"concat", "insert", "move",   "pack",
	                          "remove", "sort",   "unpack", nullptr};
	const char *const utf8[]{"char", "charpattern", "codepoint", "codes",
	                         "len",  "offset",      nullptr};
	Library(state, environment, "math", luaopen_math, math);
	Library(state, environment, "string", luaopen_string, string);
	Library(state, environment, "table", luaopen_table, table);
	Library(state, environment, "utf8", luaopen_utf8, utf8);
	// stringのメタテーブルからも、許可していないパターン処理へ到達させない。
	lua_pushliteral(state, "");
	if (lua_getmetatable(state, -1)) {
		lua_getfield(state, environment, "string");
		lua_setfield(state, -2, "__index");
		lua_pop(state, 1);
	}
	lua_pop(state, 1);
	luaL_newmetatable(state, kObjectType);
	lua_pushliteral(state, "Scene object");
	lua_setfield(state, -2, "__metatable");
	Method(state, "__tostring", Op::ID);
	lua_pushcfunction(state, EqualObjects);
	lua_setfield(state, -2, "__eq");
	lua_newtable(state);
	Method(state, "get_name", Op::NAME);
	Method(state, "get_tag", Op::TAG);
	Method(state, "set_name", Op::SET_NAME);
	Method(state, "get_id", Op::ID);
	Method(state, "is_valid", Op::VALID);
	Method(state, "destroy", Op::DESTROY);
	Method(state, "parent", Op::PARENT);
	Method(state, "set_parent", Op::SET_PARENT);
	Method(state, "find_child", Op::FIND_CHILD);
	lua_pushcfunction(state, GetChildren);
	lua_setfield(state, -2, "get_children");
	Method(state, "reference", Op::REFERENCE);
	Method(state, "is_active", Op::ACTIVE);
	Method(state, "set_active", Op::SET_ACTIVE);
	Method(state, "get_position", Op::POSITION);
	Method(state, "set_position", Op::SET_POSITION);
	Method(state, "get_local_position", Op::LOCAL_POSITION);
	Method(state, "set_local_position", Op::SET_LOCAL_POSITION);
	Method(state, "get_rotation", Op::ROTATION);
	Method(state, "set_rotation", Op::SET_ROTATION);
	Method(state, "look_at", Op::LOOK_AT);
	Method(state, "local_to_world", Op::LOCAL_TO_WORLD);
	Method(state, "world_to_local", Op::WORLD_TO_LOCAL);
	Method(state, "local_vector_to_world", Op::LOCAL_VECTOR_TO_WORLD);
	Method(state, "world_vector_to_local", Op::WORLD_VECTOR_TO_LOCAL);
	Method(state, "get_scale", Op::SCALE);
	Method(state, "set_scale", Op::SET_SCALE);
	LuaInternal::AddUIMethods(state);
	LuaInternal::AddPhysicsMethods(state);
	LuaInternal::AddAudioMethods(state);
	AddAnimationMethods(state);
	AddParticleMethods(state);
	AddAnimatorMethods(state);
	LuaInternal::AddSceneMethods(state);
	LuaInternal::AddMessageMethods(state);
	LuaInternal::AddPrefabMethods(state);
	lua_setfield(state, -2, "__index");
	lua_pop(state, 1);
	lua_newtable(state);
	Method(state, "find", Op::FIND);
	Method(state, "find_id", Op::FIND_ID);
	lua_pushcfunction(state, FindAllByTag);
	lua_setfield(state, -2, "find_all_by_tag");
	Method(state, "log", Op::LOG);
	LuaInternal::AddSpatialQueryMethods(state);
	lua_setfield(state, environment, "scene");
	return environment;
}
