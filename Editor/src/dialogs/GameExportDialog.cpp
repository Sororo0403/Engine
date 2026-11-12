#include "dialogs/GameExportDialog.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <imgui.h>
#include <stdexcept>
#include <utility>

namespace {
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
		candidate = parent / (name + L"-" + std::to_wstring(index));
	}

	return candidate;
}

/// <summary>
/// 選択ダイアログを開けるよう、候補の親から既存のフォルダを探す
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
} // namespace

void GameExportDialog::Show(GameExportOptions options, bool includesUnsavedEdits) {
	options_ = std::move(options);
	defaultOutputName_ = options_.destination.filename();
	output_ = PathUtils::ToUtf8(options_.destination);
	const auto &scenePath = options_.startupScenePath;
	startupScene_ = scenePath.empty() ? "Untitled" : PathUtils::ToUtf8(scenePath);
	includesUnsavedEdits_ = includesUnsavedEdits;
	title_ = options_.config.title;
	width_ = options_.config.width;
	height_ = options_.config.height;
	error_.clear();
	ClearCheckResult();
	browsing_ = false;

	try {
		output_ = PathUtils::ToUtf8(FindAvailableDestination(options_.destination));
	} catch (const std::filesystem::filesystem_error &e) {
		error_ = e.what();
	}

	opening_ = true;
}

void GameExportDialog::SetCheckResult(std::string message,
                                      std::vector<GameExportIssue> issues) {
	checkResult_ = std::move(message);
	checkIssues_ = std::move(issues);
	scrollCheckResultToTop_ = true;
}

const GameExportIssue &GameExportDialog::GetSelectedIssue() const {
	return checkIssues_[selectedIssue_];
}

void GameExportDialog::ClearCheckResult() {
	checkResult_.clear();
	checkIssues_.clear();
}

void GameExportDialog::ShowExportFailure(std::string error) {
	error_ = std::move(error);
	ClearCheckResult();
	opening_ = true;
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
	ui.Text("Output Folder");
	bool changed = ui.InputText("##OutputFolder", output_);
	ui.SameLine();
	if (ui.Button("Browse...")) {
		auto output = options_.destination;
		try {
			if (!output_.empty()) {
				output = PathUtils::ResolveFromExecutable(
				    PathUtils::FromUtf8(output_));
			}
		} catch (const std::exception &) {
			// 入力途中のパスを読めない場合は初期の出力先から探す。
		}
		folderDialog_.Show(FileDialogMode::SELECT_DIRECTORY,
		                   FindExistingParent(output));
		browsing_ = true;
	}
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
	GameExportResult result = GameExportResult::NONE;
	if (!checkIssues_.empty()) {
		ui.Text("Export check failed.");
		if (scrollCheckResultToTop_) {
			ImGui::SetNextWindowScroll({-1.0f, 0.0f});
		}
		const float height = 8.0f * ImGui::GetTextLineHeightWithSpacing();
		if (ImGui::BeginChild("ExportCheckResult", {0.0f, height})) {
			for (std::size_t i = 0; i < checkIssues_.size(); ++i) {
				const auto &issue = checkIssues_[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TextWrapped("%s", issue.message.c_str());
				if ((issue.objectId != 0 || !issue.scenePath.empty()) &&
				    ui.Button(issue.objectId == 0 ? "Open Scene" : "Show Object")) {
					selectedIssue_ = i;
					result = GameExportResult::OPEN_ISSUE;
				}
				ImGui::PopID();
				ImGui::Spacing();
			}
		}
		ImGui::EndChild();
		scrollCheckResultToTop_ = false;
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
			if (output_.empty()) {
				throw std::runtime_error("Choose a new output folder.");
			}

			auto selected = options_;
			selected.destination =
			    PathUtils::ResolveFromExecutable(PathUtils::FromUtf8(output_));
			selected.config.title = title_;
			selected.config.width = width_;
			selected.config.height = height_;

			if (!selected.config.IsValid()) {
				throw std::runtime_error(
				    "Enter a title and a valid window size (1-8192).");
			}

			if (std::filesystem::exists(
			        std::filesystem::symlink_status(selected.destination))) {
				throw std::runtime_error(
				    "That output path already exists. Choose a new folder.");
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
				    folderDialog_.GetSelectedPath() / defaultOutputName_;
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
	    result == GameExportResult::OPEN_ISSUE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}
