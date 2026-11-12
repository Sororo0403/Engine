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
} // namespace

void GameExportDialog::Show(GameExportOptions options,
                            std::filesystem::path scenePath) {
	options_ = std::move(options);
	output_ = PathUtils::ToUtf8(options_.destination);
	startupScene_ = scenePath.empty() ? "Untitled" : PathUtils::ToUtf8(scenePath);
	title_ = options_.config.title;
	width_ = options_.config.width;
	height_ = options_.config.height;
	error_.clear();

	try {
		output_ = PathUtils::ToUtf8(FindAvailableDestination(options_.destination));
	} catch (const std::filesystem::filesystem_error &e) {
		error_ = e.what();
	}

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
	ui.InputText("Output Folder", output_);
	ui.InputText("Game Title", title_);
	ImGui::InputInt("Width", &width_);
	ImGui::InputInt("Height", &height_);

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	GameExportResult result = GameExportResult::NONE;

	if (ui.Button("Export")) {
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
			result = GameExportResult::ACCEPTED;
		} catch (const std::exception &e) {
			error_ = e.what();
		}
	}

	ui.SameLine();

	if (ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		result = GameExportResult::CANCELLED;
	}

	ImGui::SetItemDefaultFocus();

	if (result != GameExportResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}
