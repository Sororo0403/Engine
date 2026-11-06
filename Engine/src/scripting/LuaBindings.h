#pragma once

#include "gameplay/CharacterInput.h"
#include "scene/SceneObjectId.h"
#include <string>
#include <string_view>
#include <vector>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
class Scene;

namespace LuaInternal {
struct StateNode;

/// <summary>
/// 保護されたLua呼び出しの間だけ使用する、所有権を持たない実行情報
/// </summary>
struct Call {
	Scene *scene = nullptr;
	SceneObjectId self = 0, other = 0;
	const CharacterInput *input = nullptr;
	const StateNode *initial = nullptr;
	std::string_view source;
	std::string name;
	const char *callback = nullptr;
	float seconds = 0;
	bool executing = false;
	std::string result, error, returnText;
	std::vector<std::string> logs;
	int instructions = 0;
	int *remainingInstructions = nullptr;
	std::string bytecode;
};

/// <summary>
/// 読み込み可能な標準関数と物体APIだけを環境へ準備する。Luaの保護呼び出し内で実行する
/// </summary>
/// <param name="state">初期化済みLua状態</param>
/// <param name="call">実行情報。状態より長く保持する</param>
/// <returns>スタックへ積んだ環境テーブルの位置</returns>
int OpenEnvironment(lua_State *state, Call &call);

/// <summary>
/// 直前の物体メソッドテーブルへ、文字・矩形・UI色のAPIを登録する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
void AddUIMethods(lua_State *state);

/// <summary>
/// 物体IDを、アドレスを公開しない読み取り専用userdataへ変換する
/// </summary>
/// <param name="state">Lua状態</param>
/// <param name="id">物体ID。0はnil</param>
void PushObject(lua_State *state, SceneObjectId id);
} // namespace LuaInternal
