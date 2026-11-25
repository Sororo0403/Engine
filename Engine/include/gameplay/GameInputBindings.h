#pragma once

#include "input/Key.h"
#include "input/GamepadButton.h"
#include "input/MouseButton.h"
#include <array>
#include <optional>
#include <string_view>

/// <summary>
/// 移動・ジャンプ・操作に割り当てるキー。EditorのPlayと出力ゲームで共有する
/// </summary>
struct GameInputBindings {
	Key forward = Key::W;
	Key backward = Key::S;
	Key left = Key::A;
	Key right = Key::D;
	Key jump = Key::SPACE;
	Key interact = Key::E;
	bool arrowMovement = false;
	enum class MoveStick { NONE, LEFT, RIGHT };
	MoveStick moveStick = MoveStick::LEFT;
	bool dpadMovement = true;
	float leftStickDeadZone = .15f;
	float rightStickDeadZone = .15f;
	float axisPressThreshold = .6f;
	float axisReleaseThreshold = .4f;
	std::optional<GamepadButton> gamepadJump = GamepadButton::SOUTH;
	std::optional<GamepadButton> gamepadInteract = GamepadButton::WEST;

	struct Camera {
		MoveStick stick = MoveStick::RIGHT;
		std::optional<MouseButton> rotate = MouseButton::RIGHT;
		std::optional<MouseButton> mouseReset = MouseButton::MIDDLE;
		std::optional<GamepadButton> padReset = GamepadButton::RIGHT_STICK;
		bool wheelZoom = true;
		bool operator==(const Camera &) const = default;
	} camera;

	struct UI {
		enum Action : std::size_t { LEFT, RIGHT, UP, DOWN, SUBMIT, COUNT };
		std::array<std::optional<Key>, COUNT> keys{
		    Key::LEFT, Key::RIGHT, Key::UP, Key::DOWN, Key::ENTER};
		std::array<std::optional<GamepadButton>, COUNT> buttons{
		    GamepadButton::DPAD_LEFT, GamepadButton::DPAD_RIGHT,
		    GamepadButton::DPAD_UP, GamepadButton::DPAD_DOWN, GamepadButton::SOUTH};
		std::optional<Key> next = Key::TAB;
		std::optional<Key> alternateSubmit = Key::SPACE;
		std::optional<MouseButton> pointer = MouseButton::LEFT;
		bool capturesGameplay = true;
		enum class ReverseModifier { NONE, SHIFT, CTRL, ALT };
		ReverseModifier reverseModifier = ReverseModifier::SHIFT;
		static constexpr std::array<std::string_view, 4> kReverseNames{"None", "Shift", "Ctrl", "Alt"};
		bool operator==(const UI &) const = default;
	} ui;

	/// <summary>
	/// 設定ファイルで使用するキー名と共通キーの対応
	/// </summary>
	struct Option {
		Key key;
		std::string_view name;
	};

	/// <summary>
	/// 設定ファイルで使用する操作名と割り当て先の対応
	/// </summary>
	struct Action {
		const char *name;
		Key GameInputBindings::*member;
	};

	static constexpr std::array<Option, 41> kOptions{{
	    {Key::A, "A"},         {Key::B, "B"},       {Key::C, "C"},
	    {Key::D, "D"},         {Key::E, "E"},       {Key::F, "F"},
	    {Key::G, "G"},         {Key::H, "H"},       {Key::I, "I"},
	    {Key::J, "J"},         {Key::K, "K"},       {Key::L, "L"},
	    {Key::M, "M"},         {Key::N, "N"},       {Key::O, "O"},
	    {Key::P, "P"},         {Key::Q, "Q"},       {Key::R, "R"},
	    {Key::S, "S"},         {Key::T, "T"},       {Key::U, "U"},
	    {Key::V, "V"},         {Key::W, "W"},       {Key::X, "X"},
	    {Key::Y, "Y"},         {Key::Z, "Z"},       {Key::DIGIT_0, "0"},
	    {Key::DIGIT_1, "1"},   {Key::DIGIT_2, "2"}, {Key::DIGIT_3, "3"},
	    {Key::DIGIT_4, "4"},   {Key::DIGIT_5, "5"}, {Key::DIGIT_6, "6"},
	    {Key::DIGIT_7, "7"},   {Key::DIGIT_8, "8"}, {Key::DIGIT_9, "9"},
	    {Key::SPACE, "Space"}, {Key::LEFT, "Left"}, {Key::RIGHT, "Right"},
	    {Key::UP, "Up"},       {Key::DOWN, "Down"},
	}};
	static constexpr std::array<Action, 6> kActions{
	    {{"forward", &GameInputBindings::forward},
	     {"backward", &GameInputBindings::backward},
	     {"left", &GameInputBindings::left},
	     {"right", &GameInputBindings::right},
	     {"jump", &GameInputBindings::jump},
	     {"interact", &GameInputBindings::interact}}};

	/// <summary>
	/// 設定ファイルで使用するキー名を取得する
	/// </summary>
	/// <param name="key">共通キー</param>
	/// <returns>対応する名前。割り当て不可のキーは空</returns>
	static constexpr std::string_view GetName(Key key) {
		for (const auto &option : kOptions) {
			if (option.key == key) {
				return option.name;
			}
		}

		return {};
	}

	/// <summary>
	/// 設定ファイルのキー名を共通キーへ変換する
	/// </summary>
	/// <param name="name">大文字・小文字を含めて一致させるキー名</param>
	/// <returns>対応するキー。未対応はnullopt</returns>
	static constexpr std::optional<Key> FromName(std::string_view name) {
		for (const auto &option : kOptions) {
			if (option.name == name) {
				return option.key;
			}
		}

		return std::nullopt;
	}

	/// <summary>
	/// 全操作が対応するキーに割り当てられ、重複していないか確認する
	/// </summary>
	/// <returns>true:有効 false:未対応のキー、または重複あり</returns>
	constexpr bool IsValid() const {
		if (!(leftStickDeadZone >= 0 && leftStickDeadZone < 1 &&
		      rightStickDeadZone >= 0 && rightStickDeadZone < 1 &&
		      axisReleaseThreshold >= 0 && axisReleaseThreshold < axisPressThreshold &&
		      axisPressThreshold <= 1))
			return false;
		const auto validStick = [](MoveStick stick) {
			return stick == MoveStick::NONE || stick == MoveStick::LEFT || stick == MoveStick::RIGHT;
		};
		const auto validKey = [](auto key) {
			return !key || !GetName(*key).empty() || *key == Key::ENTER ||
			       *key == Key::ESCAPE || *key == Key::TAB;
		};
		const auto validPad = [](auto button) { return !button || *button < GamepadButton::COUNT; };
		const auto validMouse = [](auto button) { return !button || *button < MouseButton::COUNT; };
		if (!validStick(camera.stick) || !validMouse(camera.rotate) ||
		    !validMouse(camera.mouseReset) || !validPad(camera.padReset) ||
		    !validMouse(ui.pointer) || !validKey(ui.next) || !validKey(ui.alternateSubmit) ||
		    static_cast<std::size_t>(ui.reverseModifier) >= UI::kReverseNames.size())
			return false;
		for (std::size_t index = 0; index < UI::COUNT; ++index)
			if (!validKey(ui.keys[index]) || !validPad(ui.buttons[index]))
				return false;
		if ((moveStick != MoveStick::NONE && moveStick != MoveStick::LEFT &&
		     moveStick != MoveStick::RIGHT) ||
		    (gamepadJump && *gamepadJump >= GamepadButton::COUNT) ||
		    (gamepadInteract && *gamepadInteract >= GamepadButton::COUNT))
			return false;
		for (std::size_t i = 0; i < kActions.size(); ++i) {
			const auto key = this->*kActions[i].member;

			if (GetName(key).empty()) {
				return false;
			}

			for (std::size_t j = 0; j < i; ++j) {
				if (key == this->*kActions[j].member) {
					return false;
				}
			}
		}

		return true;
	}

	/// <summary>
	/// 全操作の割り当てが一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const GameInputBindings &other) const = default;
};
