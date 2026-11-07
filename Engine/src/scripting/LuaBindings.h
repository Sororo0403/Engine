#pragma once

#include "gameplay/CharacterInput.h"
#include "gameplay/GameActionState.h"
#include "scene/SceneAudioAction.h"
#include "scene/SceneObjectId.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
class Scene;
struct UIInput;
struct CharacterRuntimeState;
class ScenePhysics;
class ScenePrefabs;
class ScriptMailbox;
class SceneAnimations;
class SceneParticles;
class SceneAnimators;
struct ScriptMessage;

namespace LuaInternal {
struct StateNode;

/// <summary>
/// 保護されたLua呼び出しの間だけ使用する、所有権を持たない実行情報
/// </summary>
struct Call {
	Scene *scene = nullptr;
	ScenePhysics *physics = nullptr;
	const ScenePrefabs *prefabs = nullptr;
	std::vector<SceneObjectId> *pendingDestructions = nullptr;
	int destructions = 0;
	std::vector<SceneAudioAction> *audioActions = nullptr;
	int audioOperations = 0;
	std::filesystem::path *sceneRequest = nullptr;
	int sceneRequests = 0;
	ScriptMailbox *mailbox = nullptr;
	const ScriptMessage *message = nullptr;
	const StateNode *messageInitial = nullptr;
	std::string messageData;
	int messagesSent = 0;
	int *remainingSceneRequests = nullptr;
	int *remainingAudioOperations = nullptr;
	int *remainingDestructions = nullptr;
	int instantiations = 0;
	int *remainingInstantiations = nullptr;
	std::size_t *remainingSpawnedObjects = nullptr;
	bool physicsDirty = true;
	int physicsOperations = 0;
	int *remainingPhysicsOperations = nullptr;
	SceneObjectId self = 0, other = 0;
	const CharacterInput *input = nullptr;
	const UIInput *viewport = nullptr;
	const CharacterRuntimeState *character = nullptr;
	std::span<const GameActionState> actions;
	SceneAnimations *animations = nullptr;
	int animationOperations = 0;
	SceneParticles *particles = nullptr;
	SceneAnimators *animators = nullptr;
	int animatorOperations = 0;
	int *remainingAnimatorOperations = nullptr;
	int particleOperations = 0;
	int *remainingParticleOperations = nullptr;
	int *remainingAnimationOperations = nullptr;
	const StateNode *initial = nullptr;
	const StateNode *sharedInitial = nullptr;
	std::string sharedInput, sharedResult;
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
/// 直前の物体メソッドテーブルへ、実行中の剛体速度・力積のAPIを登録する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
void AddPhysicsMethods(lua_State *state);

/// <summary>
/// 直前の物体メソッドテーブルへ、Audio Sourceの再生・停止・音量操作を登録する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>

void AddAudioMethods(lua_State *state);

/// <summary>
/// 直前の物体メソッド表へ、Transform Animationの再生・停止・シークを登録する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>

void AddAnimationMethods(lua_State *state);

/// <summary>
/// 直前の物体メソッド表へ、粒子の発生・停止・消去と状態参照を登録する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
void AddParticleMethods(lua_State *state);

/// <summary>
/// 直前の物体メソッド表へ、モデル内クリップの再生・停止・指定・参照を登録する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
void AddAnimatorMethods(lua_State *state);

/// <summary>
/// 直前の物体メソッドテーブルへ、名前付きシーンへの遷移要求を登録する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
void AddSceneMethods(lua_State *state);

/// <summary>
/// 直前の物体メソッドテーブルへ、次の更新に通知を送るAPIを登録する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
void AddMessageMethods(lua_State *state);

/// <summary>
/// 直前の物体メソッドテーブルへ、名前付きPrefabの生成を登録する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
void AddPrefabMethods(lua_State *state);

/// <summary>
/// 直前のsceneテーブルへ、現在のColliderを検索するレイキャストと範囲検索を登録する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
void AddSpatialQueryMethods(lua_State *state);

/// <summary>
/// 物体IDを、アドレスを公開しない読み取り専用userdataへ変換する
/// </summary>
/// <param name="state">Lua状態</param>
/// <param name="id">物体ID。0はnil</param>
void PushObject(lua_State *state, SceneObjectId id);
} // namespace LuaInternal
