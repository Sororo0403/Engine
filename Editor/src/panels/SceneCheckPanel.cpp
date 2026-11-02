#include "panels/SceneCheckPanel.h"
#include "assets/SceneAssetValidation.h"
#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"
#include <exception>
#include <format>
#include <imgui.h>
#include <iterator>

void SceneCheckPanel::Show() {
	open_ = true;
	checkRequested_ = true;
	focusRequested_ = true;
}

void SceneCheckPanel::Reset() {
	*this = SceneCheckPanel{};
}

void SceneCheckPanel::Check(const Scene &scene, const CameraPose &fallback) {
	auto issues = SceneValidation::Inspect(scene, fallback);
	auto assets = SceneAssetValidation::Inspect(scene);
	issues.insert(issues.end(), std::make_move_iterator(assets.begin()),
	              std::make_move_iterator(assets.end()));
	issues_.swap(issues);
	error_.clear();
	checked_ = true;
}

SceneObjectId SceneCheckPanel::Draw(const ImGuiSystem &ui, const Scene &scene,
                                    const CameraPose &fallback) {
	if (!open_) {
		return 0;
	}

	ImGui::SetNextWindowSize({620.0f, 420.0f}, ImGuiCond_FirstUseEver);

	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	if (!ImGui::Begin("Scene Check", &open_)) {
		ImGui::End();
		return 0;
	}

	if (ui.Button("Check Scene") || checkRequested_) {
		checkRequested_ = false;

		try {
			Check(scene, fallback);
		} catch (const std::exception &error) {
			error_ = error.what();
		}
	}

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	} else if (checked_) {
		ui.Text(std::format("Last check: {} issue(s)", issues_.size()));
	}

	SceneObjectId requested = 0;

	for (std::size_t i = 0; i < issues_.size(); ++i) {
		const auto &issue = issues_[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::Separator();
		ImGui::TextWrapped("%s", issue.message.c_str());

		if (issue.objectId != 0) {
			ui.BeginDisabled(!scene.Contains(issue.objectId));

			if (ui.Button("Select Object")) {
				requested = issue.objectId;
			}

			ui.EndDisabled();
		}

		ImGui::PopID();
	}

	ImGui::End();
	return requested;
}
