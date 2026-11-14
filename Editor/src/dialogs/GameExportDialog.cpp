#include "dialogs/GameExportDialog.h"
#include "assets/AssetFolders.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include <Windows.h>
#include <algorithm>
#include <format>
#include <imgui.h>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
constexpr std::size_t kIssuesPerPage = 25;

/// <summary>
/// 配布前検査の結果から入力欄へ移動するボタンの表示名を返す
/// </summary>
const char *EditFieldLabel(GameExportField field) {
	switch (field) {
	case GameExportField::OUTPUT_FOLDER:
		return "Edit Output Folder";
	case GameExportField::TITLE:
		return "Edit Game Title";
	case GameExportField::WIDTH:
		return "Edit Width";
	case GameExportField::HEIGHT:
		return "Edit Height";
	case GameExportField::STARTUP_SCENE:
	case GameExportField::NONE:
		return nullptr;
	}
	return nullptr;
}

/// <summary>
/// 出力欄の入力を読み取り、ルート以外の末尾の区切りを取り除く
/// </summary>
std::filesystem::path ReadDestination(const std::string &input) {
	auto path = PathUtils::FromPathInput(input);
	if (!path.empty() && !path.has_root_path()) {
		path = PathUtils::ResolveFromExecutable(path);
	}
	if (!path.has_filename() && path.has_relative_path()) {
		path = path.parent_path();
	}
	return path;
}

/// <summary>
/// 初期値と同じ名前の項目があれば、空いている連番の出力先を探す
/// </summary>
std::filesystem::path FindAvailableDestination(
    const std::filesystem::path &destination) {
	const auto parent = destination.parent_path();
	return parent / AssetFolders::SuggestAvailableName(
	                    parent, destination.filename().native(), L"");
}

/// <summary>
/// 候補の親から既存のフォルダを探す
/// </summary>
std::filesystem::path FindExistingParent(const std::filesystem::path &output) {
	auto parent = output.parent_path();
	std::error_code error;
	while (!parent.empty() && !std::filesystem::is_directory(parent, error)) {
		auto next = parent.parent_path();
		if (next == parent) {
			return {};
		}
		parent = std::move(next);
	}

	return parent;
}

/// <summary>
/// 検索語をWindowsのパス表記にそろえる。変換できない場合は本文だけを検索する
/// </summary>
std::filesystem::path IssueSearchPath(std::string_view filter) {
	const auto first = filter.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) {
		return {};
	}
	const auto last = filter.find_last_not_of(" \t\r\n");
	try {
		auto path = PathUtils::FromPathInput(filter.substr(first, last - first + 1));
		return path.make_preferred();
	} catch (const std::invalid_argument &) {
		return {};
	}
}

/// <summary>
/// 検索語が問題の説明または参照先に含まれるか確認する
/// </summary>
bool MatchesIssue(const GameExportIssue &issue, std::string_view filter,
                  const std::filesystem::path &searchPath) {
	const auto matchesPath = [&](std::filesystem::path path) {
		return !searchPath.empty() && !path.empty() &&
		       FindStringOrdinal(FIND_FROMSTART, path.make_preferred().c_str(), -1,
		                         searchPath.c_str(), -1, TRUE) >= 0;
	};
	return filter.empty() ||
	       issue.message.find(filter) != std::string::npos ||
	       matchesPath(issue.scenePath) || matchesPath(issue.scriptPath) ||
	       matchesPath(issue.assetPath) || matchesPath(issue.transitionScenePath) ||
	       matchesPath(issue.locationPath);
}

/// <summary>
/// 指摘を共有できるよう、本文にない参照元と修正先を付ける
/// </summary>
void AppendIssueDetails(std::string &report, const GameExportIssue &issue) {
	if (!report.empty()) {
		report += "\n\n";
	}
	report += issue.message;
	const auto appendPath = [&](const char *label, const std::filesystem::path &path) {
		if (!path.empty()) {
			const auto text = PathUtils::ToUtf8(path);
			if (issue.message.find(text) == std::string::npos) {
				report += std::format("\n{}: {}", label, text);
			}
		}
	};
	appendPath("Scene", issue.scenePath);
	if (issue.objectId != 0) {
		report += std::format("\nObject ID: {}", issue.objectId);
	}
	appendPath("Transition source", issue.transitionScenePath);
	if (issue.transitionObjectId != 0) {
		report += std::format("\nTransition source object ID: {}", issue.transitionObjectId);
	}
	appendPath("Script", issue.scriptPath);
	appendPath("Asset", issue.assetPath);
	appendPath("Location", issue.locationPath);
}

} // namespace

bool GameExportDialog::Show(GameExportOptions options, bool includesUnsavedEdits,
                            const std::filesystem::path &projectRoot,
                            const std::filesystem::path &lastExport,
                            const std::filesystem::path &windowSource) {
	const bool resume = std::exchange(resumeDraft_, false) &&
	                    windowSource.empty() &&
	                    projectRoot_ == projectRoot &&
	                    options_.startupScenePath == options.startupScenePath;
	const bool recheck = resume && (!checkResult_.empty() || recheckRestoredDraft_);
	if (resume) {
		options.destination = options_.destination;
		options.config.title = title_;
		options.config.width = width_;
		options.config.height = height_;
	}
	projectRoot_ = projectRoot;
	lastSuccessfulWindow_.reset();
	const auto &settingsSource = windowSource.empty() ? lastExport : windowSource;
	if (!settingsSource.empty() && (!projectRoot.empty() || !windowSource.empty())) {
		GameConfig previous;
		std::string error;
		if (previous.Load(settingsSource / L"game.json", error)) {
			lastSuccessfulWindow_ = WindowSettings{
			    std::move(previous.title), previous.width, previous.height};
		}
	}
	if (!windowSource.empty() && lastSuccessfulWindow_) {
		options.config.title = lastSuccessfulWindow_->title;
		options.config.width = lastSuccessfulWindow_->width;
		options.config.height = lastSuccessfulWindow_->height;
	}
	if (!windowSource.empty()) {
		lastSuccessfulWindow_.reset();
	}
	options_ = std::move(options);
	defaultOutputName_ = options_.destination.filename();
	if (!PathUtils::IsValidFileName(defaultOutputName_)) {
		defaultOutputName_ = L"Game";
	}
	startupScene_ = options_.startupScenePath.empty()
	                    ? "Untitled" : PathUtils::ToUtf8(options_.startupScenePath);
	includesUnsavedEdits_ = includesUnsavedEdits;
	if (!recheck) {
		ClearCheckResult();
	}
	error_.clear();
	browsing_ = false;
	focusField_ = GameExportField::NONE;

	if (!resume) {
		issueFilter_.clear();
		output_ = PathUtils::ToUtf8(options_.destination);
		title_ = options_.config.title;
		width_ = options_.config.width;
		height_ = options_.config.height;
		try {
			output_ = PathUtils::ToUtf8(FindAvailableDestination(options_.destination));
		} catch (const std::exception &e) {
			error_ = e.what();
		}
	}

	opening_ = true;
	return recheck;
}

void GameExportDialog::ForgetUntitledDraft() {
	if (options_.startupScenePath.empty()) {
		resumeDraft_ = false;
	}
}

void GameExportDialog::SwitchProject(const std::filesystem::path &projectRoot) {
	if (projectRoot_ == projectRoot) {
		return;
	}

	if (resumeDraft_ && !projectRoot_.empty() && !options_.startupScenePath.empty()) {
		projectDrafts_.insert_or_assign(
		    projectRoot_, Draft{std::move(options_.startupScenePath),
		                       std::move(options_.destination), std::move(output_),
		                       {std::move(title_), width_, height_}, std::move(checkResult_),
		                       std::move(issueFilter_), std::move(checkIssues_), selectedIssue_,
		                       recheckRestoredDraft_});
	}

	projectRoot_ = projectRoot;
	resumeDraft_ = false;
	ClearCheckResult();
	if (auto entry = projectDrafts_.extract(projectRoot_); !entry.empty()) {
		auto &draft = entry.mapped();
		options_.startupScenePath = std::move(draft.startupScene);
		options_.destination = std::move(draft.destination);
		output_ = std::move(draft.output);
		title_ = std::move(draft.window.title);
		width_ = draft.window.width;
		height_ = draft.window.height;
		checkResult_ = std::move(draft.checkResult);
		issueFilter_ = std::move(draft.issueFilter);
		checkIssues_ = std::move(draft.issues);
		selectedIssue_ = draft.selectedIssue;
		recheckRestoredDraft_ = draft.recheck;
		resumeDraft_ = true;
	}
}

std::optional<GameExportDraft> GameExportDialog::GetDraft() const {
	if (!resumeDraft_ || projectRoot_.empty() || options_.startupScenePath.empty()) {
		return std::nullopt;
	}
	return GameExportDraft{options_.startupScenePath, output_, title_, width_, height_,
	                       !checkResult_.empty() || recheckRestoredDraft_};
}

void GameExportDialog::RestoreDraft(const GameExportDraft &draft) {
	if (resumeDraft_ || projectRoot_.empty()) {
		return;
	}

	options_.startupScenePath = draft.startupScenePath;
	try {
		options_.destination = ReadDestination(draft.output);
	} catch (const std::invalid_argument &) {
		options_.destination.clear();
	}
	output_ = draft.output;
	title_ = draft.title;
	width_ = draft.width;
	height_ = draft.height;
	recheckRestoredDraft_ = draft.recheck;
	resumeDraft_ = true;
}

void GameExportDialog::UpdateStartupScene(const std::filesystem::path &path,
                                          bool includesUnsavedEdits) {
	startupScene_ = path.empty() ? "Untitled" : PathUtils::ToUtf8(path);
	if (options_.startupScenePath != path) {
		issueFilter_.clear();
	}
	options_.startupScenePath = path;
	includesUnsavedEdits_ = includesUnsavedEdits;
	ClearCheckResult();
}

void GameExportDialog::FollowSavedScene(
    const std::filesystem::path &projectRoot,
    const std::filesystem::path &previousPath,
    const std::filesystem::path &savedPath) {
	if (resumeDraft_ && projectRoot_ == projectRoot &&
	    options_.startupScenePath == previousPath) {
		options_.startupScenePath = savedPath;
	}
}

void GameExportDialog::FollowStartupScene(
    const std::filesystem::path &projectRoot,
    const std::filesystem::path &previousPath,
    const std::filesystem::path &startupPath) {
	if (!resumeDraft_ || projectRoot_ != projectRoot ||
	    previousPath == startupPath || options_.startupScenePath != previousPath) {
		return;
	}

	options_.startupScenePath = startupPath;
	// 再検査は行うが、変更前の起動シーンの指摘や検索位置は引き継がない。
	selectedIssue_.reset();
	issueFilter_.clear();
}

void GameExportDialog::FollowMovedScene(
    const std::filesystem::path &projectRoot,
    std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves) {
	if (resumeDraft_ && projectRoot_ == projectRoot) {
		options_.startupScenePath = PathUtils::AfterMove(options_.startupScenePath, moves);
		for (auto &issue : checkIssues_) {
			issue.scenePath = PathUtils::AfterMove(issue.scenePath, moves);
			auto scriptPath = PathUtils::AfterMove(issue.scriptPath, moves);
			if (scriptPath.native() != issue.scriptPath.native()) {
				if (issue.scriptDiagnosticPath.empty()) {
					issue.scriptDiagnosticPath = std::move(issue.scriptPath);
				}
				issue.scriptPath = std::move(scriptPath);
			}
			issue.assetPath = PathUtils::AfterMove(issue.assetPath, moves);
			issue.transitionScenePath = PathUtils::AfterMove(issue.transitionScenePath, moves);
			if (issue.field != GameExportField::OUTPUT_FOLDER) {
				issue.locationPath = PathUtils::AfterMove(issue.locationPath, moves);
			}
		}

		const auto searchPath = IssueSearchPath(issueFilter_);
		if (searchPath.is_absolute()) {
			auto normalized = searchPath.lexically_normal();
			if (!normalized.has_filename() && normalized.has_relative_path()) {
				normalized = normalized.parent_path();
			}
			auto relocated = PathUtils::AfterMove(normalized, moves);
			if (relocated.native() != normalized.native()) {
				issueFilter_ = PathUtils::ToUtf8(relocated.make_preferred());
			}
		}
	}
}

void GameExportDialog::SetCheckResult(std::string message,
                                      std::vector<GameExportIssue> issues,
                                      bool continueFromIssue) {
	issueToScrollTo_.reset();
	if (!issues.empty() && continueFromIssue && selectedIssue_ &&
	    *selectedIssue_ < checkIssues_.size()) {
		const auto &previous = checkIssues_[*selectedIssue_];
		const auto searchPath = IssueSearchPath(issueFilter_);
		const auto matches = [&](const auto &issue) {
			return MatchesIssue(issue, issueFilter_, searchPath);
		};
		const auto sameSource = [](const GameExportIssue &a, const GameExportIssue &b) {
			return a.objectId == b.objectId && a.scenePath == b.scenePath &&
			       a.scriptPath == b.scriptPath && a.assetPath == b.assetPath &&
			       a.locationPath == b.locationPath && a.field == b.field &&
			       a.inputAction == b.inputAction &&
			       a.transitionScenePath == b.transitionScenePath &&
			       a.transitionObjectId == b.transitionObjectId;
		};
		const auto findIssue = [&](const GameExportIssue &target) {
			return std::ranges::find_if(issues, [&](const auto &issue) {
				return matches(issue) && sameSource(issue, target) &&
				       issue.message == target.message;
			});
		};
		auto sameIssue = findIssue(previous);
		const bool hasSource = previous.objectId != 0 ||
		                       previous.field != GameExportField::NONE ||
		                       !previous.scenePath.empty() || !previous.scriptPath.empty() ||
		                       !previous.assetPath.empty() || !previous.locationPath.empty() ||
		                       !previous.transitionScenePath.empty() ||
		                       previous.transitionObjectId != 0;
		if (sameIssue == issues.end() && hasSource) {
			// 診断文が変わっても、参照元が一意なら同じ箇所へ戻る。
			const auto fromPreviousSource = [&](const auto &issue) {
				return matches(issue) && sameSource(issue, previous);
			};
			const auto candidate = std::ranges::find_if(issues, fromPreviousSource);
			if (candidate != issues.end() &&
			    std::find_if(candidate + 1, issues.end(), fromPreviousSource) == issues.end()) {
				sameIssue = candidate;
			}
		}
		if (sameIssue == issues.end()) {
			// 上の行が増減しても、修正した指摘の次に確認していた行を探す。
			const auto next = std::find_if(checkIssues_.begin() + *selectedIssue_ + 1,
			                               checkIssues_.end(), matches);
			if (next != checkIssues_.end()) {
				sameIssue = findIssue(*next);
			}
		}
		if (sameIssue != issues.end()) {
			issueToScrollTo_ = static_cast<std::size_t>(sameIssue - issues.begin());
		} else {
			// 修正した問題が消えたときは、検索結果に残る最も近い行へ戻る。
			auto remaining = std::count_if(checkIssues_.begin(),
			                               checkIssues_.begin() + *selectedIssue_, matches);
			for (std::size_t i = 0; i < issues.size(); ++i) {
				if (matches(issues[i])) {
					issueToScrollTo_ = i;
					if (remaining == 0) {
						break;
					}
					--remaining;
				}
			}
		}
	}
	selectedIssue_ = issueToScrollTo_;
	issuePage_ = 0;
	if (!issues.empty() && !issueToScrollTo_) {
		issueToScrollTo_ = 0;
	}
	checkResult_ = std::move(message);
	recheckRestoredDraft_ = false;
	checkResultStale_ = false;
	checkIssues_ = std::move(issues);
}

const GameExportIssue &GameExportDialog::GetSelectedIssue() const {
	return checkIssues_[*selectedIssue_];
}

void GameExportDialog::ClearCheckResult() {
	checkResult_.clear();
	recheckRestoredDraft_ = false;
	checkResultStale_ = false;
	checkIssues_.clear();
	selectedIssue_.reset();
	issueToScrollTo_.reset();
	issuePage_ = 0;
}

void GameExportDialog::ShowExportFailure(std::string error,
                                         std::vector<GameExportIssue> issues) {
	SetCheckResult(std::move(error), std::move(issues));
	Reopen();
}

void GameExportDialog::Reopen(std::string error) {
	error_ = std::move(error);
	opening_ = true;
}

void GameExportDialog::PreserveDraft() {
	resumeDraft_ = true;
}

bool GameExportDialog::SuggestOutputName() {
	try {
		const auto output = ReadDestination(output_);
		if (!output.is_absolute() ||
		    !PathUtils::IsValidFileName(output.filename())) {
			throw std::runtime_error("Enter a valid output folder name.");
		}
		const auto suggested = FindAvailableDestination(output);
		error_.clear();
		if (suggested != output) {
			output_ = PathUtils::ToUtf8(suggested);
			checkResultStale_ = !checkResult_.empty();
			return true;
		}
	} catch (const std::exception &e) {
		error_ = e.what();
	}
	return false;
}

GameExportResult GameExportDialog::Draw(const ImGuiSystem &ui,
                                        bool canSaveAndCheck) {
	constexpr const char *title = "Export Game###GameExportDialog";

	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}

	ImGui::SetNextWindowSize({600, 0}, ImGuiCond_Appearing);

	if (!ui.BeginPopupModal(title)) {
		return GameExportResult::NONE;
	}

	ImGui::TextWrapped("Startup Scene: %s", startupScene_.c_str());
	if (includesUnsavedEdits_) {
		ui.Text("Includes unsaved changes to the startup scene.");
	} else if (canSaveAndCheck) {
		ui.Text("Check Export reads the saved version of linked scenes.");
	}
	const bool wasBrowsing = browsing_;
	ui.BeginDisabled(wasBrowsing);
	bool chooseStartup = false;
	if (!projectRoot_.empty()) {
		chooseStartup = ui.Button("Choose Startup Scene...");
		ImGui::SetItemTooltip("Change the startup scene saved in this project.");
	}
	ui.Text("Output Folder");
	const auto focusField = std::exchange(focusField_, GameExportField::NONE);
	const auto drawField = [&](GameExportField field, const auto &drawInput) {
		if (focusField == field) {
			ImGui::SetKeyboardFocusHere();
		}
		const bool changed = drawInput();
		if (focusField == field) {
			ImGui::SetScrollHereY(0.0f);
		}
		return changed;
	};
	bool changed = drawField(GameExportField::OUTPUT_FOLDER, [&] {
		return ui.InputText("##OutputFolder", output_);
	});
	ui.SameLine();
	if (ui.Button("Browse...")) {
		auto output = options_.destination;
		browseOutputName_ = defaultOutputName_;
		try {
			const auto entered = ReadDestination(output_);
			if (PathUtils::IsValidFileName(entered.filename())) {
				browseOutputName_ = entered.filename();
			}
			if (entered.is_absolute()) {
				output = entered;
			}
		} catch (const std::exception &) {
			// 入力途中のパスを読めない場合は初期の出力先から探す。
		}
		folderDialog_.Show(FileDialogMode::SELECT_DIRECTORY,
		                   FindExistingParent(output));
		browsing_ = true;
	}
	ui.SameLine();
	ui.BeginDisabled(output_.empty());
	if (ui.Button("Suggest Name")) {
		SuggestOutputName();
	}
	ui.EndDisabled();
	changed |= drawField(GameExportField::TITLE, [&] {
		return ui.InputText("Game Title", title_);
	});
	changed |= drawField(GameExportField::WIDTH, [&] {
		return ImGui::InputInt("Width", &width_);
	});
	changed |= drawField(GameExportField::HEIGHT, [&] {
		return ImGui::InputInt("Height", &height_);
	});
	if (lastSuccessfulWindow_) {
		const auto &last = *lastSuccessfulWindow_;
		ui.BeginDisabled(title_ == last.title && width_ == last.width && height_ == last.height);
		if (ui.Button("Use Last Export Window Settings")) {
			title_ = last.title;
			width_ = last.width;
			height_ = last.height;
			changed = true;
		}
		ui.EndDisabled();
	}
	if (changed) {
		error_.clear();
		checkResultStale_ = !checkResult_.empty();
	}

	ui.BeginDisabled(output_.empty());
	if (ui.Button("Show Output Location")) {
		try {
			const auto output = ReadDestination(output_);
			if (!output.is_absolute()) {
				throw std::invalid_argument("Enter a valid output path.");
			}
			ShellUtils::RevealLocationInExplorer(output, error_);
		} catch (const std::exception &error) {
			error_ = error.what();
		}
	}
	ui.EndDisabled();
	ImGui::SetItemTooltip("Show the output path or its nearest existing folder in Explorer.");

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}
	GameExportResult result = GameExportResult::NONE;
	bool recheckIssue = false;
	if (checkResultStale_) {
		ui.Text("Settings changed. Run Check Export to update the results.");
	}
	if (!checkIssues_.empty()) {
		if (ui.InputText("Search Issues", issueFilter_)) {
			selectedIssue_.reset();
			issueToScrollTo_ = 0;
			issuePage_ = 0;
		}
		ui.SameLine();
		ui.BeginDisabled(issueFilter_.empty());
		if (ui.Button("Clear Search")) {
			issueFilter_.clear();
			selectedIssue_.reset();
			issueToScrollTo_ = 0;
			issuePage_ = 0;
		}
		ui.EndDisabled();
		const auto searchPath = IssueSearchPath(issueFilter_);
		std::vector<std::size_t> matching;
		for (std::size_t i = 0; i < checkIssues_.size(); ++i) {
			if (MatchesIssue(checkIssues_[i], issueFilter_, searchPath)) {
				matching.push_back(i);
			}
		}
		ui.Text(std::format("Matching issues: {} / {}", matching.size(), checkIssues_.size()));
		const auto lastPage = matching.empty() ? 0 : (matching.size() - 1) / kIssuesPerPage;
		if (issueToScrollTo_) {
			const auto found = std::ranges::lower_bound(matching, *issueToScrollTo_);
			issuePage_ = static_cast<std::size_t>(found - matching.begin()) / kIssuesPerPage;
		}
		issuePage_ = (std::min)(issuePage_, lastPage);
		bool resetScroll = issueToScrollTo_ == 0;
		const auto goToPage = [&](std::size_t page) {
			issuePage_ = page;
			selectedIssue_ = matching[page * kIssuesPerPage];
			issueToScrollTo_.reset();
			resetScroll = true;
		};
		if (lastPage != 0) {
			ui.BeginDisabled(issuePage_ == 0);
			if (ui.Button("Previous Issues")) {
				goToPage(issuePage_ - 1);
			}
			ui.EndDisabled();
			ui.SameLine();
			ui.Text(std::format("Page {} / {}", issuePage_ + 1, lastPage + 1));
			ui.SameLine();
			ui.BeginDisabled(issuePage_ == lastPage);
			if (ui.Button("Next Issues")) {
				goToPage(issuePage_ + 1);
			}
			ui.EndDisabled();
		}
		if (resetScroll) {
			ImGui::SetNextWindowScroll({-1.0f, 0.0f});
		}
		const float height = 8.0f * ImGui::GetTextLineHeightWithSpacing();
		if (ImGui::BeginChild("ExportCheckResult", {0.0f, height})) {
			if (matching.empty()) {
				ui.Text("No issues match the search. Clear Search to see all issues.");
			}
			const auto first = issuePage_ * kIssuesPerPage;
			const auto end = (std::min)(first + kIssuesPerPage, matching.size());
			for (auto row = first; row < end; ++row) {
				const auto i = matching[row];
				const auto &issue = checkIssues_[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TextWrapped("%s", issue.message.c_str());
				if (issueToScrollTo_ == i && i != 0) {
					ImGui::SetScrollHereY(0.0f);
				}
				const bool hasObject =
				    issue.objectId != 0 || !issue.scenePath.empty();
				if (hasObject &&
				    ui.Button(issue.objectId == 0 ? "Open Scene" : "Show Object")) {
					selectedIssue_ = i;
					result = GameExportResult::OPEN_ISSUE;
				}
				if (issue.transitionObjectId != 0) {
					if (hasObject) {
						ui.SameLine();
					}
					if (ui.Button("Show Transition Source")) {
						selectedIssue_ = i;
						result = GameExportResult::OPEN_TRANSITION_SOURCE;
					}
				}
				if (!issue.scriptPath.empty()) {
					if (hasObject)
						ui.SameLine();
					if (ui.Button("Edit Script")) {
						selectedIssue_ = i;
						result = GameExportResult::EDIT_ISSUE_SCRIPT;
					}
				}
				if (!issue.inputAction.empty()) {
					if (hasObject) {
						ui.SameLine();
					}
					if (ui.Button("Edit Input Binding")) {
						selectedIssue_ = i;
						result = GameExportResult::EDIT_ISSUE_INPUT;
					}
				}
				if (!issue.assetPath.empty()) {
					const auto assetPath = PathUtils::ToUtf8(issue.assetPath);
					std::error_code statusError;
					const auto status = std::filesystem::symlink_status(
					    issue.assetPath, statusError);
					const bool missing =
					    status.type() == std::filesystem::file_type::not_found;
					ui.BeginDisabled(missing);
					if (ui.Button("Show in Assets")) {
						selectedIssue_ = i;
						result = GameExportResult::REVEAL_ISSUE_ASSET;
					}
					ui.EndDisabled();
					if (missing &&
					    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
						ImGui::SetTooltip("The referenced file is missing.");
					}
					ui.SameLine();
					if (ui.Button(missing ? "Open Folder in Explorer" : "Show in Explorer")) {
						selectedIssue_ = i;
						ShellUtils::RevealLocationInExplorer(issue.assetPath, error_);
					}
					ImGui::SetItemTooltip("%s", assetPath.c_str());
					ui.SameLine();
					if (ui.Button("Copy Asset Path")) {
						ImGui::SetClipboardText(assetPath.c_str());
					}
				}
				if (!issue.locationPath.empty()) {
					const auto path = PathUtils::ToUtf8(issue.locationPath);
					if (ui.Button("Show Location in Explorer")) {
						selectedIssue_ = i;
						ShellUtils::RevealLocationInExplorer(issue.locationPath, error_);
					}
					ImGui::SetItemTooltip("%s", path.c_str());
					ui.SameLine();
					if (ui.Button("Copy Path")) {
						ImGui::SetClipboardText(path.c_str());
					}
				}
				if (!issue.assetPath.empty() || !issue.locationPath.empty()) {
					ui.SameLine();
					if (ui.Button("Recheck Here")) {
						selectedIssue_ = i;
						recheckIssue = true;
					}
					ImGui::SetItemTooltip(
					    "Run Check Export and resume the list at this issue.");
				}
				if (const auto *label = EditFieldLabel(issue.field); label && ui.Button(label)) {
					selectedIssue_ = i;
					focusField_ = issue.field;
				}
				if (issue.field == GameExportField::STARTUP_SCENE &&
				    !projectRoot_.empty() && ui.Button("Choose Replacement Startup Scene...")) {
					chooseStartup = true;
				}
				if (issue.field == GameExportField::OUTPUT_FOLDER &&
				    !issue.locationPath.empty() && !checkResultStale_) {
					std::error_code status;
					if (std::filesystem::exists(
					        std::filesystem::symlink_status(issue.locationPath, status))) {
						ui.SameLine();
						if (ui.Button("Use Next Available Name") && SuggestOutputName()) {
							selectedIssue_ = i;
							focusField_ = GameExportField::OUTPUT_FOLDER;
						}
					}
				}
				ImGui::PopID();
				ImGui::Spacing();
			}
			issueToScrollTo_.reset();
		}
		ImGui::EndChild();
		if (ui.Button("Copy Details")) {
			std::string report;
			for (const auto &issue : checkIssues_) {
				AppendIssueDetails(report, issue);
			}
			ImGui::SetClipboardText(report.c_str());
		}
		ImGui::SetItemTooltip("Copy all issues, including those hidden by search.");
		if (!issueFilter_.empty()) {
			ui.SameLine();
			ui.BeginDisabled(matching.empty());
			if (ui.Button("Copy Matching Issues")) {
				std::string report;
				for (const auto i : matching) {
					AppendIssueDetails(report, checkIssues_[i]);
				}
				ImGui::SetClipboardText(report.c_str());
			}
			ImGui::SetItemTooltip("Copy matching issues from all pages.");
			ui.EndDisabled();
		}
	} else if (!checkResult_.empty() && !checkResultStale_) {
		ImGui::TextWrapped("%s", checkResult_.c_str());
	}

	const bool exportRequested = ui.Button("Export");
	ui.SameLine();
	const bool checkRequested = ui.Button("Check Export") || recheckIssue;
	bool saveAndCheckRequested = false;
	if (canSaveAndCheck) {
		ui.SameLine();
		saveAndCheckRequested = ui.Button("Save Scene and Check");
	}
	const bool navigatingIssue = result == GameExportResult::OPEN_ISSUE ||
	                             result == GameExportResult::OPEN_TRANSITION_SOURCE ||
	                             result == GameExportResult::EDIT_ISSUE_SCRIPT ||
	                             result == GameExportResult::EDIT_ISSUE_INPUT ||
	                             result == GameExportResult::REVEAL_ISSUE_ASSET;
	ui.SameLine();
	const bool keepDraft = ui.Button("Keep Draft and Close");
	ImGui::SetItemTooltip(
	    "Restore these settings when reopening Export Game for the same scene. "
	    "Saved project scenes also keep the draft after restarting the Editor.");
	if (chooseStartup || exportRequested || checkRequested || saveAndCheckRequested ||
	    navigatingIssue || keepDraft) {
		error_.clear();
		try {
			auto selected = options_;
			try {
				selected.destination = ReadDestination(output_);
			} catch (const std::invalid_argument &) {
				if (!chooseStartup && !navigatingIssue && !keepDraft) {
					focusField_ = GameExportField::OUTPUT_FOLDER;
					throw;
				}
				// 出力先の入力が無効でも、下書きの保持や修正箇所への移動を許可する。
				selected.destination.clear();
			}
			selected.config.title = title_;
			selected.config.width = width_;
			selected.config.height = height_;

			bool outputOccupied = false;
			if (exportRequested) {
				if (!selected.config.HasValidWindowTitle()) {
					focusField_ = GameExportField::TITLE;
					throw std::runtime_error("Enter a valid game title.");
				}
				if (!GameConfig::IsValidWindowDimension(selected.config.width)) {
					focusField_ = GameExportField::WIDTH;
					throw std::runtime_error("Window width must be between 1 and 8192.");
				}
				if (!GameConfig::IsValidWindowDimension(selected.config.height)) {
					focusField_ = GameExportField::HEIGHT;
					throw std::runtime_error("Window height must be between 1 and 8192.");
				}

				// 出力先の検査で失敗した場合は入力欄に戻す。
				focusField_ = GameExportField::OUTPUT_FOLDER;
				if (!selected.destination.is_absolute() ||
				    !PathUtils::IsValidFileName(selected.destination.filename())) {
					throw std::runtime_error("Choose a new output folder.");
				}
				outputOccupied = std::filesystem::exists(
				    std::filesystem::symlink_status(selected.destination));
				if (outputOccupied) {
					const std::string message =
					    "That output path already exists. Choose a new folder.";
					issueFilter_.clear();
					SetCheckResult(message, {{.message = message,
					                          .locationPath = selected.destination,
					                          .field = GameExportField::OUTPUT_FOLDER}});
				} else {
					focusField_ = GameExportField::NONE;
				}
			}

			if (!outputOccupied) {
				options_ = std::move(selected);
				result = keepDraft              ? GameExportResult::CANCELLED
				         : chooseStartup         ? GameExportResult::CHOOSE_STARTUP_SCENE
				         : exportRequested      ? GameExportResult::ACCEPTED
				         : saveAndCheckRequested ? GameExportResult::SAVE_AND_CHECK
				         : checkRequested        ? GameExportResult::CHECK
				                                 : result;
			}
		} catch (const std::exception &e) {
			error_ = e.what();
		}
	}

	ui.SameLine();

	if (ui.Button("Cancel") ||
	    (!wasBrowsing && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
		result = GameExportResult::CANCELLED;
	}

	ImGui::SetItemDefaultFocus();
	ui.EndDisabled();

	if (browsing_ && result == GameExportResult::NONE) {
		const auto folderResult = folderDialog_.Draw(ui);
		if (folderResult != FileDialogResult::NONE) {
			browsing_ = false;
		}
		if (folderResult == FileDialogResult::ACCEPTED) {
			try {
				const auto candidate =
				    folderDialog_.GetSelectedPath() / browseOutputName_;
				auto output = PathUtils::ToUtf8(FindAvailableDestination(candidate));
				if (output != output_) {
					checkResultStale_ = !checkResult_.empty();
					output_ = std::move(output);
				}
				error_.clear();
			} catch (const std::exception &e) {
				error_ = e.what();
			}
		}
	}

	if (result == GameExportResult::CANCELLED ||
	    result == GameExportResult::ACCEPTED ||
	    result == GameExportResult::SAVE_AND_CHECK ||
	    result == GameExportResult::CHOOSE_STARTUP_SCENE ||
	    result == GameExportResult::OPEN_ISSUE ||
	    result == GameExportResult::OPEN_TRANSITION_SOURCE ||
	    result == GameExportResult::REVEAL_ISSUE_ASSET ||
	    navigatingIssue) {
		resumeDraft_ = keepDraft || navigatingIssue;
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}
