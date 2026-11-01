#include "dialogs/GameExportDialog.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <imgui.h>
#include <stdexcept>
#include <utility>

void GameExportDialog::Show(GameExportOptions options) {
	options_ = std::move(options);
	output_ = PathUtils::ToUtf8(options_.destination);
	title_ = options_.config.title;
	width_ = options_.config.width;
	height_ = options_.config.height;
	error_.clear();
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

			if (std::filesystem::exists(selected.destination)) {
				throw std::runtime_error(
				    "That folder already exists. Choose a new folder.");
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
