#include "gameplay/ScriptInspector.h"
#include "assets/AssetDragDrop.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <format>
#include <imgui.h>
#include <imgui_stdlib.h>

void ScriptInspector::Clear() {
	draftObject_ = 0;
	initial_.clear();
	draft_.clear();
	referenceName_ = "target";
	prefabName_ = "prefab";
	sceneName_ = "scene";
	error_.clear();
	edit_.clear();
}

bool ScriptInspector::Draw(const ImGuiSystem &ui, const Scene &scene,
                           SceneObjectId id, SceneObject &object,
                           EditorHistory &history,
                           std::filesystem::path &sceneOpenRequest,
                           std::span<const std::filesystem::path> scripts,
                           std::span<const std::filesystem::path> prefabs,
                           std::span<const std::filesystem::path> scenes) {
	edit_.clear();
	if (!object.script)
		return false;
	if (draftObject_ != id || initial_ != object.script->initialState) {
		draftObject_ = id;
		initial_ = object.script->initialState;
		draft_ = initial_;
		error_.clear();
	}

	if (!ui.CollapsingHeader("Lua Script"))
		return false;
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("ScriptInspector");
	auto value = *object.script;
	bool changed = false;
	if (ui.Checkbox("Script Enabled", value.enabled))
		changed |= history.SetScript(id, value);
	if (ui.Checkbox("Edit in Game", value.editInGame))
		changed |= history.SetScript(id, value);
	ImGui::SetItemTooltip("Add a Button to this object to offer a Lua editor in the game.");
	const auto preview = value.asset.empty()
	                         ? std::string("None")
	                         : PathUtils::ToUtf8(value.asset.filename());
	if (ImGui::BeginCombo("Script Asset", preview.c_str())) {
		if (ui.Selectable("None", value.asset.empty())) {
			value.asset.clear();
			changed |= history.SetScript(id, value);
		}

		for (const auto &path : scripts) {
			const auto label = PathUtils::ToUtf8(path);

			if (ui.Selectable(label.c_str(), path == value.asset)) {
				value.asset = path;
				changed |= history.SetScript(id, value);
			}
		}

		ImGui::EndCombo();
	}

	if (auto path = AssetDragDrop::AcceptTarget(AssetDragDrop::Kind::SCRIPT);
	    !path.empty()) {
		value.asset = std::move(path);
		changed |= history.SetScript(id, value);
	}
	ImGui::BeginDisabled(value.asset.empty());
	if (ui.Button("Edit Script"))
		edit_ = value.asset;
	ImGui::EndDisabled();
	ImGui::InputTextMultiline("Initial State", &draft_, {0, 100});
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("JSON object used at Play / Restart. Apply to record "
		                  "one undo step.");
	if (ui.Button("Apply Initial State")) {
		std::string normalized;

		if (LuaScript::NormalizeState(draft_, normalized, error_)) {
			value.initialState = std::move(normalized);
			changed |= history.SetScript(id, value);
			initial_ = value.initialState;
			draft_ = initial_;
		}
	}

	ui.SameLine();

	if (ui.Button("Reload Initial State")) {
		initial_ = object.script->initialState;
		draft_ = initial_;
		error_.clear();
	}

	std::size_t remove = value.references.size();

	for (std::size_t i = 0; i < value.references.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		auto &[name, target] = value.references[i];
		const auto *selected = scene.FindObject(target);
		const auto label =
		    selected ? std::format("{} (ID {})", selected->name, target)
		    : target ? std::format("Missing (ID {})", target)
		             : "None";
		if (ImGui::BeginCombo(name.c_str(), label.c_str())) {
			if (ui.Selectable("None", target == 0)) {
				target = 0;
				changed |= history.SetScript(id, value);
			}

			for (const auto &[other, entry] : scene.GetObjects()) {
				const auto item = std::format("{} (ID {})", entry.name, other);

				if (ui.Selectable(item.c_str(), target == other)) {
					target = other;
					changed |= history.SetScript(id, value);
				}
			}

			ImGui::EndCombo();
		}

		if (ui.Button("Remove Reference"))
			remove = i;
		ImGui::PopID();
	}

	if (remove < value.references.size()) {
		value.references.erase(value.references.begin() +
		                       static_cast<std::ptrdiff_t>(remove));
		changed |= history.SetScript(id, value);
	}

	ui.InputText("Reference Name", referenceName_);
	ImGui::BeginDisabled(value.references.size() >=
	                     ScriptComponent::kMaximumReferences);
	if (ui.Button("Add Reference")) {
		auto next = value;
		next.references.emplace_back(referenceName_, 0);
		if (!next.IsValid())
			error_ =
			    "Use a unique, nonempty reference name of at most 64 bytes.";
		else {
			changed |= history.SetScript(id, next);
			error_.clear();
		}
	}

	ImGui::EndDisabled();

	std::size_t removePrefab = value.prefabs.size();
	ImGui::PushID("PrefabSlots");

	for (std::size_t i = 0; i < value.prefabs.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		auto &[name, path] = value.prefabs[i];
		const auto prefabPreview = path.empty()
		                               ? std::string("None")
		                               : PathUtils::ToUtf8(path.filename());
		const auto label = "Prefab: " + name;

		if (ImGui::BeginCombo(label.c_str(), prefabPreview.c_str())) {
			if (ui.Selectable("None", path.empty())) {
				path.clear();
				changed |= history.SetScript(id, value);
			}

			for (const auto &candidate : prefabs) {
				const auto title = PathUtils::ToUtf8(candidate);

				if (ui.Selectable(title.c_str(), path == candidate)) {
					path = candidate;
					changed |= history.SetScript(id, value);
				}
			}

			ImGui::EndCombo();
		}

		if (auto droppedPrefab =
		        AssetDragDrop::AcceptTarget(AssetDragDrop::Kind::PREFAB);
		    !droppedPrefab.empty()) {
			value.prefabs[i].second = std::move(droppedPrefab);
			changed |= history.SetScript(id, value);
		}
		if (ui.Button("Remove Prefab"))
			removePrefab = i;
		ImGui::PopID();
	}

	if (removePrefab < value.prefabs.size()) {
		value.prefabs.erase(value.prefabs.begin() +
		                    static_cast<std::ptrdiff_t>(removePrefab));
		changed |= history.SetScript(id, value);
	}

	ui.InputText("Prefab Name", prefabName_);
	ImGui::BeginDisabled(value.prefabs.size() >=
	                     ScriptComponent::kMaximumPrefabs);
	if (ui.Button("Add Prefab")) {
		auto next = value;
		next.prefabs.emplace_back(prefabName_, std::filesystem::path{});
		if (!next.IsValid())
			error_ = "Use a unique, nonempty Prefab name of at most 64 bytes.";
		else {
			changed |= history.SetScript(id, next);
			error_.clear();
		}
	}

	ImGui::EndDisabled();
	ImGui::PopID();
	std::size_t removeScene = value.scenes.size();
	ImGui::PushID("SceneSlots");

	for (std::size_t i = 0; i < value.scenes.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		auto &[name, path] = value.scenes[i];
		const auto scenePreview = path.empty()
		                              ? std::string("None")
		                              : PathUtils::ToUtf8(path.filename());
		const auto label = "Scene: " + name;

		if (ImGui::BeginCombo(label.c_str(), scenePreview.c_str())) {
			if (ui.Selectable("None", path.empty())) {
				path.clear();
				changed |= history.SetScript(id, value);
			}

			for (const auto &candidate : scenes) {
				const auto title = PathUtils::ToUtf8(candidate);

				if (ui.Selectable(title.c_str(), path == candidate)) {
					path = candidate;
					changed |= history.SetScript(id, value);
				}
			}

			ImGui::EndCombo();
		}

		if (auto droppedScene =
		        AssetDragDrop::AcceptTarget(AssetDragDrop::Kind::SCENE);
		    !droppedScene.empty()) {
			value.scenes[i].second = std::move(droppedScene);
			changed |= history.SetScript(id, value);
		}
		ui.BeginDisabled(path.empty());
		if (ui.Button("Open Scene")) {
			sceneOpenRequest = path;
		}
		ui.EndDisabled();
		ui.SameLine();
		if (ui.Button("Remove Scene"))
			removeScene = i;
		ImGui::PopID();
	}

	if (removeScene < value.scenes.size()) {
		value.scenes.erase(value.scenes.begin() +
		                   static_cast<std::ptrdiff_t>(removeScene));
		changed |= history.SetScript(id, value);
	}

	ui.InputText("Scene Name", sceneName_);
	ImGui::BeginDisabled(value.scenes.size() >=
	                     ScriptComponent::kMaximumScenes);
	if (ui.Button("Add Scene")) {
		auto next = value;
		next.scenes.emplace_back(sceneName_, std::filesystem::path{});
		if (!next.IsValid())
			error_ = "Use a unique, nonempty Scene name of at most 64 bytes.";
		else {
			changed |= history.SetScript(id, next);
			error_.clear();
		}
	}

	ImGui::EndDisabled();
	ImGui::PopID();
	if (!error_.empty())
		ImGui::TextWrapped("%s", error_.c_str());
	if (ui.Button("Remove Lua Script")) {
		changed |= history.SetScript(id, std::nullopt);
		Clear();
	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}

void ScriptInspector::DrawReadOnly(
    const ImGuiSystem &ui, const SceneObject &object,
    std::optional<std::string_view> state) const {
	if (!object.script || !ui.CollapsingHeader("Lua Script"))
		return;
	ui.Text(object.script->enabled ? "Enabled" : "Disabled");
	ui.Text(PathUtils::ToUtf8(object.script->asset));
	for (const auto &[name, target] : object.script->references)
		ui.Text(std::format("{}: ID {}", name, target));
	for (const auto &[name, path] : object.script->prefabs)
		ui.Text(std::format("Prefab {}: {}", name, PathUtils::ToUtf8(path)));
	for (const auto &[name, path] : object.script->scenes)
		ui.Text(std::format("Scene {}: {}", name, PathUtils::ToUtf8(path)));
	ui.Text("State");
	if (state)
		ImGui::TextWrapped("%.*s", static_cast<int>(state->size()),
		                   state->data());
	else
		ui.Text("Not initialized");
}
