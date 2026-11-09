#include "LuaStateData.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using Json = nlohmann::json;
const char kArrayMarker = 0;

/// <summary>
/// JSONの値を再帰的に確認し、Luaへ損失なく渡せる範囲に制限する
/// </summary>
/// <param name="value">確認する値</param>
/// <param name="depth">ルートを0とした深さ</param>
/// <param name="count">確認済み値の件数</param>
/// <returns>true:有効 false:型・深さ・件数・整数範囲が無効</returns>
bool Valid(const Json &value, int depth, std::size_t &count) {
	if (depth > LuaScript::kMaximumStateDepth ||
	    ++count > LuaScript::kMaximumStateValues)
		return false;
	if (value.is_object() || value.is_array()) {
		for (auto i = value.cbegin(); i != value.cend(); ++i)
			if (!Valid(i.value(), depth + 1, count))
				return false;
		return true;
	}

	if (value.is_number_unsigned())
		return value.get<std::uint64_t>() <=
		       std::uint64_t((std::numeric_limits<lua_Integer>::max)());
	if (value.is_number_float())
		return std::isfinite(value.get<double>());
	return value.is_number_integer() || value.is_boolean() || value.is_string();
}

/// <summary>
/// 生のテーブルだけを読み、関数や循環する値、疎な配列を状態へ持ち込まない
/// </summary>
/// <param name="state">スタックの余裕を確保済みのLua状態</param>
/// <param name="index">読み込む値の絶対位置</param>
/// <param name="depth">現在の深さ</param>
/// <param name="values">読み込み済み値の件数</param>
/// <param name="bytes">文字列の合計バイト数</param>
/// <param name="parents">祖先テーブルのアドレス。Luaの外には返さない</param>
/// <returns>読み込んだJSON。無効な値はC++例外</returns>
Json Read(
    lua_State *state, int index, int depth, std::size_t &values,
    std::size_t &bytes,
    std::array<const void *, LuaScript::kMaximumStateDepth + 1> &parents) {
	if (depth > LuaScript::kMaximumStateDepth ||
	    ++values > LuaScript::kMaximumStateValues)
		throw std::runtime_error(
		    "Script state exceeds its depth or value limit.");
	switch (lua_type(state, index)) {
	case LUA_TBOOLEAN:
		return bool(lua_toboolean(state, index));
	case LUA_TNUMBER:
		if (lua_isinteger(state, index))
			return std::int64_t(lua_tointeger(state, index));
		if (!std::isfinite(lua_tonumber(state, index)))
			throw std::runtime_error("Script state numbers must be finite.");
		return lua_tonumber(state, index);
	case LUA_TSTRING: {
		std::size_t size = 0;
		const char *text = lua_tolstring(state, index, &size);
		if (size > LuaScript::kMaximumStateBytes - bytes)
			throw std::runtime_error(
			    "Script state strings exceed the size limit.");
		bytes += size;
		return std::string(text, size);
	}
	case LUA_TTABLE:
		break;
	default:
		throw std::runtime_error("Script state supports numbers, booleans, "
		                         "strings and tables only.");
	}

	const void *identity = lua_topointer(state, index);
	if (std::find(parents.begin(), parents.begin() + depth, identity) !=
	    parents.begin() + depth)
		throw std::runtime_error("Script state cannot contain cyclic tables.");
	parents[depth] = identity;
	bool strings = true, integers = true, marked = false;
	std::size_t count = 0, maximum = 0;

	if (lua_getmetatable(state, index)) {
		lua_rawgetp(state, LUA_REGISTRYINDEX, &kArrayMarker);
		marked = lua_rawequal(state, -1, -2) != 0;
		lua_pop(state, 2);
	}

	lua_pushnil(state);

	while (lua_next(state, index)) {
		if (++count > LuaScript::kMaximumStateValues)
			throw std::runtime_error("Script state has too many table fields.");
		const bool string = lua_type(state, -2) == LUA_TSTRING;
		const bool integer = lua_isinteger(state, -2) &&
		                     lua_tointeger(state, -2) > 0 &&
		                     lua_tointeger(state, -2) <=
		                         lua_Integer(LuaScript::kMaximumStateValues);
		strings &= string;
		integers &= integer;
		if (integer)
			maximum =
			    (std::max)(maximum,
			               static_cast<std::size_t>(lua_tointeger(state, -2)));
		lua_pop(state, 1);
	}

	if (count && ((!strings && !integers) || (integers && maximum != count)))
		throw std::runtime_error("Script state tables need string keys or "
		                         "contiguous array indices.");
	const bool array = (count && integers) || (!count && marked);
	Json result = array ? Json::array() : Json::object();

	if (array) {
		for (std::size_t i = 1; i <= count; ++i) {
			lua_rawgeti(state, index, static_cast<lua_Integer>(i));
			result.push_back(Read(state, lua_gettop(state), depth + 1, values,
			                      bytes, parents));
			lua_pop(state, 1);
		}
	} else {
		lua_pushnil(state);

		while (lua_next(state, index)) {
			std::size_t size = 0;
			const char *key = lua_tolstring(state, -2, &size);
			if (size > LuaScript::kMaximumStateBytes - bytes)
				throw std::runtime_error(
				    "Script state keys exceed the size limit.");
			bytes += size;
			result[std::string(key, size)] = Read(
			    state, lua_gettop(state), depth + 1, values, bytes, parents);
			lua_pop(state, 1);
		}
	}

	return result;
}
} // namespace

bool LuaInternal::ValidState(const nlohmann::json &value, bool root) {
	std::size_t count = 0;
	return (!root || value.is_object()) && Valid(value, 0, count);
}

void LuaInternal::PrepareState(lua_State *state) {
	lua_newtable(state);
	lua_pushliteral(state, "Script array");
	lua_setfield(state, -2, "__metatable");
	lua_rawsetp(state, LUA_REGISTRYINDEX, &kArrayMarker);
}

LuaInternal::StateNode LuaInternal::BuildState(const nlohmann::json &value) {
	StateNode node;

	if (value.is_object() || value.is_array()) {
		node.kind = value.is_object() ? StateNode::Kind::OBJECT
		                              : StateNode::Kind::ARRAY;
		node.children.reserve(value.size());

		for (auto i = value.cbegin(); i != value.cend(); ++i) {
			auto child = BuildState(i.value());
			if (value.is_object())
				child.key = i.key();
			node.children.push_back(std::move(child));
		}
	} else if (value.is_boolean()) {
		node.kind = StateNode::Kind::BOOLEAN;
		node.boolean = value.get<bool>();
	} else if (value.is_number_integer()) {
		node.kind = StateNode::Kind::INTEGER;
		node.integer = value.get<lua_Integer>();
	} else if (value.is_number()) {
		node.kind = StateNode::Kind::NUMBER;
		node.number = value.get<lua_Number>();
	} else {
		node.kind = StateNode::Kind::STRING;
		node.text = value.get_ref<const std::string &>();
	}

	return node;
}

void LuaInternal::PushState(lua_State *state, const StateNode &value) {
	// このフレームには参照・整数だけを持ち、LuaのlongjmpにC++の所有値を跨がせない。
	switch (value.kind) {
	case StateNode::Kind::OBJECT:
		lua_createtable(state, 0, static_cast<int>(value.children.size()));

		for (std::size_t i = 0; i < value.children.size(); ++i) {
			const auto &child = value.children[i];
			lua_pushlstring(state, child.key.data(), child.key.size());
			PushState(state, child);
			lua_rawset(state, -3);
		}

		break;
	case StateNode::Kind::ARRAY:
		lua_createtable(state, static_cast<int>(value.children.size()), 0);
		lua_rawgetp(state, LUA_REGISTRYINDEX, &kArrayMarker);
		lua_setmetatable(state, -2);

		for (std::size_t i = 0; i < value.children.size(); ++i) {
			PushState(state, value.children[i]);
			lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
		}

		break;
	case StateNode::Kind::BOOLEAN:
		lua_pushboolean(state, value.boolean);
		break;
	case StateNode::Kind::INTEGER:
		lua_pushinteger(state, value.integer);
		break;
	case StateNode::Kind::NUMBER:
		lua_pushnumber(state, value.number);
		break;
	case StateNode::Kind::STRING:
		lua_pushlstring(state, value.text.data(), value.text.size());
		break;
	}
}

bool LuaInternal::ReadState(lua_State *state, int index, Call &call) noexcept {
	return ReadState(state, index, call.result, call.error);
}

bool LuaInternal::ReadState(lua_State *state, int index, std::string &result,
                            std::string &failure) noexcept {
	try {
		std::array<const void *, LuaScript::kMaximumStateDepth + 1> parents{};
		std::size_t count = 0, bytes = 0;
		auto value =
		    Read(state, lua_absindex(state, index), 0, count, bytes, parents);
		if (!value.is_object())
			throw std::runtime_error(
			    "The state root must remain an object with string keys.");
		auto text = value.dump();
		if (text.size() > LuaScript::kMaximumStateBytes)
			throw std::runtime_error("Script state exceeds 64 KiB.");
		result.swap(text);
		return true;
	} catch (const std::exception &error) {
		failure.assign(error.what(),
		               (std::min)(std::strlen(error.what()), std::size_t(512)));
		return false;
	} catch (...) {
		failure.assign("Could not read Script state.");
		return false;
	}
}
