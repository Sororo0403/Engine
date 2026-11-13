#include "panels/SceneCheckPanel.h"
#include "assets/SceneAssetValidation.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include "scene/Scene.h"
#include <algorithm>
#include <exception>
#include <format>
#include <imgui.h>
#include <iterator>
#include <set>
#include <string_view>

void SceneCheckPanel::Show() {
	open_ = true;
	checkRequested_ = true;
	focusRequested_ = true;
}

void SceneCheckPanel::Reset() {
	*this = SceneCheckPanel{};
}

void SceneCheckPanel::Check(const Scene &scene, const CameraPose &fallback,
                            std::span<const GameActionBinding> bindings) {
	auto issues = SceneValidation::Inspect(scene, fallback);
	auto inputs = SceneValidation::InspectInputs(scene, bindings);
	issues.insert(issues.end(), std::make_move_iterator(inputs.begin()),
	              std::make_move_iterator(inputs.end()));
	auto assets = SceneAssetValidation::Inspect(scene);
	issues.insert(issues.end(), std::make_move_iterator(assets.begin()),
	              std::make_move_iterator(assets.end()));
	issues_.swap(issues);
	error_.clear();
	assetActionError_.clear();
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

bool SceneCheckPanel::MatchesFilter(const SceneValidationIssue &issue,
                                    const Scene &scene) const {
	if (filter_.empty() || issue.message.find(filter_) != std::string::npos ||
	    (!issue.assetPath.empty() &&
	     PathUtils::ToUtf8(issue.assetPath).find(filter_) != std::string::npos)) {
		return true;
	}

	if (issue.objectId == 0) {
		return false;
	}

	if (std::to_string(issue.objectId).find(filter_) != std::string::npos) {
		return true;
	}

	const auto *object = scene.FindObject(issue.objectId);

	if (!object) {
		return false;
	}

	const std::string_view name = object->name.empty()
	                                  ? std::string_view{"Object"}
	                                  : std::string_view{object->name};
	return name.find(filter_) != std::string_view::npos;
}

SceneCheckPanel::Request
SceneCheckPanel::Draw(const ImGuiSystem &ui, const Scene &scene,
                      const CameraPose &fallback,
                      std::span<const GameActionBinding> bindings) {
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
			Check(scene, fallback, bindings);
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
	if (!assetActionError_.empty()) {
		ImGui::TextWrapped("%s", assetActionError_.c_str());
	}

	ui.InputText("Search Issues", filter_);
	ui.SameLine();

	if (ui.Button("Clear Search")) {
		filter_.clear();
	}

	const auto matchingCount =
	    std::count_if(issues_.begin(), issues_.end(), [&](const auto &issue) {
		    return MatchesFilter(issue, scene);
	    });
	ui.Text(std::format("Showing {} of {} issue(s)", matchingCount,
	                    issues_.size()));

	if (checked_ && matchingCount == 0 && !issues_.empty()) {
		ui.Text("No issues match the search.");
	}

	Request requested;
	const auto objects = GetIssueObjects(scene);
	ui.BeginDisabled(objects.empty());

	if (ui.Button("Select Listed Issue Objects")) {
		requested.objectIds = objects;
	}

	ui.SameLine();

	if (ui.Button("Frame Listed Issue Objects")) {
		requested = {objects, true};
	}

	ui.EndDisabled();

	for (std::size_t i = 0; i < issues_.size(); ++i) {
		const auto &issue = issues_[i];

		if (!MatchesFilter(issue, scene)) {
			continue;
		}

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

		if (!issue.assetPath.empty()) {
			const auto path = PathUtils::ToUtf8(issue.assetPath);
			if (ui.Button("Copy Asset Path")) {
				ImGui::SetClipboardText(path.c_str());
			}
			ui.SameLine();
			if (ui.Button("Show Location in Explorer")) {
				ShellUtils::RevealLocationInExplorer(issue.assetPath, assetActionError_);
			}
			ImGui::SetItemTooltip("%s", path.c_str());
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
		if (MatchesFilter(issue, scene) && scene.Contains(issue.objectId) &&
		    seen.insert(issue.objectId).second) {
			ids.push_back(issue.objectId);
		}
	}

	return ids;
}
