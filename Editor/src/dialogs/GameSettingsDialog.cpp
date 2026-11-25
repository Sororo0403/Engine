#include "dialogs/GameSettingsDialog.h"

#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"

#include <algorithm>
#include <exception>
#include <imgui.h>
#include <utility>

void GameSettingsDialog::Show(const GameConfig &config,
                              const std::filesystem::path &directory,
                              const std::filesystem::path &configuration,
                              std::string inputAction) {
	config_ = config;
	title_ = config.title;
	width_ = config.width;
	height_ = config.height;
	input_ = config.input;
	actions_ = config.actions;
	inputAction_ = std::move(inputAction);
	focusInputAction_ = !inputAction_.empty();
	cameraRelativeMovement_ = config.cameraRelativeMovement;
	gamepadEnabled_ = config.gamepadEnabled;
	lighting_ = config.lighting;
	settingsId_ = config.settingsId;
	persistentScriptKeys_ = config.persistentScriptKeys;
	error_.clear();
	failedProjectFile_.clear();
	locationError_.clear();
	source_ = configuration;
	lastSavedPath_.clear();
	destination_ = directory / L"Game.settings.json";
	saving_ = false;
	opening_ = true;
	selecting_ = false;

	if (!source_.empty()) {
		try {
			source_ = PathUtils::ResolveFromExecutable(source_);
			destination_ = source_;
			LoadConfiguration(source_);
		} catch (const std::exception &error) {
			error_ = error.what();
		}
	}
}

void GameSettingsDialog::Reopen(std::string error, std::filesystem::path projectFile) {
	error_ = std::move(error);
	failedProjectFile_ = std::move(projectFile);
	locationError_.clear();
	opening_ = true;
}

GameSettingsResult GameSettingsDialog::Draw(const ImGuiSystem &ui, bool retryPlay,
                                            bool canPrepareExport) {
	constexpr const char *title = "Game Settings###GameSettingsDialog";

	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}

	ImGui::SetNextWindowSize({560, 0}, ImGuiCond_Appearing);

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
	ImGui::BeginChild(
	    "GameSettingsFields",
	    {0, (std::max)(120.0f,
	                   (std::min)(550.0f, ImGui::GetMainViewport()->WorkSize.y -
	                                          200.0f))});
	ImGui::SeparatorText("Game");
	ui.InputText("Game Title", title_);
	ImGui::InputInt("Width", &width_);
	ImGui::InputInt("Height", &height_);

	ImGui::Checkbox("Camera Relative Movement", &cameraRelativeMovement_);

	ImGui::SeparatorText("Lighting");
	ImGui::SliderFloat("Default Ambient Light", &lighting_.ambient, 0.0f, 1.0f,
	                   "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SeparatorText("Gameplay Input");
	ImGui::Checkbox("Enable Gamepad", &gamepadEnabled_);
	ImGui::Checkbox("Also Move with Arrow Keys", &input_.arrowMovement);
	const std::array<const char *, GameInputBindings::kActions.size()> labels{
	    "Move Forward", "Move Backward", "Move Left",
	    "Move Right",   "Jump",          "Interact"};

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

	ImGui::BeginDisabled(!gamepadEnabled_);
	int stick = static_cast<int>(input_.moveStick);
	if (ImGui::Combo("Gamepad Move Stick", &stick, "None\0Left Stick\0Right Stick\0"))
		input_.moveStick = static_cast<GameInputBindings::MoveStick>(stick);
	ImGui::Checkbox("Also Move with D-Pad", &input_.dpadMovement);
	const auto gamepadButton = [&](const char *label, std::optional<GamepadButton> &button) {
		const auto &options = GameActionBinding::kGamepadOptions;
		if (ImGui::BeginCombo(label, button ? options[static_cast<std::size_t>(*button)].name : "None")) {
			if (ui.Selectable("None", !button))
				button.reset();
			for (const auto &option : options)
				if (ui.Selectable(option.name, button == option.button))
					button = option.button;
			ImGui::EndCombo();
		}
	};
	gamepadButton("Gamepad Jump", input_.gamepadJump);
	gamepadButton("Gamepad Interact", input_.gamepadInteract);
	ImGui::EndDisabled();

	if (ui.Button("Reset Gameplay Input")) {
		input_ = {};
	}

	ImGui::SeparatorText("Named Input Actions");
	const bool focusInputAction = std::exchange(focusInputAction_, false);
	if (focusInputAction) {
		ImGui::SetScrollHereY(0.0f);
	}
	if (!inputAction_.empty() &&
	    std::ranges::find(actions_, inputAction_, &GameActionBinding::name) == actions_.end()) {
		ImGui::TextWrapped("Missing action: %s", inputAction_.c_str());
		const bool canAdd = actions_.size() < GameActionBinding::kMaximumCount &&
		                    GameActionBinding::IsValidName(inputAction_);
		ui.BeginDisabled(!canAdd);
		if (ui.Button("Add Missing Action")) {
			actions_.push_back({inputAction_});
			focusInputAction_ = true;
		}
		ui.EndDisabled();
	}

	if (ImGui::BeginChild("NamedActions", {0, 200}, ImGuiChildFlags_Borders)) {
		for (std::size_t i = 0; i < actions_.size();) {
			ImGui::PushID(static_cast<int>(i));
			auto &action = actions_[i];
			if (!inputAction_.empty() && action.name == inputAction_) {
				ui.Text("Action reported by check");
				if (focusInputAction) {
					ImGui::SetScrollHereY(0.0f);
				}
			}
			ui.InputText("Name", action.name);
			if (ImGui::BeginCombo(
			        "Keyboard",
			        action.keyboard
			            ? GameActionBinding::GetKeyName(*action.keyboard).data()
			            : "None")) {
				if (ui.Selectable("None", !action.keyboard))
					action.keyboard.reset();
				for (const auto &option : GameActionBinding::kAdditionalKeyboardOptions) {
					if (ui.Selectable(option.name.data(),
					                  action.keyboard == option.key))
						action.keyboard = option.key;
				}
				for (const auto &option : GameInputBindings::kOptions) {
					if (ui.Selectable(option.name.data(),
					                  action.keyboard == option.key))
						action.keyboard = option.key;
				}

				ImGui::EndCombo();
			}
			const auto buttons = [&](const char *label, auto &selected,
			                         const auto &options) {
				const char *preview = "None";

				for (const auto &option : options) {
					if (selected == option.button)
						preview = option.name;
				}

				if (ImGui::BeginCombo(label, preview)) {
					if (ui.Selectable("None", !selected))
						selected.reset();
					for (const auto &option : options) {
						if (ui.Selectable(option.name,
						                  selected == option.button))
							selected = option.button;
					}

					ImGui::EndCombo();
				}
			};
			buttons("Mouse", action.mouse, GameActionBinding::kMouseOptions);
			buttons("Gamepad", action.gamepad,
			        GameActionBinding::kGamepadOptions);
			buttons("Gamepad Axis", action.gamepadAxis,
			        GameActionBinding::kGamepadAxisOptions);
			const bool remove = ui.Button("Remove Action");
			ImGui::Separator();
			ImGui::PopID();
			if (remove)
				actions_.erase(actions_.begin() +
				               static_cast<std::ptrdiff_t>(i));
			else
				++i;
		}
	}

	ImGui::EndChild();
	ui.BeginDisabled(actions_.size() >= GameActionBinding::kMaximumCount);

	if (ui.Button("Add Action")) {
		for (std::size_t suffix = 1;; ++suffix) {
			const auto name = "action" + std::to_string(suffix);

			if (std::none_of(actions_.begin(), actions_.end(),
			                 [&](const auto &a) { return a.name == name; })) {
				actions_.push_back({name});
				break;
			}
		}
	}

	ui.EndDisabled();
	ImGui::EndChild();
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
	const auto hasKey = [](const auto &keys, const auto &key) {
		return std::ranges::find(keys, key) != keys.end();
	};
	const auto removed = [&](const auto &key) { return !hasKey(persistentScriptKeys_, key); };
	const auto added = [&](const auto &key) { return !hasKey(config_.persistentScriptKeys, key); };
	const bool keysChanged = std::ranges::any_of(config_.persistentScriptKeys, removed) ||
	                         std::ranges::any_of(persistentScriptKeys_, added);
	if (settingsId_ != config_.settingsId || keysChanged) {
		ImGui::TextWrapped("The loaded configuration changes the script save settings.");
		if (settingsId_ != config_.settingsId) {
			ImGui::TextWrapped("Save data ID: %s -> %s",
			                   config_.settingsId.empty() ? "Disabled" : config_.settingsId.c_str(),
			                   settingsId_.empty() ? "Disabled" : settingsId_.c_str());
		}
		if (keysChanged && ui.CollapsingHeader("Saved Script Variables")) {
			if (ui.BeginChild("ScriptSaveChanges", 0.f, ImGui::GetTextLineHeightWithSpacing() * 4)) {
				for (const auto &key : config_.persistentScriptKeys) {
					if (removed(key)) {
						ImGui::BulletText("Remove from save: %s", key.c_str());
					}
				}
				for (const auto &key : persistentScriptKeys_) {
					if (added(key)) {
						ImGui::BulletText("Add to save: %s", key.c_str());
					}
				}
			}
			ui.EndChild();
		}
		if (ui.Button("Keep Current Script Save Settings")) {
			settingsId_ = config_.settingsId;
			persistentScriptKeys_ = config_.persistentScriptKeys;
		}
	}
	if (!failedProjectFile_.empty() && ui.Button("Show Project Location")) {
		ShellUtils::RevealLocationInExplorer(failedProjectFile_, locationError_);
	}
	if (!locationError_.empty()) {
		ImGui::TextWrapped("%s", locationError_.c_str());
	}
	const bool apply = ui.Button(retryPlay ? "Apply and Retry Play" : "Apply");
	ui.SameLine();
	bool prepareExport = false;
	if (canPrepareExport) {
		prepareExport = ui.Button("Apply and Prepare Export");
		ui.SameLine();
	}
	const bool cancel =
	    ui.Button("Cancel") ||
	    (!selecting_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false));
	ui.EndDisabled();

	if (cancel) {
		result = GameSettingsResult::CANCELLED;
	} else if (apply || prepareExport) {
		GameConfig candidate;

		if (BuildConfiguration(candidate)) {
			config_ = std::move(candidate);
			result = prepareExport ? GameSettingsResult::PREPARE_EXPORT
			                       : GameSettingsResult::ACCEPTED;
		}
	}

	ImGui::SetItemDefaultFocus();

	if (selecting_) {
		const auto selected = picker_.Draw(ui);
		bool retrySelection = false;

		if (selected == FileDialogResult::ACCEPTED) {
			if (saving_) {
				destination_ = picker_.GetSelectedPath();

				if (pendingSave_.Save(destination_, error_, picker_.IsOverwriteConfirmed())) {
					lastSavedPath_ = destination_;
				} else {
					picker_.RetrySelection(std::move(error_), destination_);
					error_.clear();
					retrySelection = true;
				}
			} else {
				const auto path = picker_.GetSelectedPath();
				if (LoadConfiguration(path)) {
					source_ = path;
				} else {
					picker_.RetrySelection(std::move(error_), path);
					error_.clear();
					retrySelection = true;
				}
			}
		}

		if (selected != FileDialogResult::NONE && !retrySelection) {
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
	actions_ = loaded.actions;
	cameraRelativeMovement_ = loaded.cameraRelativeMovement;
	gamepadEnabled_ = loaded.gamepadEnabled;
	lighting_ = loaded.lighting;
	settingsId_ = std::move(loaded.settingsId);
	persistentScriptKeys_ = std::move(loaded.persistentScriptKeys);
	return true;
}

bool GameSettingsDialog::BuildConfiguration(GameConfig &candidate) {
	auto value = config_;
	value.title = title_;
	value.width = width_;
	value.height = height_;
	value.input = input_;
	value.actions = actions_;
	value.cameraRelativeMovement = cameraRelativeMovement_;
	value.gamepadEnabled = gamepadEnabled_;
	value.lighting = lighting_;
	value.settingsId = settingsId_;
	value.persistentScriptKeys = persistentScriptKeys_;

	if (!value.IsValid()) {
		error_ =
		    !GameActionBinding::IsValid(value.actions)
		        ? "Use unique action names (1-64 UTF-8 bytes) and supported "
		          "bindings (up to 32 actions)."
		    : !value.input.IsValid()
		        ? "Choose different supported keys for every gameplay action."
		    : !value.lighting.IsValid()
		        ? "Ambient light must be between 0 and 1, and the saved legacy "
		          "lighting settings must be valid."
		        : "Enter a title and a valid window size (1-8192).";
		return false;
	}

	candidate = std::move(value);
	error_.clear();
	return true;
}
