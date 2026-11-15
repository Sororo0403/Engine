#include "graphics/ui/GameScriptEditor.h"
#include "gameplay/CharacterInput.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <vector>

void GameScriptEditor::OpenClickedScript(const Scene &scene,
                                         std::span<const SceneObjectId> clicks) {
	if (open_)
		return;

	for (const auto id : clicks) {
		const auto *object = scene.FindObject(id);
		if (!object || !object->script || !object->script->enabled ||
		    !object->script->editInGame || object->script->asset.empty())
			continue;

		if (object_ != id || path_ != object->script->asset || !loaded_) {
			object_ = id;
			path_ = object->script->asset;
			starter_.clear();
			result_.clear();
			loaded_ = LuaScript::Read(path_, starter_, result_);
			code_ = starter_;
		}
		opening_ = open_ = true;
		return;
	}
}

void GameScriptEditor::Draw(const Scene &scene) {
	if (!open_)
		return;

	constexpr auto popup = "コードを書く###GameScriptEditor";
	if (opening_) {
		ImGui::OpenPopup(popup);
		opening_ = false;
	}

	const auto size = ImGui::GetMainViewport()->WorkSize;
	ImGui::SetNextWindowSize({std::min(900.f, size.x * .92f),
	                          std::min(640.f, size.y * .9f)}, ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(popup, &open_, ImGuiWindowFlags_NoSavedSettings))
		return;

	ImGui::TextUnformatted(PathUtils::ToUtf8(path_.filename()).c_str());
	if (loaded_) {
		const float height = std::max(100.f, ImGui::GetContentRegionAvail().y - 170.f);
		if (ImGui::InputTextMultiline("##Code", &code_, {-1, height},
		                              ImGuiInputTextFlags_AllowTabInput))
			result_.clear();

		if (ImGui::Button("実行")) {
			Run(scene);
		}
		ImGui::SameLine();
		if (ImGui::Button("初期コードに戻す")) {
			code_ = starter_;
			result_.clear();
		}
		ImGui::SameLine();
	}
	if (ImGui::Button("閉じる")) {
		open_ = false;
		ImGui::CloseCurrentPopup();
	}
	if (!result_.empty()) {
		ImGui::SeparatorText("実行結果");
		ImGui::TextWrapped("%s", result_.c_str());
	}
	ImGui::EndPopup();
}

void GameScriptEditor::Run(const Scene &scene) {
	const auto *object = scene.FindObject(object_);
	if (!object || !object->script) {
		result_ = "このスクリプトは現在のシーンにありません。";
		return;
	}

	result_.clear();
	LuaScriptProgram program;
	if (!LuaScript::Compile(code_, PathUtils::ToUtf8(path_.filename()),
	                        program, result_))
		return;

	Scene scratch;
	scratch.CopyFrom(scene);
	std::vector<std::string> logs;
	std::string state = object->script->initialState;
	if (!LuaScript::Invoke(program, "start", scratch, object_, state, 0.f, {},
	                      0, logs, result_))
		return;

	for (const auto &message : logs) {
		if (!result_.empty())
			result_ += '\n';
		result_ += message;
	}
	if (result_.empty())
		result_ = "実行が完了しました。";
}

void GameScriptEditor::Reset() {
	*this = {};
}

bool GameScriptEditor::IsOpen() const {
	return open_;
}
