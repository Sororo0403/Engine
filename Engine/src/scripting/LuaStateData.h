#pragma once
#include "LuaBindings.h"
#include <json.hpp>

namespace LuaInternal {
/// <summary>
/// Luaの確保失敗時にC++のイテレーターを跨がないよう、事前に準備する読み取り専用の状態ツリー
/// </summary>
struct StateNode {
	enum class Kind { OBJECT, ARRAY, BOOLEAN, INTEGER, NUMBER, STRING };
	Kind kind = Kind::OBJECT;
	std::string_view key, text;
	lua_Integer integer = 0;
	lua_Number number = 0;
	bool boolean = false;
	std::vector<StateNode> children;
};

/// <summary>
/// 検証済みJSONから、Lua APIを使用しない事前変換ツリーを作る
/// </summary>
/// <param name="value">ツリーより長く保持する検証済みJSON</param>
/// <returns>入力JSONの文字列を参照する状態ツリー</returns>
StateNode BuildState(const nlohmann::json &value);

/// <summary>
/// JSON状態の型と件数を確認する。Lua APIは使用しない
/// </summary>
/// <param name="value">確認する値</param>
/// <param name="root">true:ルートにオブジェクトを要求する</param>
/// <returns>true:有効 false:型・深さ・件数が範囲外</returns>
bool ValidState(const nlohmann::json &value, bool root = true);

/// <summary>
/// 事前検証したJSONを、Luaの保護呼び出し内でテーブルへ変換する
/// </summary>
/// <param name="state">Lua状態</param>
/// <param name="value">Lua呼び出しより長く保持するJSON</param>
void PushState(lua_State *state, const StateNode &value);

/// <summary>
/// JSON配列の空状態を保持する内部メタデータを、保護呼び出し内で準備する
/// </summary>
/// <param name="state">Lua状態</param>
void PrepareState(lua_State *state);

/// <summary>
/// メタメソッドを実行せずstateを読み、成功時だけ実行結果へJSONを保持する
/// </summary>
/// <param name="state">保護呼び出し内でスタックに余裕を確保したLua状態</param>
/// <param name="index">stateテーブルの位置</param>
/// <param name="call">結果と失敗理由</param>
/// <returns>true:読み取り成功 false:型・循環・サイズ・深さが無効</returns>
bool ReadState(lua_State *state, int index, Call &call) noexcept;
} // namespace LuaInternal
