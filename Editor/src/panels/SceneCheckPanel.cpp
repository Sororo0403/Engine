#include "panels/SceneCheckPanel.h"
#include "assets/ModelUsage.h"
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

void SceneCheckPanel::RequestRecheckIfOpen() {
	checkRequested_ = open_;
}

void SceneCheckPanel::Reset() {
	*this = SceneCheckPanel{};
}

void SceneCheckPanel::OnSceneOpened(const std::filesystem::path &previous,
                                   const std::filesystem::path &current) {
	if (!previous.empty()) {
		auto issue = std::move(issueToRestore_);
		if (lastIssue_ && *lastIssue_ < issues_.size()) {
			issue = issues_[*lastIssue_];
		}
		if (filter_.empty() && !issue) {
			scenePositions_.erase(previous);
		} else {
			scenePositions_.insert_or_assign(previous, SceneCheckPosition{filter_, std::move(issue)});
		}
	}
	filter_.clear();
	issueToRestore_.reset();
	if (const auto found = scenePositions_.find(current); found != scenePositions_.end()) {
		filter_ = found->second.filter;
		issueToRestore_ = found->second.issue;
	}

	issues_.clear();
	lastIssue_.reset();
	scrollToIssue_.reset();
	error_.clear();
	assetActionError_.clear();
	focusRequested_ = false;
	checked_ = false;
	RequestRecheckIfOpen();
}

void SceneCheckPanel::OnSceneSavedAs(const std::filesystem::path &previous,
                                    const std::filesystem::path &current) {
	for (const auto *path : {&previous, &current}) {
		if (!path->empty()) {
			if (!filter_.empty()) {
				scenePositions_[*path].filter = filter_;
			} else if (const auto found = scenePositions_.find(*path);
			           found != scenePositions_.end()) {
				if (found->second.issue) {
					found->second.filter.clear();
				} else {
					scenePositions_.erase(found);
				}
			}
		}
	}
	RequestRecheckIfOpen();
}

void SceneCheckPanel::OnAssetsMoved(
    std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves) {
	const auto relocateIssue = [&](SceneValidationIssue &issue) {
		issue.assetPath = PathUtils::AfterMove(issue.assetPath, moves);
		issue.modelPath = PathUtils::AfterMove(issue.modelPath, moves);
	};
	const auto relocateFilter = [&](std::string &filter) {
		auto path = PathUtils::FromPathSearch(filter);
		if (!path.is_absolute()) {
			return;
		}
		path = path.lexically_normal();
		if (!path.has_filename() && path.has_relative_path()) {
			path = path.parent_path();
		}
		const auto relocated = PathUtils::AfterMove(path, moves);
		if (relocated.native() != path.native()) {
			filter = PathUtils::ToUtf8(relocated);
		}
	};
	relocateFilter(filter_);
	for (auto &issue : issues_) {
		relocateIssue(issue);
	}
	if (issueToRestore_) {
		relocateIssue(*issueToRestore_);
	}

	// 移動先に過去の表示状態があれば、実際に移動したシーンの状態を優先する。
	std::map<std::filesystem::path, SceneCheckPosition> movedPositions;
	for (auto it = scenePositions_.begin(); it != scenePositions_.end();) {
		relocateFilter(it->second.filter);
		if (it->second.issue) {
			relocateIssue(*it->second.issue);
		}
		auto destination = PathUtils::AfterMove(it->first, moves);
		if (destination.native() == it->first.native()) {
			++it;
			continue;
		}
		movedPositions.insert_or_assign(std::move(destination), std::move(it->second));
		it = scenePositions_.erase(it);
	}
	for (auto &[path, position] : movedPositions) {
		scenePositions_.insert_or_assign(path, std::move(position));
	}
	RequestRecheckIfOpen();
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

	const auto searchPath = PathUtils::FromPathSearch(filter_);
	const auto matches = [&](const SceneValidationIssue &issue) {
		return MatchesFilter(issue, scene, searchPath);
	};
	scrollToIssue_.reset();
	const bool hadSelection = lastIssue_ && *lastIssue_ < issues_.size();
	if (!filter_.empty() && !issues.empty() &&
	    (hadSelection || std::ranges::any_of(issues_, matches)) &&
	    std::ranges::none_of(issues, matches)) {
		filter_.clear();
	}

	if (hadSelection || issueToRestore_) {
		const auto &previous = hadSelection ? issues_[*lastIssue_] : *issueToRestore_;
		auto remaining = hadSelection
		                     ? std::count_if(issues_.begin(), issues_.begin() + *lastIssue_, matches)
		                     : 0;
		std::optional<std::size_t> sameSource;
		bool ambiguousSource = false;
		const bool hasSource = previous.objectId != 0 ||
		                       !previous.assetPath.empty() || !previous.modelPath.empty();
		for (std::size_t i = 0; i < issues.size(); ++i) {
			const auto &issue = issues[i];
			if (!matches(issue))
				continue;
			if (issue.objectId == previous.objectId &&
			    issue.destinationObjectId == previous.destinationObjectId &&
			    issue.modelPath == previous.modelPath &&
			    issue.assetPath == previous.assetPath &&
			    issue.inputAction == previous.inputAction) {
				if (issue.message == previous.message) {
					scrollToIssue_ = i;
					sameSource.reset();
					break;
				}
				if (hasSource) {
					ambiguousSource |= sameSource.has_value();
					sameSource = i;
				}
			}
			if (hadSelection && remaining >= 0) {
				scrollToIssue_ = i;
				--remaining;
			}
		}
		// 診断文が変わったときも、同じ参照元が一件ならその行に戻る。
		if (sameSource && !ambiguousSource) {
			scrollToIssue_ = sameSource;
		}
	}
	issueToRestore_.reset();
	lastIssue_ = scrollToIssue_;
	issues_.swap(issues);
	error_.clear();
	assetActionError_.clear();
	checked_ = true;
}

std::string SceneCheckPanel::GetReport(const Scene *filteredScene) const {
	if (!error_.empty()) {
		return std::format("Scene Check failed:\n{}\n", error_);
	}

	if (!checked_) {
		return {};
	}

	std::string report;
	const auto searchPath = filteredScene ? PathUtils::FromPathSearch(filter_)
	                                      : std::filesystem::path{};
	std::size_t count = 0;
	for (std::size_t i = 0; i < issues_.size(); ++i) {
		const auto &issue = issues_[i];
		if (filteredScene && !MatchesFilter(issue, *filteredScene, searchPath)) {
			continue;
		}
		++count;
		report += std::format("\n{}. {}\n", i + 1, issue.message);
		if (issue.objectId != 0) {
			report += std::format("   Object ID: {}\n", issue.objectId);
		}
		if (!issue.inputAction.empty()) {
			report += std::format("   Input action: {}\n", issue.inputAction);
		}
		if (!issue.assetPath.empty()) {
			report += std::format("   Asset: {}\n", PathUtils::ToUtf8(issue.assetPath));
		}
		if (!issue.modelPath.empty() && issue.modelPath != issue.assetPath) {
			report += std::format("   Model: {}\n", PathUtils::ToUtf8(issue.modelPath));
		}
		if (issue.destinationObjectId != 0) {
			report += std::format("   Destination Object ID: {}\n", issue.destinationObjectId);
		}
	}

	const auto heading = filteredScene
	                         ? std::format("Last Scene Check: {} of {} issue(s) match the search\n",
	                                       count, issues_.size())
	                         : std::format("Last Scene Check: {} issue(s)\n", count);
	return heading + report;
}

bool SceneCheckPanel::MatchesFilter(const SceneValidationIssue &issue,
                                    const Scene &scene,
                                    const std::filesystem::path &searchPath) const {
	if (filter_.empty() || issue.message.find(filter_) != std::string::npos ||
	    issue.inputAction.find(filter_) != std::string::npos) {
		return true;
	}
	if (PathUtils::MatchesPathSearch(issue.assetPath, searchPath) ||
	    PathUtils::MatchesPathSearch(issue.modelPath, searchPath)) {
		return true;
	}

	const auto matchesId = [&](SceneObjectId id) {
		return id != 0 && std::to_string(id).find(filter_) != std::string::npos;
	};
	if (matchesId(issue.objectId) || matchesId(issue.destinationObjectId)) {
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
                      std::span<const GameActionBinding> bindings,
                      std::uint64_t editStateId) {
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

	bool resultsOutdated =
	    checked_ && (checkedEditStateId_ != editStateId ||
	                 !std::ranges::equal(checkedBindings_, bindings));
	if (ui.Button(resultsOutdated ? "Recheck Scene" : "Check Scene") ||
	    checkRequested_) {
		checkRequested_ = false;

		try {
			std::vector<GameActionBinding> currentBindings(bindings.begin(), bindings.end());
			Check(scene, fallback, bindings);
			checkedBindings_.swap(currentBindings);
			checkedEditStateId_ = editStateId;
			resultsOutdated = false;
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
		if (resultsOutdated) {
			ui.Text("Scene or input bindings changed. Recheck to update results.");
		}
	}
	if (!assetActionError_.empty()) {
		ImGui::TextWrapped("%s", assetActionError_.c_str());
	}

	if (ui.InputText("Search Issues", filter_)) {
		issueToRestore_.reset();
		lastIssue_.reset();
		scrollToIssue_.reset();
	}
	ui.SameLine();

	if (ui.Button("Clear Search")) {
		filter_.clear();
		if (lastIssue_) {
			scrollToIssue_ = lastIssue_;
		}
	}

	const auto searchPath = PathUtils::FromPathSearch(filter_);
	const auto matchingCount =
	    std::count_if(issues_.begin(), issues_.end(), [&](const auto &issue) {
		    return MatchesFilter(issue, scene, searchPath);
	    });
	ui.Text(std::format("Showing {} of {} issue(s)", matchingCount,
	                    issues_.size()));
	if (!filter_.empty() && checked_ && error_.empty()) {
		ui.SameLine();
		ui.BeginDisabled(matchingCount == 0);
		if (ui.Button("Copy Matching Issues")) {
			const auto report = GetReport(&scene);
			ImGui::SetClipboardText(report.c_str());
		}
		ui.EndDisabled();
	}

	if (checked_ && matchingCount == 0 && !issues_.empty()) {
		ui.Text("No issues match the search.");
	}

	Request requested;
	const auto objects = GetIssueObjects(scene, searchPath);
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

		if (!MatchesFilter(issue, scene, searchPath)) {
			continue;
		}

		ImGui::PushID(static_cast<int>(i));
		ImGui::Separator();
		ImGui::TextWrapped("%s", issue.message.c_str());
		if (scrollToIssue_ == i) {
			ImGui::SetScrollHereY(0.45f);
			scrollToIssue_.reset();
		}

		if (issue.objectId != 0) {
			ui.BeginDisabled(!scene.Contains(issue.objectId));

			if (ui.Button("Select Object")) {
				lastIssue_ = i;
				requested = {{issue.objectId}, false};
			}

			ui.SameLine();

			if (ui.Button("Frame Object")) {
				lastIssue_ = i;
				requested = {{issue.objectId}, true};
			}

			ui.SameLine();

			if (ui.Button("Inspect Object")) {
				lastIssue_ = i;
				requested = {{issue.objectId}, false, true};
			}

			ui.EndDisabled();
		}

		if (!issue.inputAction.empty()) {
			if (issue.objectId != 0) {
				ui.SameLine();
			}
			ui.BeginDisabled(resultsOutdated || !error_.empty());
			if (ui.Button("Edit Input Binding")) {
				lastIssue_ = i;
				requested = {};
				requested.inputAction = issue.inputAction;
			}
			ui.EndDisabled();
		}

		if (!issue.assetPath.empty()) {
			const auto path = PathUtils::ToUtf8(issue.assetPath);
			const auto *object = scene.FindObject(issue.objectId);
			const auto validPath = [](const std::filesystem::path &reference) {
				return !reference.empty() &&
				       (!reference.has_root_path() || reference.is_absolute()) &&
				       reference.native().find(L'\0') == std::wstring::npos;
			};
			const auto matchesAsset = [&](const std::filesystem::path &reference) {
				return validPath(reference) && PathUtils::IsSameFile(issue.assetPath, reference);
			};
			// モデル本体の診断だけを対象にし、依存ファイルの診断からは選択しない。
			const bool isReferencedModel =
			    object && object->HasMesh() && PathUtils::IsModelFile(issue.assetPath) &&
			    matchesAsset(object->mesh->modelPath);
			if (isReferencedModel && ui.Button("Select All Using This Model")) {
				try {
					auto users = ModelUsage::FindObjects(scene, issue.assetPath);
					if (!users.empty()) {
						lastIssue_ = i;
						requested = {std::move(users)};
						assetActionError_.clear();
					}
				} catch (const std::exception &error) {
					assetActionError_ = error.what();
				}
			}
			const bool isReferencedTexture =
			    object && PathUtils::IsImageFile(issue.assetPath) &&
			    ((object->image && matchesAsset(object->image->texture)) ||
			     (object->particleEmitter && matchesAsset(object->particleEmitter->texture)) ||
			     (object->meshRenderer &&
			      std::ranges::any_of(object->meshRenderer->textures.slots,
			                          [&](const auto &slot) { return matchesAsset(slot.second); })));
			std::error_code status;
			const auto assetStatus = std::filesystem::status(
			    validPath(issue.assetPath) ? PathUtils::ResolveFromExecutable(issue.assetPath)
			                               : issue.assetPath,
			    status);
			const bool assetExists = std::filesystem::is_regular_file(assetStatus);
			const bool missing = assetStatus.type() == std::filesystem::file_type::not_found;
			const bool modelDependency =
			    !isReferencedModel && object && object->HasMesh() &&
			    validPath(issue.modelPath) && validPath(object->mesh->modelPath) &&
			    PathUtils::IsSameFile(issue.modelPath, object->mesh->modelPath) &&
			    std::filesystem::is_regular_file(
			        PathUtils::ResolveFromExecutable(issue.modelPath), status);
			const bool reloadModel = isReferencedModel || modelDependency;
			if ((reloadModel || isReferencedTexture) && assetExists) {
				if (isReferencedModel)
					ui.SameLine();
				if (ui.Button(reloadModel ? "Reload Model" : "Reload Texture")) {
					lastIssue_ = i;
					requested = {};
					if (reloadModel)
						requested.modelReloadPath = modelDependency ? issue.modelPath : issue.assetPath;
					else
						requested.textureReloadPath = issue.assetPath;
				}
			}
			ui.BeginDisabled(missing);
			if (ui.Button("Show in Assets")) {
				lastIssue_ = i;
				requested = {};
				requested.assetPath = issue.assetPath;
			}
			ui.EndDisabled();
			if (missing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled |
			                                    ImGuiHoveredFlags_ForTooltip)) {
				ImGui::SetTooltip("The referenced file is missing.");
			}
			ui.SameLine();
			if (ui.Button("Copy Asset Path")) {
				lastIssue_ = i;
				ImGui::SetClipboardText(path.c_str());
			}
			ui.SameLine();
			if (ui.Button(missing ? "Open Folder in Explorer" : "Show Location in Explorer")) {
				lastIssue_ = i;
				ShellUtils::RevealLocationInExplorer(issue.assetPath, assetActionError_);
			}
			ImGui::SetItemTooltip("%s", path.c_str());
			const bool script = PathUtils::HasExtension(issue.assetPath, ".lua");
			const bool sceneFile = PathUtils::HasExtension(issue.assetPath, ".json") &&
			                       PathUtils::HasExtension(issue.assetPath.stem(), ".scene");
			if ((script || sceneFile) && assetExists) {
				const char *label = script ? "Edit Script"
				                    : issue.destinationObjectId != 0 ? "Show Destination Object"
				                                                    : "Open Referenced Scene";
				if (ui.Button(label)) {
					lastIssue_ = i;
					requested = {};
					if (script) {
						requested.scriptPath = issue.assetPath;
						requested.scriptDiagnostic = issue.message;
					} else {
						requested.scenePath = issue.assetPath;
						requested.destinationObjectId = issue.destinationObjectId;
					}
				}
			}
		}

		if (issue.objectId != 0 || !issue.inputAction.empty() || !issue.assetPath.empty()) {
			ui.SameLine();
		}
		if (ui.Button("Recheck Here")) {
			lastIssue_ = i;
			checkRequested_ = true;
		}
		ImGui::SetItemTooltip("Check the scene again and resume at this issue.");

		ImGui::PopID();
	}

	ImGui::End();
	return requested;
}

std::vector<SceneObjectId>
SceneCheckPanel::GetIssueObjects(const Scene &scene,
                                const std::filesystem::path &searchPath) const {
	std::vector<SceneObjectId> ids;
	std::set<SceneObjectId> seen;

	for (const auto &issue : issues_) {
		if (MatchesFilter(issue, scene, searchPath) && scene.Contains(issue.objectId) &&
		    seen.insert(issue.objectId).second) {
			ids.push_back(issue.objectId);
		}
	}

	return ids;
}
