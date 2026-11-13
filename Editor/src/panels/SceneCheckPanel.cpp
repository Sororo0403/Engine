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
		if (filter_.empty()) {
			sceneFilters_.erase(previous);
		} else {
			sceneFilters_.insert_or_assign(previous, filter_);
		}
	}
	filter_.clear();
	if (const auto found = sceneFilters_.find(current); found != sceneFilters_.end()) {
		filter_ = found->second;
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
			if (filter_.empty()) {
				sceneFilters_.erase(*path);
			} else {
				sceneFilters_.insert_or_assign(*path, filter_);
			}
		}
	}
	RequestRecheckIfOpen();
}

void SceneCheckPanel::OnAssetsMoved(
    std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves) {
	// 移動先に過去の検索語があれば、実際に移動したシーンの検索語を優先する。
	std::map<std::filesystem::path, std::string> movedFilters;
	for (auto it = sceneFilters_.begin(); it != sceneFilters_.end();) {
		auto destination = PathUtils::AfterMove(it->first, moves);
		if (destination.native() == it->first.native()) {
			++it;
			continue;
		}
		movedFilters.insert_or_assign(std::move(destination), std::move(it->second));
		it = sceneFilters_.erase(it);
	}
	for (auto &[path, filter] : movedFilters) {
		sceneFilters_.insert_or_assign(path, std::move(filter));
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

	scrollToIssue_.reset();
	if (lastIssue_ && *lastIssue_ < issues_.size()) {
		const auto &previous = issues_[*lastIssue_];
		const auto matches = [&](const SceneValidationIssue &issue) {
			return MatchesFilter(issue, scene);
		};
		auto remaining = std::count_if(issues_.begin(),
		                               issues_.begin() + *lastIssue_, matches);
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
			    issue.assetPath == previous.assetPath) {
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
			if (remaining >= 0) {
				scrollToIssue_ = i;
				--remaining;
			}
		}
		// 診断文が変わったときも、同じ参照元が一件ならその行に戻る。
		if (sameSource && !ambiguousSource) {
			scrollToIssue_ = sameSource;
		}
	}
	lastIssue_ = scrollToIssue_;
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
		lastIssue_.reset();
		scrollToIssue_.reset();
	}
	ui.SameLine();

	if (ui.Button("Clear Search")) {
		filter_.clear();
		lastIssue_.reset();
		scrollToIssue_.reset();
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
			const bool modelDependency =
			    !isReferencedModel && object && object->HasMesh() &&
			    validPath(issue.modelPath) && validPath(object->mesh->modelPath) &&
			    PathUtils::IsSameFile(issue.modelPath, object->mesh->modelPath) &&
			    std::filesystem::is_regular_file(
			        PathUtils::ResolveFromExecutable(issue.modelPath), status);
			const bool reloadModel = isReferencedModel || modelDependency;
			if ((reloadModel || isReferencedTexture) &&
			    std::filesystem::is_regular_file(issue.assetPath, status)) {
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
			if (ui.Button("Show in Assets")) {
				lastIssue_ = i;
				requested = {};
				requested.assetPath = issue.assetPath;
			}
			ui.SameLine();
			if (ui.Button("Copy Asset Path")) {
				lastIssue_ = i;
				ImGui::SetClipboardText(path.c_str());
			}
			ui.SameLine();
			if (ui.Button("Show Location in Explorer")) {
				lastIssue_ = i;
				ShellUtils::RevealLocationInExplorer(issue.assetPath, assetActionError_);
			}
			ImGui::SetItemTooltip("%s", path.c_str());
			const bool script = PathUtils::HasExtension(issue.assetPath, ".lua");
			const bool sceneFile = PathUtils::HasExtension(issue.assetPath, ".json") &&
			                       PathUtils::HasExtension(issue.assetPath.stem(), ".scene");
			if ((script || sceneFile) &&
			    std::filesystem::is_regular_file(issue.assetPath, status)) {
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
