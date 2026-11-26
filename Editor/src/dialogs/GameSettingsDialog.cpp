#include "dialogs/GameSettingsDialog.h"

#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"

#include <algorithm>
#include <exception>
#include <imgui.h>
#include <utility>

void GameSettingsDialog::Show(const GameConfig &config,
                              const std::filesystem::path &directory,
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
	fullscreen_ = config.fullscreen;
	verticalSync_ = config.verticalSync;
	frameTimeLimit_ = config.frameTimeLimit;
	rigidBodyGravity_ = config.rigidBodyGravity;
	lighting_ = config.lighting;
	settingsId_ = config.settingsId;
	persistentScriptKeys_ = config.persistentScriptKeys;
	error_.clear();
	source_.clear();
	lastSavedPath_.clear();
	destination_ = directory / L"Game.settings.json";
	saving_ = false;
	opening_ = true;
	selecting_ = false;
}

void GameSettingsDialog::Reopen(std::string error) {
	error_ = std::move(error);
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
	ImGui::Checkbox("Borderless Fullscreen", &fullscreen_);
	ImGui::Checkbox("Vertical Sync", &verticalSync_);
	ImGui::DragFloat3("Rigid Body Gravity", rigidBodyGravity_.data(), .1f, -1000.f, 1000.f,
	                  "%.2f", ImGuiSliderFlags_AlwaysClamp);
	bool synchronizedTime = frameTimeLimit_ > 0.0f;
	if (ImGui::Checkbox("Limit Simulation Frame Time", &synchronizedTime))
		frameTimeLimit_ = synchronizedTime ? GameConfig::kMaximumFrameTimeLimit : 0.0f;
	if (synchronizedTime) {
		float milliseconds = frameTimeLimit_ * 1000.0f;
		if (ImGui::SliderFloat("Maximum Frame Time (ms)", &milliseconds, 1.0f, 100.0f,
		                       "%.1f", ImGuiSliderFlags_AlwaysClamp))
			frameTimeLimit_ = milliseconds / 1000.0f;
	}

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
	if (ui.CollapsingHeader("Gamepad Sensitivity")) {
		ImGui::SliderFloat("Left Stick Dead Zone", &input_.leftStickDeadZone, 0, .99f);
		ImGui::SliderFloat("Right Stick Dead Zone", &input_.rightStickDeadZone, 0, .99f);
		ImGui::SliderFloat("Axis Press Threshold", &input_.axisPressThreshold, .01f, 1);
		ImGui::SliderFloat("Axis Release Threshold", &input_.axisReleaseThreshold, 0, .99f);
	}
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

	const auto mouseButton = [&](const char *label, std::optional<MouseButton> &button) {
		const auto &options = GameActionBinding::kMouseOptions;
		if (ImGui::BeginCombo(label, button ? options[static_cast<std::size_t>(*button)].name : "None")) {
			if (ui.Selectable("None", !button))
				button.reset();
			for (const auto &option : options)
				if (ui.Selectable(option.name, button == option.button))
					button = option.button;
			ImGui::EndCombo();
		}
	};
	if (ImGui::CollapsingHeader("Camera Input")) {
		int cameraStick = static_cast<int>(input_.camera.stick);
		if (ImGui::Combo("Orbit Stick", &cameraStick, "None\0Left Stick\0Right Stick\0"))
			input_.camera.stick = static_cast<GameInputBindings::MoveStick>(cameraStick);
		ImGui::Checkbox("Mouse Wheel Zoom", &input_.camera.wheelZoom);
		gamepadButton("Gamepad Camera Reset", input_.camera.padReset);
		mouseButton("Orbit Drag", input_.camera.rotate);
		mouseButton("Mouse Camera Reset", input_.camera.mouseReset);
	}
	if (ImGui::CollapsingHeader("Game UI Input")) {
		const auto key = [&](const char *label, std::optional<Key> &value) {
			if (ImGui::BeginCombo(label, value ? GameActionBinding::GetKeyName(*value).data() : "None")) {
				if (ui.Selectable("None", !value))
					value.reset();
				for (const auto &option : GameInputBindings::kOptions)
					if (ui.Selectable(option.name.data(), value == option.key))
						value = option.key;
				for (const auto &option : GameActionBinding::kAdditionalKeyboardOptions)
					if (ui.Selectable(option.name.data(), value == option.key))
						value = option.key;
				ImGui::EndCombo();
			}
		};
		constexpr std::array uiLabels{"Left", "Right", "Up", "Down", "Submit"};
		for (std::size_t i = 0; i < uiLabels.size(); ++i) {
			const auto keyLabel = std::string("UI Key ") + uiLabels[i];
			const auto buttonLabel = std::string("UI Gamepad ") + uiLabels[i];
			key(keyLabel.c_str(), input_.ui.keys[i]);
			gamepadButton(buttonLabel.c_str(), input_.ui.buttons[i]);
		}
		key("UI Cycle Focus", input_.ui.next);
		int reverse = static_cast<int>(input_.ui.reverseModifier);
		if (ImGui::Combo("UI Reverse Focus Modifier", &reverse, "None\0Shift\0Ctrl\0Alt\0"))
			input_.ui.reverseModifier = static_cast<GameInputBindings::UI::ReverseModifier>(reverse);
		key("UI Alternate Submit", input_.ui.alternateSubmit);
		mouseButton("UI Pointer Button", input_.ui.pointer);
		ImGui::Checkbox("UI Captures Gameplay Input", &input_.ui.capturesGameplay);
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
	fullscreen_ = loaded.fullscreen;
	verticalSync_ = loaded.verticalSync;
	frameTimeLimit_ = loaded.frameTimeLimit;
	rigidBodyGravity_ = loaded.rigidBodyGravity;
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
	value.fullscreen = fullscreen_;
	value.verticalSync = verticalSync_;
	value.frameTimeLimit = frameTimeLimit_;
	value.rigidBodyGravity = rigidBodyGravity_;
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
