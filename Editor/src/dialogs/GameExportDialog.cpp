#include "dialogs/GameExportDialog.h"
#include "assets/AssetFolders.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include <algorithm>
#include <format>
#include <imgui.h>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
/// <summary>
/// 出力欄の入力を読み取り、通常の相対パスは実行ファイルを基準にする
/// </summary>
std::filesystem::path ReadDestination(const std::string &input) {
	auto path = PathUtils::FromPathInput(input);
	if (!path.empty() && !path.has_root_path()) {
		path = PathUtils::ResolveFromExecutable(path);
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
/// 検索語が問題の説明または参照先に含まれるか確認する
/// </summary>
bool MatchesIssue(const GameExportIssue &issue, std::string_view filter) {
	return filter.empty() ||
	       issue.message.find(filter) != std::string::npos ||
	       PathUtils::ToUtf8(issue.scenePath).find(filter) != std::string::npos ||
	       PathUtils::ToUtf8(issue.scriptPath).find(filter) != std::string::npos ||
	       PathUtils::ToUtf8(issue.assetPath).find(filter) != std::string::npos;
}

} // namespace

bool GameExportDialog::Show(GameExportOptions options, bool includesUnsavedEdits,
                            const std::filesystem::path &projectRoot) {
	const bool resume = std::exchange(resumeDraft_, false) &&
	                    projectRoot_ == projectRoot &&
	                    options_.startupScenePath == options.startupScenePath;
	const bool recheck = resume && !checkResult_.empty();
	if (resume) {
		options.destination = options_.destination;
		options.config.title = title_;
		options.config.width = width_;
		options.config.height = height_;
	}
	projectRoot_ = projectRoot;
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

void GameExportDialog::SetCheckResult(std::string message,
                                      std::vector<GameExportIssue> issues,
                                      bool continueFromIssue) {
	issueToScrollTo_.reset();
	if (!issues.empty() && continueFromIssue && selectedIssue_ < checkIssues_.size()) {
		const auto &previous = checkIssues_[selectedIssue_];
		const auto sameIssue = std::ranges::find_if(issues, [&](const auto &issue) {
			return issue.objectId == previous.objectId &&
			       issue.scenePath == previous.scenePath &&
			       issue.scriptPath == previous.scriptPath &&
			       issue.assetPath == previous.assetPath &&
			       issue.message == previous.message;
		});
		if (sameIssue != issues.end()) {
			issueToScrollTo_ = static_cast<std::size_t>(sameIssue - issues.begin());
		} else {
			// 修正した問題が消えたときは、検索結果に残る最も近い行へ戻る。
			const auto matches = [&](const auto &issue) {
				return MatchesIssue(issue, issueFilter_);
			};
			auto remaining = std::count_if(checkIssues_.begin(),
			                               checkIssues_.begin() + selectedIssue_, matches);
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
	if (!issues.empty() && !issueToScrollTo_) {
		issueToScrollTo_ = 0;
	}
	checkResult_ = std::move(message);
	checkResultStale_ = false;
	checkIssues_ = std::move(issues);
}

const GameExportIssue &GameExportDialog::GetSelectedIssue() const {
	return checkIssues_[selectedIssue_];
}

void GameExportDialog::ClearCheckResult() {
	checkResult_.clear();
	checkResultStale_ = false;
	checkIssues_.clear();
	issueToScrollTo_.reset();
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
	bool changed = ui.InputText("##OutputFolder", output_);
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
		try {
			const auto output = ReadDestination(output_);
			if (!output.is_absolute() ||
			    !PathUtils::IsValidFileName(output.filename())) {
				throw std::runtime_error("Enter a valid output folder name.");
			}
			const auto suggested = FindAvailableDestination(output);
			if (suggested != output) {
				output_ = PathUtils::ToUtf8(suggested);
				changed = true;
			}
			error_.clear();
		} catch (const std::exception &e) {
			error_ = e.what();
		}
	}
	ui.EndDisabled();
	changed |= ui.InputText("Game Title", title_);
	changed |= ImGui::InputInt("Width", &width_);
	changed |= ImGui::InputInt("Height", &height_);
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
	if (checkResultStale_) {
		ui.Text("Settings changed. Run Check Export to update the results.");
	}
	if (!checkIssues_.empty()) {
		const auto matches = [&](const GameExportIssue &issue) {
			return MatchesIssue(issue, issueFilter_);
		};
		if (ui.InputText("Search Issues", issueFilter_)) {
			issueToScrollTo_ = 0;
		}
		const auto matching = std::ranges::count_if(checkIssues_, matches);
		ui.Text(std::format("Matching issues: {} / {}", matching, checkIssues_.size()));
		if (issueToScrollTo_ == 0) {
			ImGui::SetNextWindowScroll({-1.0f, 0.0f});
		}
		const float height = 8.0f * ImGui::GetTextLineHeightWithSpacing();
		if (ImGui::BeginChild("ExportCheckResult", {0.0f, height})) {
			for (std::size_t i = 0; i < checkIssues_.size(); ++i) {
				const auto &issue = checkIssues_[i];
				if (!matches(issue)) {
					continue;
				}
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
				if (!issue.scriptPath.empty()) {
					if (hasObject)
						ui.SameLine();
					if (ui.Button("Edit Script")) {
						selectedIssue_ = i;
						result = GameExportResult::EDIT_ISSUE_SCRIPT;
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
					if (ui.Button(missing ? "Open Folder in Explorer" : "Show in Explorer"))
						ShellUtils::RevealLocationInExplorer(issue.assetPath, error_);
					ImGui::SetItemTooltip("%s", assetPath.c_str());
					ui.SameLine();
					if (ui.Button("Copy Asset Path")) {
						ImGui::SetClipboardText(assetPath.c_str());
					}
				}
				ImGui::PopID();
				ImGui::Spacing();
			}
			issueToScrollTo_.reset();
		}
		ImGui::EndChild();
		if (ui.Button("Copy Details")) {
			ImGui::SetClipboardText(checkResult_.c_str());
		}
		ImGui::SetItemTooltip("Copy all issues, including those hidden by search.");
	} else if (!checkResult_.empty() && !checkResultStale_) {
		ImGui::TextWrapped("%s", checkResult_.c_str());
	}

	const bool exportRequested = ui.Button("Export");
	ui.SameLine();
	const bool checkRequested = ui.Button("Check Export");
	bool saveAndCheckRequested = false;
	if (canSaveAndCheck) {
		ui.SameLine();
		saveAndCheckRequested = ui.Button("Save Scene and Check");
	}
	const bool navigatingIssue = result == GameExportResult::OPEN_ISSUE ||
	                             result == GameExportResult::EDIT_ISSUE_SCRIPT ||
	                             result == GameExportResult::REVEAL_ISSUE_ASSET;
	ui.SameLine();
	const bool keepDraft = ui.Button("Keep Draft and Close");
	ImGui::SetItemTooltip("Restore these settings when reopening Export Game for the same startup scene.");
	if (chooseStartup || exportRequested || checkRequested || saveAndCheckRequested ||
	    navigatingIssue || keepDraft) {
		error_.clear();
		try {
			auto selected = options_;
			try {
				selected.destination = ReadDestination(output_);
			} catch (const std::invalid_argument &) {
				if (!chooseStartup && !navigatingIssue && !keepDraft) {
					throw;
				}
				// 出力先の入力が無効でも、下書きの保持や修正箇所への移動を許可する。
				selected.destination.clear();
			}
			selected.config.title = title_;
			selected.config.width = width_;
			selected.config.height = height_;

			if (exportRequested && !selected.config.HasValidWindowSettings()) {
				throw std::runtime_error(
				    "Enter a title and a valid window size (1-8192).");
			}

			if (exportRequested) {
				if (!selected.destination.is_absolute()) {
					throw std::runtime_error("Choose a new output folder.");
				}
				if (std::filesystem::exists(
				        std::filesystem::symlink_status(selected.destination))) {
					throw std::runtime_error(
					    "That output path already exists. Choose a new folder.");
				}
			}

			options_ = std::move(selected);
			result = keepDraft              ? GameExportResult::CANCELLED
			         : chooseStartup         ? GameExportResult::CHOOSE_STARTUP_SCENE
			         : exportRequested      ? GameExportResult::ACCEPTED
			         : saveAndCheckRequested ? GameExportResult::SAVE_AND_CHECK
			         : checkRequested        ? GameExportResult::CHECK
			                                 : result;
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
	    result == GameExportResult::REVEAL_ISSUE_ASSET ||
	    result == GameExportResult::EDIT_ISSUE_SCRIPT) {
		resumeDraft_ = keepDraft || result == GameExportResult::OPEN_ISSUE ||
		               result == GameExportResult::REVEAL_ISSUE_ASSET ||
		               result == GameExportResult::EDIT_ISSUE_SCRIPT;
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}
