#include "panels/SceneCheckPanel.h"
#include "assets/SceneAssetValidation.h"
#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"
#include <exception>
#include <format>
#include <imgui.h>
#include <iterator>
#include <set>

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

std::string SceneCheckPanel::GetReport() const {
	if (!error_.empty()) {
		return std::format("Scene Check failed:\n{}\n", error_);
	}

	if (!checked_) {
		return {};
	}

	auto report =
	    std::format("Last Scene Check: {} issue(s)\n", issues_.size());

	for (std::size_t i = 0; i < issues_.size(); ++i) {
		report += std::format("\n{}. {}\n", i + 1, issues_[i].message);
	}

	return report;
}

SceneCheckPanel::Request SceneCheckPanel::Draw(const ImGuiSystem &ui,
                                               const Scene &scene,
                                               const CameraPose &fallback) {
	if (!open_) {
		return {};
	}

	ImGui::SetNextWindowSize({620.0f, 420.0f}, ImGuiCond_FirstUseEver);

	const bool focusWindow = focusRequested_;

	if (focusWindow) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	const bool windowVisible = ImGui::Begin("Scene Check", &open_);

	if (focusWindow && open_) {
		ui.FocusWindow("Scene Check");
	}

	if (!windowVisible) {
		ImGui::End();
		return {};
	}

	if (ui.Button("Check Scene") || checkRequested_) {
		checkRequested_ = false;

		try {
			Check(scene, fallback);
		} catch (const std::exception &error) {
			error_ = error.what();
		}
	}

	ui.BeginDisabled(!checked_ && error_.empty());

	if (ui.Button("Copy Results")) {
		const auto report = GetReport();
		ImGui::SetClipboardText(report.c_str());
	}

	ui.EndDisabled();

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	} else if (checked_) {
		ui.Text(std::format("Last check: {} issue(s)", issues_.size()));
	}

	Request requested;
	const auto objects = GetIssueObjects(scene);
	ui.BeginDisabled(objects.empty());
	if (ui.Button("Select All Issue Objects")) {
		requested.objectIds = objects;
	}
	ui.SameLine();
	if (ui.Button("Frame All Issue Objects")) {
		requested = {objects, true};
	}
	ui.EndDisabled();

	for (std::size_t i = 0; i < issues_.size(); ++i) {
		const auto &issue = issues_[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::Separator();
		ImGui::TextWrapped("%s", issue.message.c_str());

		if (issue.objectId != 0) {
			ui.BeginDisabled(!scene.Contains(issue.objectId));

			if (ui.Button("Select Object")) {
				requested = {{issue.objectId}, false};
			}

			ui.SameLine();
			if (ui.Button("Frame Object")) {
				requested = {{issue.objectId}, true};
			}

			ui.SameLine();

			if (ui.Button("Inspect Object")) {
				requested = {{issue.objectId}, false, true};
			}

			ui.EndDisabled();
		}

		ImGui::PopID();
	}

	ImGui::End();
	return requested;
}

std::vector<SceneObjectId>
SceneCheckPanel::GetIssueObjects(const Scene &scene) const {
	std::vector<SceneObjectId> ids;
	std::set<SceneObjectId> seen;
	for (const auto &issue : issues_) {
		if (scene.Contains(issue.objectId) &&
		    seen.insert(issue.objectId).second) {
			ids.push_back(issue.objectId);
		}
	}
	return ids;
}
