#include "gameplay/SceneScripts.h"
#include "core/Logger.h"
#include "gameplay/CharacterInput.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneDestruction.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <set>
#include <stdexcept>
#include <utility>

bool SceneScripts::Configure(const Scene &scene, bool preserve,
                             std::string &error, SceneObjectId *invalidObject) {
	SceneObjectId current = 0;
	try {
		auto instances =
		    preserve ? instances_ : std::map<SceneObjectId, Instance>{};
		auto programs =
		    preserve ? programs_
		             : std::map<std::filesystem::path, LuaScriptProgram>{};
		auto prefabs = preserve ? prefabs_ : ScenePrefabs{};
		if (!prefabs.Configure(scene, preserve, error))
			throw std::runtime_error(error);
		for (const auto &source : prefabs.GetScenes())
			for (const auto &[id, o] : source->GetObjects()) {
				(void)id;
				if (!o.script || o.script->asset.empty())
					continue;
				auto path = PathUtils::ResolveFromExecutable(o.script->asset);
				path.make_preferred();
				if (programs.contains(path))
					continue;
				std::string text;
				LuaScriptProgram compiled;
				if (!LuaScript::Read(path, text, error) ||
				    !LuaScript::Compile(text, PathUtils::ToUtf8(path), compiled,
				                        error))
					throw std::runtime_error(error);
				programs.emplace(path, std::move(compiled));
			}
		for (auto i = instances.begin(); i != instances.end();) {
			const auto *object = scene.FindObject(i->first);
			if (!object || !object->script || object->script->asset.empty() ||
			    object->script->asset != i->second.settings.asset ||
			    object->script->initialState != i->second.settings.initialState)
				i = instances.erase(i);
			else {
				i->second.settings = *object->script;
				++i;
			}
		}
		for (const auto &[id, object] : scene.GetObjects()) {
			current = id;
			if (!object.script || !object.script->enabled ||
			    object.script->asset.empty() ||
			    !SceneActivity::IsActive(scene, id) || instances.contains(id))
				continue;
			if (!object.script->IsValid())
				throw std::runtime_error("Lua Script settings are invalid.");
			if (instances.size() >= kMaximumInstances)
				throw std::runtime_error("Lua Script instance limit exceeded.");
			auto path = PathUtils::ResolveFromExecutable(object.script->asset);
			path.make_preferred();
			auto program = programs.find(path);
			if (program == programs.end()) {
				std::string source;
				if (!LuaScript::Read(path, source, error))
					throw std::runtime_error(error);
				LuaScriptProgram compiled;
				if (!LuaScript::Compile(source, PathUtils::ToUtf8(path),
				                        compiled, error))
					throw std::runtime_error(error);
				program = programs.emplace(path, std::move(compiled)).first;
			}
			Instance instance;
			instance.settings = *object.script;
			instance.program = program->second;
			if (!LuaScript::NormalizeState(object.script->initialState,
			                               instance.state, error))
				throw std::runtime_error(error);
			instances.emplace(id, std::move(instance));
		}
		prefabs_ = std::move(prefabs);
		instances_.swap(instances);
		programs_.swap(programs);
		if (!preserve) {
			mailbox_.Clear();
			pending_.clear();
			logs_.clear();
		}
		mailbox_.RemoveMissingReceivers(scene);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error =
		    std::format("Script object ID {}: {}", current, exception.what());
		if (invalidObject)
			*invalidObject = current;
		return false;
	}
}

bool SceneScripts::Run(
    Scene &scene, float seconds, const CharacterInput &input,
    std::span<const TriggerEvent> events, bool startOnly, std::string &error,
    SceneObjectId *invalidObject, std::span<const SceneObjectId> uiClicks,
    ScenePhysics *physics, std::span<const CollisionEvent> collisions,
    std::vector<SceneAudioAction> *audioActions,
    std::filesystem::path *sceneRequest, std::string *sharedState,
    std::span<const GameActionState> actions, SceneAnimations *animations,
    SceneParticles *particles, SceneAnimators *animators) {
	SceneObjectId current = 0;
	try {
		if (!std::isfinite(seconds) || seconds < 0)
			throw std::runtime_error("Script time is invalid.");
		logs_.clear();
		auto incoming = mailbox_.BeginStep(!startOnly && seconds > 0);
		std::string game = sharedState ? *sharedState : std::string("{}");
		std::size_t callbacks = 0;
		int budget = kFrameInstructions;
		int physicsBudget = kFramePhysicsOperations;
		int sceneBudget = kFrameSceneRequests;
		std::filesystem::path requestedScene;
		int audioBudget = kFrameAudioOperations;
		int animationBudget = 1024;
		int particleBudget = 1024;
		int animatorBudget = 1024;
		std::vector<SceneAudioAction> requestedAudio;
		int destructionBudget = kFrameDestructions;
		std::vector<SceneObjectId> pendingDestructions;
		int spawnBudget = kFrameInstantiations;
		std::size_t objectBudget = kFrameSpawnedObjects;
		const auto active = [&](SceneObjectId id) {
			const auto *o = scene.FindObject(id);
			return o && o->script && o->script->enabled &&
			       !o->script->asset.empty() &&
			       SceneActivity::IsActive(scene, id);
		};
		const auto invoke = [&](SceneObjectId id, const char *callback,
		                        SceneObjectId other,
		                        const ScriptMessage *message = nullptr) {
			current = id;
			auto &instance = instances_.at(id);
			if (++callbacks > kMaximumCallbacks)
				throw std::runtime_error(
				    "Script callback count limit exceeded.");
			std::vector<std::string> messages;
			if (!LuaScript::Invoke(
			        instance.program, callback, scene, id, instance.state,
			        (std::min)(seconds, kMaximumFrameSeconds), input, other,
			        messages, error, &budget, physics, &physicsBudget,
			        &prefabs_, &spawnBudget, &objectBudget,
			        &pendingDestructions, &destructionBudget,
			        audioActions ? &requestedAudio : nullptr, &audioBudget,
			        sceneRequest ? &requestedScene : nullptr, &sceneBudget,
			        sharedState ? &game : nullptr, &mailbox_, message, actions,
			        animations, &animationBudget, particles, &particleBudget,
			        animators, &animatorBudget))
				throw std::runtime_error(std::format(
				    "{} [{}]: {}", PathUtils::ToUtf8(instance.settings.asset),
				    callback, error));
			if (messages.size() > kMaximumLogs - logs_.size())
				throw std::runtime_error("Script frame log limit exceeded.");
			for (auto &line : messages)
				logs_.emplace_back(id, std::move(line));
		};
		for (auto &[id, instance] : instances_) {
			if (!active(id))
				continue;
			if (!instance.started) {
				invoke(id, "start", 0);
				instance.started = true;
			}
			if (!startOnly && seconds > 0 && active(id))
				invoke(id, "update", 0);
		}
		if (!startOnly && seconds > 0) {
			auto queued = std::move(pending_);
			pending_.clear();
			queued.insert(queued.end(), events.begin(), events.end());
			for (const auto &event : queued) {
				if (event.type != TriggerEvent::Type::EXIT &&
				    (!scene.Contains(event.contact.first) ||
				     !scene.Contains(event.contact.second)))
					continue;
				const char *callback =
				    event.type == TriggerEvent::Type::ENTER  ? "trigger_enter"
				    : event.type == TriggerEvent::Type::EXIT ? "trigger_exit"
				                                             : "trigger_stay";
				for (const auto id :
				     std::array{event.contact.first, event.contact.second}) {
					if (instances_.contains(id) && active(id))
						invoke(id, callback,
						       id == event.contact.first ? event.contact.second
						                                 : event.contact.first);
				}
			}
		}
		if (!startOnly && seconds > 0) {
			for (const auto &event : collisions) {
				if (event.type != CollisionEvent::Type::EXIT &&
				    (!scene.Contains(event.contact.first) ||
				     !scene.Contains(event.contact.second)))
					continue;
				const char *callback =
				    event.type == CollisionEvent::Type::ENTER
				        ? "collision_enter"
				    : event.type == CollisionEvent::Type::EXIT
				        ? "collision_exit"
				        : "collision_stay";
				for (auto id :
				     std::array{event.contact.first, event.contact.second})
					if (instances_.contains(id) && active(id))
						invoke(id, callback,
						       id == event.contact.first ? event.contact.second
						                                 : event.contact.first);
			}
		}
		if (!startOnly && seconds > 0) {
			std::set<SceneObjectId> delivered;
			for (auto id : uiClicks) {
				const auto *object = scene.FindObject(id);
				if (delivered.insert(id).second && instances_.contains(id) &&
				    active(id) && object->button && object->button->enabled &&
				    object->button->interactable)
					invoke(id, "click", 0);
			}
		}
		for (const auto &message : incoming) {
			if (instances_.contains(message.receiver) &&
			    active(message.receiver) &&
			    instances_.at(message.receiver).started)
				invoke(message.receiver, "message", message.sender, &message);
		}
		if (!SceneDestruction::Apply(scene, pendingDestructions, error))
			throw std::runtime_error(error);
		mailbox_.RemoveMissingReceivers(scene);
		if (audioActions) {
			auto combined = *audioActions;
			combined.insert(combined.end(), requestedAudio.begin(),
			                requestedAudio.end());
			std::erase_if(combined, [&](const auto &action) {
				const auto *o = scene.FindObject(action.target);
				return !o || !o->audioSource;
			});
			audioActions->swap(combined);
		}
		if (sceneRequest && !requestedScene.empty())
			*sceneRequest = std::move(requestedScene);
		if (sharedState)
			sharedState->swap(game);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error =
		    std::format("Script object ID {}: {}", current, exception.what());
		if (invalidObject)
			*invalidObject = current;
		return false;
	}
}

void SceneScripts::QueueTriggers(std::span<const TriggerEvent> events) {
	if (instances_.empty())
		return;
	for (const auto &event : events)
		if (event.type != TriggerEvent::Type::STAY)
			pending_.push_back(event);
}

std::optional<std::string_view> SceneScripts::GetState(SceneObjectId id) const {
	const auto found = instances_.find(id);
	return found == instances_.end()
	           ? std::nullopt
	           : std::optional<std::string_view>{found->second.state};
}

void SceneScripts::FlushLogs() {
	for (const auto &[id, message] : logs_)
		Logger::Info(std::format("Script (ID {}): {}", id, message));
	logs_.clear();
}
