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
	error_.clear();
	opening_ = true;
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

	ui.InputText("Game Title", title_);
	ImGui::InputInt("Width", &width_);
	ImGui::InputInt("Height", &height_);

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

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	GameSettingsResult result = GameSettingsResult::NONE;

	const bool apply = ui.Button("Apply");
	ui.SameLine();
	const bool cancel =
	    ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

	if (cancel) {
		result = GameSettingsResult::CANCELLED;
	} else if (apply) {
		auto candidate = config_;
		candidate.title = title_;
		candidate.width = width_;
		candidate.height = height_;
		candidate.input = input_;

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

	if (result != GameSettingsResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}
