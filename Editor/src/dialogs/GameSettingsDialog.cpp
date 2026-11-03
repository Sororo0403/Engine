#include "dialogs/GameSettingsDialog.h"

#include "graphics/ui/ImGuiSystem.h"

#include <imgui.h>
#include <utility>

void GameSettingsDialog::Show(const GameConfig &config,
                              const std::filesystem::path &directory) {
	config_ = config;
	title_ = config.title;
	width_ = config.width;
	height_ = config.height;
	input_ = config.input;
	cameraRelativeMovement_ = config.cameraRelativeMovement;
	lighting_ = config.lighting;
	error_.clear();
	source_.clear();
	destination_ = directory / L"Game.settings.json";
	saving_ = false;
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
		saving_ = false;
		picker_.Show(FileDialogMode::OPEN, source_,
		             FileDialogFilter::GAME_CONFIG);
		selecting_ = true;
	}
	ui.EndDisabled();
	ui.BeginDisabled(selecting_);
	const bool saveConfiguration = ui.Button("Save Game Configuration");
	ui.EndDisabled();

	ui.BeginDisabled(selecting_);
	ui.InputText("Game Title", title_);
	ImGui::InputInt("Width", &width_);
	ImGui::InputInt("Height", &height_);

	ImGui::Checkbox("Camera Relative Movement", &cameraRelativeMovement_);

	ImGui::SeparatorText("Lighting");
	ImGui::InputFloat3("Light Direction", &lighting_.direction.x);
	ImGui::ColorEdit3("Light Color", &lighting_.color.x);
	ImGui::SliderFloat("Light Intensity", &lighting_.intensity, 0.0f,
	                   LightingSettings::kMaxIntensity);
	ImGui::SliderFloat("Ambient Light", &lighting_.ambient, 0.0f, 1.0f);
	if (ui.Button("Reset Lighting")) {
		lighting_ = {};
	}

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

	if (saveConfiguration) {
		if (BuildConfiguration(pendingSave_)) {
			picker_.Show(FileDialogMode::SAVE,
			             destination_.empty()
			                 ? std::filesystem::path(L"Game.settings.json")
			                 : destination_,
			             FileDialogFilter::GAME_CONFIG);
			selecting_ = true;
			saving_ = true;
		}
	}

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
		GameConfig candidate;
		if (BuildConfiguration(candidate)) {
			config_ = std::move(candidate);
			result = GameSettingsResult::ACCEPTED;
		}
	}

	ImGui::SetItemDefaultFocus();

	if (selecting_) {
		const auto selected = picker_.Draw(ui);
		if (selected == FileDialogResult::ACCEPTED) {
			if (saving_) {
				destination_ = picker_.GetSelectedPath();
				pendingSave_.Save(destination_, error_);
			} else {
				source_ = picker_.GetSelectedPath();
				LoadConfiguration(source_);
			}
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
	lighting_ = loaded.lighting;
	return true;
}

bool GameSettingsDialog::BuildConfiguration(GameConfig &candidate) {
	auto value = config_;
	value.title = title_;
	value.width = width_;
	value.height = height_;
	value.input = input_;
	value.cameraRelativeMovement = cameraRelativeMovement_;
	value.lighting = lighting_;
	if (!value.IsValid()) {
		error_ =
		    !value.input.IsValid()
		        ? "Choose different supported keys for every gameplay action."
		    : !value.lighting.IsValid()
		        ? "Enter a nonzero finite light direction, color/ambient (0-1) "
		          "and intensity (0-16)."
		        : "Enter a title and a valid window size (1-8192).";
		return false;
	}
	candidate = std::move(value);
	error_.clear();
	return true;
}
