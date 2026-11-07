#include "scripting/LuaScript.h"
#include "LuaBindings.h"
#include "LuaStateData.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "scene/Scene.h"
#include "scripting/ScriptMessage.h"
#include "ui/UIInput.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>

struct LuaScriptProgram::Data {
	std::string bytes, name;
};

namespace {
struct Memory {
	std::size_t used = 0;
};

/// <summary>
/// Luaの確保量を追跡し、VM単位の上限を越える確保を拒否する
/// </summary>
/// <param name="data">VMより長く保持する確保量</param>
/// <param name="pointer">現在の確保領域。新規はnullptr</param>
/// <param name="oldSize">既存領域の大きさ。新規の場合は使用しない</param>
/// <param name="newSize">新しい大きさ。0は解放</param>
/// <returns>確保領域。失敗はnullptrで、既存領域を保持する</returns>
void *Allocate(void *data, void *pointer, std::size_t oldSize,
               std::size_t newSize) {
	auto &memory = *static_cast<Memory *>(data);
	const auto old = pointer ? oldSize : 0;
	if (!newSize) {
		std::free(pointer);
		memory.used -= old;
		return nullptr;
	}
	if (old > memory.used ||
	    newSize > LuaScript::kMaximumVmBytes - (memory.used - old))
		return nullptr;
	void *next = std::realloc(pointer, newSize);
	if (next)
		memory.used = memory.used - old + newSize;
	return next;
}

/// <summary>
/// Lua命令の実行数を制限する。保護呼び出しまで伝わるエラーとして停止する
/// </summary>
/// <param name="state">実行中のLua状態</param>
/// <param name="debug">使用しないフック情報</param>
void Instructions(lua_State *state, lua_Debug *debug) {
	(void)debug;
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	call.instructions += LuaScript::kHookInterval;
	if (call.remainingInstructions) {
		*call.remainingInstructions -= LuaScript::kHookInterval;
		if (*call.remainingInstructions < 0)
			luaL_error(state, "Script frame instruction limit exceeded.");
	}
	if (call.instructions > LuaScript::kMaximumInstructions)
		luaL_error(state, "Script callback instruction limit exceeded.");
}

/// <summary>
/// コールバックの失敗をファイル名とLuaの行番号で識別する
/// </summary>
/// <param name="state">失敗理由を持つLua状態</param>
/// <returns>トレース文字列1個</returns>
int Traceback(lua_State *state) {
	const char *message = lua_tostring(state, 1);
	luaL_traceback(state, state, message ? message : "Script callback failed.",
	               1);
	return 1;
}

/// <summary>
/// Luaのバイナリ生成結果を、サイズを制限したホスト側のバッファへ追加する
/// </summary>
/// <param name="state">使用しないLua状態</param>
/// <param name="data">生成済みのバイト列</param>
/// <param name="size">バイト数</param>
/// <param name="context">呼び出し情報</param>
/// <returns>0:追加成功 1:サイズ超過または確保失敗</returns>
int WriteProgram(lua_State *state, const void *data, std::size_t size,
                 void *context) {
	(void)state;
	auto &call = *static_cast<LuaInternal::Call *>(context);
	try {
		if (size > LuaScript::kMaximumBytecodeBytes - call.bytecode.size())
			return 1;
		call.bytecode.append(static_cast<const char *>(data), size);
		return 0;
	} catch (...) {
		return 1;
	}
}

/// <summary>
/// 構文確認とコンパイルをLuaの保護呼び出し内で行う
/// </summary>
/// <param name="state">Lua状態</param>
/// <returns>0。失敗時はLuaエラー</returns>
int Compile(lua_State *state) {
	auto &call = *static_cast<LuaInternal::Call *>(lua_touserdata(state, 1));
	if (luaL_loadbufferx(state, call.source.data(), call.source.size(),
	                     call.name.c_str(), "t") != LUA_OK)
		return lua_error(state);
	if (lua_dump(state, WriteProgram, &call, 0))
		return luaL_error(
		    state,
		    "Compiled Script exceeds its size limit or could not be stored.");
	return 0;
}

/// <summary>
/// コンパイル済みモジュール・環境・stateを準備し、指定したコールバックを実行する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
/// <returns>0。失敗時はLuaエラー</returns>
int Invoke(lua_State *state) {
	auto &call = *static_cast<LuaInternal::Call *>(lua_touserdata(state, 1));
	const int environment = LuaInternal::OpenEnvironment(state, call);
	LuaInternal::PrepareState(state);
	int sharedIndex = 0;
	if (call.sharedInitial) {
		LuaInternal::PushState(state, *call.sharedInitial);
		sharedIndex = lua_gettop(state);
		lua_pushvalue(state, sharedIndex);
		lua_setfield(state, environment, "game");
	}
	if (luaL_loadbufferx(state, call.source.data(), call.source.size(),
	                     call.name.c_str(), "b") != LUA_OK)
		return lua_error(state);
	lua_pushvalue(state, environment);
	if (!lua_setupvalue(state, -2, 1))
		return luaL_error(state, "Script environment could not be attached.");
	lua_call(state, 0, 1);
	const int module = lua_gettop(state);
	if (!lua_istable(state, module))
		return luaL_error(state, "A Script must return a table of callbacks.");
	if (sharedIndex) {
		lua_getfield(state, environment, "game");
		if (!lua_rawequal(state, -1, sharedIndex))
			return luaL_error(
			    state,
			    "Modify game fields instead of replacing the shared table.");
		lua_pop(state, 1);
		if (!lua_checkstack(state, LuaScript::kMaximumStateDepth * 4 + 32))
			return luaL_error(state, "Shared data stack limit exceeded.");
		if (!LuaInternal::ReadState(state, sharedIndex, call.sharedResult,
		                            call.error)) {
			lua_pushlstring(state, call.error.data(), call.error.size());
			return lua_error(state);
		}
		if (call.sharedResult != call.sharedInput)
			return luaL_error(state, "Change shared game data inside "
			                         "callbacks, not at module scope.");
	}
	LuaInternal::PushState(state, *call.initial);
	const int initial = lua_gettop(state);
	lua_getfield(state, module, call.callback);
	if (lua_isnil(state, -1))
		return 0;
	if (!lua_isfunction(state, -1))
		return luaL_error(state,
		                  "The Script callback must be a function or absent.");
	call.executing = true;
	LuaInternal::PushObject(state, call.self);
	lua_pushvalue(state, initial);
	int arguments = 2;
	if (std::strcmp(call.callback, "update") == 0) {
		lua_pushnumber(state, call.seconds);
		lua_createtable(state, 0, 7);
		lua_pushnumber(state, call.input->moveX);
		lua_setfield(state, -2, "moveX");
		lua_pushnumber(state, call.input->moveZ);
		lua_setfield(state, -2, "moveZ");
		lua_pushboolean(state, call.input->jumpPressed);
		lua_setfield(state, -2, "jumpPressed");
		lua_pushboolean(state, call.input->interactPressed);
		lua_setfield(state, -2, "interactPressed");

		if (const auto *viewport = call.viewport) {
			lua_createtable(state, 0, 2);
			lua_pushinteger(state, viewport->width);
			lua_setfield(state, -2, "width");
			lua_pushinteger(state, viewport->height);
			lua_setfield(state, -2, "height");
			lua_setfield(state, -2, "viewport");
			if (viewport->enabled && viewport->pointerValid) {
				lua_createtable(state, 0, 4);
				lua_pushnumber(state, viewport->pointerX);
				lua_setfield(state, -2, "x");
				lua_pushnumber(state, viewport->pointerY);
				lua_setfield(state, -2, "y");
				lua_pushboolean(state, viewport->pointerDown);
				lua_setfield(state, -2, "down");
				lua_pushboolean(state, viewport->pointerPressed);
				lua_setfield(state, -2, "pressed");
				lua_setfield(state, -2, "pointer");
			}
		}

		lua_createtable(state, 0, static_cast<int>(call.actions.size()));
		for (std::size_t i = 0; i < call.actions.size(); ++i) {
			lua_pushlstring(state, call.actions[i].name.data(),
			                call.actions[i].name.size());
			lua_createtable(state, 0, 3);
			lua_pushboolean(state, call.actions[i].down);
			lua_setfield(state, -2, "down");
			lua_pushboolean(state, call.actions[i].pressed);
			lua_setfield(state, -2, "pressed");
			lua_pushboolean(state, call.actions[i].released);
			lua_setfield(state, -2, "released");
			lua_rawset(state, -3);
		}
		lua_setfield(state, -2, "actions");
		arguments = 4;
	} else if (std::strcmp(call.callback, "message") == 0) {
		lua_pushlstring(state, call.message->name.data(),
		                call.message->name.size());
		LuaInternal::PushState(state, *call.messageInitial);
		LuaInternal::PushObject(state, call.message->sender);
		arguments = 5;
	} else if (std::strcmp(call.callback, "start") != 0 &&
	           std::strcmp(call.callback, "click") != 0) {
		LuaInternal::PushObject(state, call.other);
		arguments = 3;
	}
	lua_call(state, arguments, 0);
	call.executing = false;
	if (!lua_checkstack(state, LuaScript::kMaximumStateDepth * 4 + 32))
		return luaL_error(state, "Script state stack limit exceeded.");
	if (!LuaInternal::ReadState(state, initial, call)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}
	if (sharedIndex) {
		lua_getfield(state, environment, "game");
		if (!lua_rawequal(state, -1, sharedIndex))
			return luaL_error(
			    state,
			    "Modify game fields instead of replacing the shared table.");
		lua_pop(state, 1);
		if (!LuaInternal::ReadState(state, sharedIndex, call.sharedResult,
		                            call.error)) {
			lua_pushlstring(state, call.error.data(), call.error.size());
			return lua_error(state);
		}
	}
	return 0;
}

/// <summary>
/// すべての確保を保護呼び出し内へ閉じ込め、VMの失敗をC++側の文字列へ返す
/// </summary>
/// <param name="call">実行情報</param>
/// <param name="function">実行する内部C関数</param>
/// <param name="error">失敗理由</param>
/// <returns>true:呼び出し成功 false:実行・確保・制限の失敗</returns>
bool Run(LuaInternal::Call &call, lua_CFunction function, std::string &error) {
	Memory memory;
	std::unique_ptr<lua_State, decltype(&lua_close)> state(
	    lua_newstate(Allocate, &memory, 0), lua_close);
	if (!state) {
		error = "Script VM could not be allocated.";
		return false;
	}
	call.error.reserve(1024);
	*static_cast<LuaInternal::Call **>(lua_getextraspace(state.get())) = &call;
	lua_sethook(state.get(), Instructions, LUA_MASKCOUNT,
	            LuaScript::kHookInterval);
	lua_pushcfunction(state.get(), Traceback);
	const int handler = lua_gettop(state.get());
	lua_pushcfunction(state.get(), function);
	lua_pushlightuserdata(state.get(), &call);
	if (lua_pcall(state.get(), 1, 0, handler) != LUA_OK) {
		std::size_t size = 0;
		const char *text = lua_tolstring(state.get(), -1, &size);
		error = text ? std::string(text, size) : "Script execution failed.";
		return false;
	}
	error.clear();
	return true;
}

/// <summary>
/// 呼び出し可能なコールバック名を、終端を持つ定数文字列へ変換する
/// </summary>
/// <param name="name">確認する名前</param>
/// <returns>対応する名前。未対応はnullptr</returns>
const char *Callback(std::string_view name) {
	for (const char *value :
	     {"start", "update", "trigger_enter", "trigger_stay", "trigger_exit",
	      "click", "collision_enter", "collision_stay", "collision_exit",
	      "message"})
		if (name == value)
			return value;
	return nullptr;
}
} // namespace

bool LuaScript::Read(const std::filesystem::path &path, std::string &source,
                     std::string &error) {
	try {
		if (path.empty() || !PathUtils::HasExtension(path, ".lua") ||
		    path.native().find(L'\0') != std::wstring::npos)
			throw std::runtime_error("Choose a Lua Script asset.");
		std::ifstream input(PathUtils::ResolveFromExecutable(path),
		                    std::ios::binary | std::ios::ate);
		if (!input)
			throw std::runtime_error("Could not open Lua Script: " +
			                         PathUtils::ToUtf8(path));
		const auto size = input.tellg();
		if (size < 0 || size > std::streamoff(kMaximumSourceBytes))
			throw std::runtime_error("Lua Script exceeds 256 KiB.");
		std::string candidate(static_cast<std::size_t>(size), '\0');
		input.seekg(0);
		if (size &&
		    !input.read(candidate.data(), static_cast<std::streamsize>(size)))
			throw std::runtime_error("Could not read the Lua Script.");
		if (candidate.starts_with("\xef\xbb\xbf"))
			candidate.erase(0, 3);
		if (!candidate.empty() &&
		    StringConversion::Utf8ToWide(candidate).empty())
			throw std::runtime_error("Lua Scripts must use UTF-8 text.");
		source.swap(candidate);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool LuaScript::Validate(std::string_view source, std::string &error) {
	LuaScriptProgram program;
	return Compile(source, "Script", program, error);
}

bool LuaScript::Compile(std::string_view source, std::string_view name,
                        LuaScriptProgram &program, std::string &error) {
	try {
		if (source.size() > kMaximumSourceBytes)
			throw std::runtime_error("Lua Script exceeds 256 KiB.");
		if (source.starts_with("\xef\xbb\xbf"))
			source.remove_prefix(3);
		LuaInternal::Call call;
		call.source = source;
		call.name = "@" + std::string(name);
		if (!Run(call, ::Compile, error))
			return false;
		auto data = std::make_shared<LuaScriptProgram::Data>();
		data->bytes = std::move(call.bytecode);
		data->name = std::move(call.name);
		LuaScriptProgram candidate;
		candidate.data_ = std::move(data);
		program = std::move(candidate);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool LuaScript::NormalizeState(std::string_view source, std::string &normalized,
                               std::string &error) {
	try {
		if (source.size() > kMaximumStateBytes)
			throw std::runtime_error("Script initial state exceeds 64 KiB.");
		auto value = nlohmann::json::parse(source);
		if (!LuaInternal::ValidState(value))
			throw std::runtime_error(
			    "Script state needs an object containing finite numbers, "
			    "booleans, strings or nested tables; null is not supported.");
		auto text = value.dump();
		if (text.size() > kMaximumStateBytes)
			throw std::runtime_error("Normalized Script state exceeds 64 KiB.");
		normalized.swap(text);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool LuaScript::Invoke(
    const LuaScriptProgram &program, std::string_view callback, Scene &scene,
    SceneObjectId self, std::string &state, float seconds,
    const CharacterInput &input, SceneObjectId other,
    std::vector<std::string> &logs, std::string &error,
    int *remainingInstructions, ScenePhysics *physics,
    int *remainingPhysicsOperations, const ScenePrefabs *prefabs,
    int *remainingInstantiations, std::size_t *remainingSpawnedObjects,
    std::vector<SceneObjectId> *pendingDestructions, int *remainingDestructions,
    std::vector<SceneAudioAction> *audioActions, int *remainingAudioOperations,
    std::filesystem::path *sceneRequest, int *remainingSceneRequests,
    std::string *sharedState, ScriptMailbox *mailbox,
    const ScriptMessage *message, std::span<const GameActionState> actions,
    SceneAnimations *animations, int *remainingAnimationOperations,
    SceneParticles *particles, int *remainingParticleOperations,
    SceneAnimators *animators, int *remainingAnimatorOperations,
    const UIInput *viewport) {
	try {
		const char *name = Callback(callback);
		if (!program || !name || !scene.Contains(self) ||
		    !std::isfinite(seconds) || seconds < 0 ||
		    !std::isfinite(input.moveX) || !std::isfinite(input.moveZ) ||
		    !GameActionState::IsValid(actions) ||
		    (viewport && !viewport->IsValid()) ||
		    (other && !scene.Contains(other) && callback != "trigger_exit" &&
		     callback != "collision_exit" && callback != "message") ||
		    (remainingInstructions && *remainingInstructions < 0) ||
		    (remainingPhysicsOperations && *remainingPhysicsOperations < 0) ||
		    (remainingInstantiations && *remainingInstantiations < 0) ||
		    (remainingDestructions && *remainingDestructions < 0) ||
		    (remainingAudioOperations && *remainingAudioOperations < 0) ||
		    (remainingSceneRequests && *remainingSceneRequests < 0) ||
		    (remainingAnimationOperations &&
		     *remainingAnimationOperations < 0) ||
		    (remainingParticleOperations && *remainingParticleOperations < 0) ||
		    (remainingAnimatorOperations && *remainingAnimatorOperations < 0))
			throw std::runtime_error("Script callback arguments are invalid.");
		std::string messageNormalized;
		if (callback == "message") {
			if (!message || message->receiver != self ||
			    message->name.empty() ||
			    message->name.size() > ScriptMessage::kMaximumNameBytes ||
			    message->name.find('\0') != std::string::npos ||
			    StringConversion::Utf8ToWide(message->name).empty() ||
			    message->sender != other ||
			    message->data.size() > ScriptMessage::kMaximumDataBytes ||
			    !NormalizeState(message->data, messageNormalized, error) ||
			    messageNormalized.size() > ScriptMessage::kMaximumDataBytes)
				throw std::runtime_error(
				    "Script message arguments are invalid.");
		} else if (message)
			throw std::runtime_error(
			    "Message data requires the message callback.");
		const auto messageJson = nlohmann::json::parse(
		    message ? messageNormalized : std::string("{}"));
		const auto messageNodes = LuaInternal::BuildState(messageJson);
		std::string normalized;
		if (!NormalizeState(state, normalized, error))
			return false;
		if (sharedState == &state)
			throw std::runtime_error(
			    "Object state and shared game data must use separate storage.");
		std::string sharedNormalized;
		if (sharedState &&
		    !NormalizeState(*sharedState, sharedNormalized, error))
			return false;
		const auto sharedJson = nlohmann::json::parse(
		    sharedState ? sharedNormalized : std::string("{}"));
		const auto sharedNodes = LuaInternal::BuildState(sharedJson);
		const auto initial = nlohmann::json::parse(normalized);
		const auto nodes = LuaInternal::BuildState(initial);
		LuaInternal::Call call;
		call.scene = &scene;
		call.self = self;
		call.other = other;
		call.input = &input;
		call.actions = actions;
		call.viewport = viewport;
		call.animations = animations;
		call.particles = particles;
		call.animators = animators;
		call.remainingAnimatorOperations = remainingAnimatorOperations;
		call.remainingParticleOperations = remainingParticleOperations;
		call.remainingAnimationOperations = remainingAnimationOperations;
		call.initial = &nodes;
		call.mailbox = mailbox;
		call.message = message;
		call.messageInitial = message ? &messageNodes : nullptr;
		call.sharedInitial = sharedState ? &sharedNodes : nullptr;
		call.sharedInput = std::move(sharedNormalized);
		call.source = program.data_->bytes;
		call.name = program.data_->name;
		call.callback = name;
		call.seconds = seconds;
		call.remainingInstructions = remainingInstructions;
		call.physics = physics;
		call.remainingPhysicsOperations = remainingPhysicsOperations;
		call.sceneRequest = sceneRequest;
		call.remainingSceneRequests = remainingSceneRequests;
		call.audioActions = audioActions;
		call.remainingAudioOperations = remainingAudioOperations;
		call.pendingDestructions = pendingDestructions;
		call.remainingDestructions = remainingDestructions;
		call.prefabs = prefabs;
		call.remainingInstantiations = remainingInstantiations;
		call.remainingSpawnedObjects = remainingSpawnedObjects;
		if (!Run(call, ::Invoke, error))
			return false;
		auto combined = logs;
		combined.insert(combined.end(), call.logs.begin(), call.logs.end());
		if (!call.result.empty())
			state.swap(call.result);
		if (sharedState && !call.sharedResult.empty())
			sharedState->swap(call.sharedResult);
		logs.swap(combined);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
