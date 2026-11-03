#include "dialogs/GameSettingsDialog.h"

#include "graphics/ui/ImGuiSystem.h"

#include <imgui.h>
#include <utility>

void GameSettingsDialog::Show(const GameConfig &config) {
	config_ = config;
	title_ = config.title;
	width_ = config.width;
	height_ = config.height;
	input_ = config.input;
	cameraRelativeMovement_ = config.cameraRelativeMovement;
	error_.clear();
	source_.clear();
	opening_ = true;
	selecting_ = false;
}

GameSettingsResult GameSettingsDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *title = "Game Settings###GameSettingsDialog";

	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}

	ImGui::SetNextWindowSize({480, 0}, ImGuiCond_Appearing);

	if (!ui.BeginPopupModal(title)) {
		return GameSettingsResult::NONE;
	}

	ui.BeginDisabled(selecting_);
	if (ui.Button("Load Game Configuration")) {
		picker_.Show(FileDialogMode::OPEN, source_,
		             FileDialogFilter::GAME_CONFIG);
		selecting_ = true;
	}
	ui.EndDisabled();

	ui.BeginDisabled(selecting_);
	ui.InputText("Game Title", title_);
	ImGui::InputInt("Width", &width_);
	ImGui::InputInt("Height", &height_);

	ImGui::Checkbox("Camera Relative Movement", &cameraRelativeMovement_);

	ImGui::SeparatorText("Gameplay Keys");
	const std::array<const char *, 5> labels{"Move Forward", "Move Backward",
	                                         "Move Left", "Move Right", "Jump"};

	for (std::size_t i = 0; i < GameInputBindings::kActions.size(); ++i) {
		auto &key = input_.*GameInputBindings::kActions[i].member;

		if (ImGui::BeginCombo(labels[i],
		                      GameInputBindings::GetName(key).data())) {
			for (const auto &option : GameInputBindings::kOptions) {
				if (ui.Selectable(option.name.data(), key == option.key)) {
					key = option.key;
				}
			}

			ImGui::EndCombo();
		}
	}

	if (ui.Button("Reset Gameplay Keys")) {
		input_ = {};
	}
	ui.EndDisabled();

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	GameSettingsResult result = GameSettingsResult::NONE;

	ui.BeginDisabled(selecting_);
	const bool apply = ui.Button("Apply");
	ui.SameLine();
	const bool cancel =
	    ui.Button("Cancel") ||
	    (!selecting_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false));
	ui.EndDisabled();

	if (cancel) {
		result = GameSettingsResult::CANCELLED;
	} else if (apply) {
		auto candidate = config_;
		candidate.title = title_;
		candidate.width = width_;
		candidate.height = height_;
		candidate.input = input_;
		candidate.cameraRelativeMovement = cameraRelativeMovement_;

		if (candidate.IsValid()) {
			config_ = std::move(candidate);
			result = GameSettingsResult::ACCEPTED;
		} else {
			error_ = !candidate.input.IsValid()
			             ? "Choose different supported keys for every gameplay "
			               "action."
			             : "Enter a title and a valid window size (1-8192).";
		}
	}

	ImGui::SetItemDefaultFocus();

	if (selecting_) {
		const auto selected = picker_.Draw(ui);
		if (selected == FileDialogResult::ACCEPTED) {
			source_ = picker_.GetSelectedPath();
			LoadConfiguration(source_);
		}
		if (selected != FileDialogResult::NONE) {
			selecting_ = false;
		}
	}

	if (result != GameSettingsResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}

bool GameSettingsDialog::LoadConfiguration(const std::filesystem::path &path) {
	GameConfig loaded;
	if (!loaded.Load(path, error_)) {
		return false;
	}

	title_ = loaded.title;
	width_ = loaded.width;
	height_ = loaded.height;
	input_ = loaded.input;
	cameraRelativeMovement_ = loaded.cameraRelativeMovement;
	return true;
}
