#include "dialogs/GameExportDialog.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include <algorithm>
#include <imgui.h>
#include <stdexcept>
#include <utility>

namespace {
/// <summary>
/// 出力欄の入力を読み取り、通常の相対パスは実行ファイルを基準にする
/// </summary>
std::filesystem::path ReadDestination(const std::string &input) {
	auto path = PathUtils::FromUtf8(input);
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
	auto candidate = destination;
	const auto parent = destination.parent_path();
	const auto name = destination.filename().native();

	for (std::size_t index = 2;
	     std::filesystem::exists(std::filesystem::symlink_status(candidate));
	     ++index) {
		const auto numbered = name + L"-" + std::to_wstring(index);
		if (!PathUtils::IsValidFileName(numbered)) {
			throw std::runtime_error("The suggested output folder name is too long or invalid.");
		}
		candidate = parent / numbered;
	}

	return candidate;
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
/// 問題の素材をExplorerで選択する。素材がなければ既存の親フォルダを開く
/// </summary>
void ShowAssetInExplorer(const std::filesystem::path &path, std::string &error) {
	if (!path.is_absolute() || path.native().find(L'\0') != std::wstring::npos) {
		error = "The asset path is invalid.";
		return;
	}

	std::error_code statusError;
	if (std::filesystem::exists(path, statusError)) {
		ShellUtils::RevealInExplorer(path, error);
		return;
	}
	if (statusError && statusError != std::errc::no_such_file_or_directory &&
	    statusError != std::errc::not_a_directory) {
		error = "Could not check the asset location: " + statusError.message();
		return;
	}

	const auto parent = FindExistingParent(path.lexically_normal());
	if (parent.empty()) {
		error = "No existing folder was found for this asset.";
		return;
	}
	ShellUtils::OpenDirectory(parent, error);
}
} // namespace

void GameExportDialog::Show(GameExportOptions options, bool includesUnsavedEdits,
                            const std::filesystem::path &projectRoot) {
	const bool resume = std::exchange(resumeDraft_, false) &&
	                    projectRoot_ == projectRoot &&
	                    options_.startupScenePath == options.startupScenePath;
	projectRoot_ = projectRoot;
	options_ = std::move(options);
	defaultOutputName_ = options_.destination.filename();
	if (!PathUtils::IsValidFileName(defaultOutputName_)) {
		defaultOutputName_ = L"Game";
	}
	UpdateStartupScene(options_.startupScenePath, includesUnsavedEdits);
	error_.clear();
	browsing_ = false;

	if (!resume) {
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
}

void GameExportDialog::ForgetUntitledDraft() {
	if (options_.startupScenePath.empty()) {
		resumeDraft_ = false;
	}
}

void GameExportDialog::UpdateStartupScene(const std::filesystem::path &path,
                                          bool includesUnsavedEdits) {
	startupScene_ = path.empty() ? "Untitled" : PathUtils::ToUtf8(path);
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
	if (!issues.empty()) {
		issueToScrollTo_ = continueFromIssue
		                       ? (std::min)(selectedIssue_, issues.size() - 1)
		                       : 0;
	}
	checkResult_ = std::move(message);
	checkIssues_ = std::move(issues);
}

const GameExportIssue &GameExportDialog::GetSelectedIssue() const {
	return checkIssues_[selectedIssue_];
}

void GameExportDialog::ClearCheckResult() {
	checkResult_.clear();
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

GameExportResult GameExportDialog::Draw(const ImGuiSystem &ui) {
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
			}
			changed = true;
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
		ClearCheckResult();
	}

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}
	GameExportResult result = chooseStartup ? GameExportResult::CHOOSE_STARTUP_SCENE
	                                        : GameExportResult::NONE;
	if (!checkIssues_.empty()) {
		ui.Text("Export issues");
		if (issueToScrollTo_ == 0) {
			ImGui::SetNextWindowScroll({-1.0f, 0.0f});
		}
		const float height = 8.0f * ImGui::GetTextLineHeightWithSpacing();
		if (ImGui::BeginChild("ExportCheckResult", {0.0f, height})) {
			for (std::size_t i = 0; i < checkIssues_.size(); ++i) {
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
				if (!issue.scriptPath.empty()) {
					if (hasObject)
						ui.SameLine();
					if (ui.Button("Edit Script")) {
						selectedIssue_ = i;
						result = GameExportResult::EDIT_ISSUE_SCRIPT;
					}
				}
				if (!issue.assetPath.empty()) {
					if (hasObject || !issue.scriptPath.empty())
						ui.SameLine();
					if (ui.Button("Show in Explorer"))
						ShowAssetInExplorer(issue.assetPath, error_);
					ImGui::SetItemTooltip("%s",
					                      PathUtils::ToUtf8(issue.assetPath).c_str());
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
	} else if (!checkResult_.empty()) {
		ImGui::TextWrapped("%s", checkResult_.c_str());
	}

	const bool exportRequested = ui.Button("Export");
	ui.SameLine();
	const bool checkRequested = ui.Button("Check Export");
	if (exportRequested || checkRequested) {
		error_.clear();
		ClearCheckResult();
		try {
			auto selected = options_;
			selected.destination = ReadDestination(output_);
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
			result = exportRequested ? GameExportResult::ACCEPTED
			                         : GameExportResult::CHECK;
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
				output_ = PathUtils::ToUtf8(FindAvailableDestination(candidate));
				error_.clear();
				ClearCheckResult();
			} catch (const std::exception &e) {
				error_ = e.what();
			}
		}
	}

	if (result == GameExportResult::CANCELLED ||
	    result == GameExportResult::ACCEPTED ||
	    result == GameExportResult::CHOOSE_STARTUP_SCENE ||
	    result == GameExportResult::OPEN_ISSUE ||
	    result == GameExportResult::EDIT_ISSUE_SCRIPT) {
		resumeDraft_ = result == GameExportResult::OPEN_ISSUE ||
		               result == GameExportResult::EDIT_ISSUE_SCRIPT;
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}
