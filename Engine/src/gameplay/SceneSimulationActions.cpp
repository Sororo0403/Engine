#include "gameplay/SceneSimulation.h"

#include "scene/SceneActivity.h"
#include <array>
#include <format>
#include <utility>

void SceneSimulation::CopyFrom(const SceneSimulation &source) {
	scene_.ReplaceObjects(source.scene_.GetObjects());
	world_ = source.world_;
	triggers_ = source.triggers_;
	controller_ = source.controller_;
	character_ = source.character_;
	camera_ = source.camera_;
	updateOrder_ = source.updateOrder_;
	followPoses_ = source.followPoses_;
	spawn_ = source.spawn_;
	respawnCount_ = source.respawnCount_;
	lastRespawnPosition_ = source.lastRespawnPosition_;
	pose_ = source.pose_;
	fallbackPose_ = source.fallbackPose_;
	cameraSettings_ = source.cameraSettings_;
	playing_ = source.playing_;
	frameEvents_ = source.frameEvents_;
	pendingActionSignals_ = source.pendingActionSignals_;
	firedActions_ = source.firedActions_;
	lastUpdateError_ = source.lastUpdateError_;
}

void SceneSimulation::Adopt(SceneSimulation &source) {
	// シーンの複製と検証が完了してから、確保を伴わない移動で残りの状態を確定する。
	scene_.ReplaceObjects(source.scene_.GetObjects());
	world_ = std::move(source.world_);
	triggers_ = std::move(source.triggers_);
	controller_ = source.controller_;
	character_ = source.character_;
	camera_ = source.camera_;
	updateOrder_ = std::move(source.updateOrder_);
	followPoses_ = std::move(source.followPoses_);
	spawn_ = source.spawn_;
	respawnCount_ = source.respawnCount_;
	lastRespawnPosition_ = source.lastRespawnPosition_;
	pose_ = source.pose_;
	fallbackPose_ = source.fallbackPose_;
	cameraSettings_ = source.cameraSettings_;
	playing_ = source.playing_;
	frameEvents_ = std::move(source.frameEvents_);
	pendingActionSignals_ = std::move(source.pendingActionSignals_);
	firedActions_ = std::move(source.firedActions_);
	lastUpdateError_ = std::move(source.lastUpdateError_);
}

void SceneSimulation::QueueActionSignals(
    std::span<const TriggerEvent> events,
    std::vector<ActionSignal> &signals) const {
	for (const auto &event : events) {
		if (event.type == TriggerEvent::Type::STAY) {
			continue;
		}
		for (const auto id :
		     std::array{event.contact.first, event.contact.second}) {
			const auto *object = scene_.FindObject(id);
			if (object && SceneActivity::IsActive(scene_, id) &&
			    object->collider && object->collider->isTrigger &&
			    object->triggerActions &&
			    !object->triggerActions->actions.empty()) {
				signals.push_back({id, event});
			}
		}
	}
}

bool SceneSimulation::ApplyTriggerActions(std::string &error,
                                          SceneObjectId *invalidObject) {
	error.clear();
	auto signals = std::move(pendingActionSignals_);
	pendingActionSignals_.clear();
	QueueActionSignals(frameEvents_, signals);
	auto fired = firedActions_;
	std::map<SceneObjectId, bool> requested;
	for (const auto &signal : signals) {
		const auto *object = scene_.FindObject(signal.receiver);
		if (!object || !SceneActivity::IsActive(scene_, signal.receiver) ||
		    !object->collider || !object->collider->isTrigger ||
		    !object->triggerActions) {
			continue;
		}
		const auto &actions = object->triggerActions->actions;
		for (std::size_t i = 0; i < actions.size(); ++i) {
			const auto &action = actions[i];
			const auto key = std::pair{signal.receiver, i};
			if (action.event != signal.event.type ||
			    (action.once && fired.contains(key))) {
				continue;
			}
			const auto *target = scene_.FindObject(action.target);
			if (!action.IsValid() || !target) {
				error = std::format("Trigger Actions on object ID {} have an "
				                    "invalid action or target.",
				                    signal.receiver);
				if (invalidObject) {
					*invalidObject = signal.receiver;
				}
				return false;
			}
			auto entry =
			    requested.try_emplace(action.target, target->active).first;
			switch (action.operation) {
			case TriggerAction::Operation::ENABLE:
				entry->second = true;
				break;
			case TriggerAction::Operation::DISABLE:
				entry->second = false;
				break;
			case TriggerAction::Operation::TOGGLE:
				entry->second = !entry->second;
				break;
			}
			if (action.once) {
				fired.insert(key);
			}
		}
	}
	std::vector<SceneActiveChange> changes;
	for (const auto &[id, active] : requested) {
		if (scene_.FindObject(id)->active != active) {
			changes.push_back({id, active});
		}
	}
	if (!changes.empty()) {
		auto events = frameEvents_;
		if (!SetActive(changes, error, invalidObject)) {
			error = "Trigger Actions could not apply their activity changes: " +
			        error;
			return false;
		}
		// 操作が生成した検知は観察用に残し、操作の実行だけを次の正の時間の更新へ送る。
		for (const auto &event : frameEvents_) {
			if (event.type != TriggerEvent::Type::STAY) {
				events.push_back(event);
			}
		}
		frameEvents_.swap(events);
	}
	firedActions_.swap(fired);
	return true;
}
