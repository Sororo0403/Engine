#pragma once

#include "gameplay/GameInputBindings.h"
#include "input/GamepadButton.h"
#include "input/MouseButton.h"
#include <optional>
#include <span>
#include <string>

/// <summary>
/// 名前付き操作に割り当てるキーとボタン。複数の機器は論理和で一つの操作として扱う
/// </summary>
struct GameActionBinding {
	static constexpr std::size_t kMaximumCount = 32;
	static constexpr std::size_t kMaximumNameBytes = 64;
	std::string name;
	std::optional<Key> keyboard;
	std::optional<MouseButton> mouse;
	std::optional<GamepadButton> gamepad;

	template <class T> struct Option {
		T button;
		const char *name;
	};

	static constexpr std::array<Option<MouseButton>, 5> kMouseOptions{
	    {{MouseButton::LEFT, "Left"},
	     {MouseButton::RIGHT, "Right"},
	     {MouseButton::MIDDLE, "Middle"},
	     {MouseButton::BUTTON4, "Button4"},
	     {MouseButton::BUTTON5, "Button5"}}};
	static constexpr std::array<Option<GamepadButton>, 14> kGamepadOptions{
	    {{GamepadButton::SOUTH, "South"},
	     {GamepadButton::EAST, "East"},
	     {GamepadButton::WEST, "West"},
	     {GamepadButton::NORTH, "North"},
	     {GamepadButton::DPAD_UP, "DpadUp"},
	     {GamepadButton::DPAD_DOWN, "DpadDown"},
	     {GamepadButton::DPAD_LEFT, "DpadLeft"},
	     {GamepadButton::DPAD_RIGHT, "DpadRight"},
	     {GamepadButton::LEFT_SHOULDER, "LeftShoulder"},
	     {GamepadButton::RIGHT_SHOULDER, "RightShoulder"},
	     {GamepadButton::LEFT_STICK, "LeftStick"},
	     {GamepadButton::RIGHT_STICK, "RightStick"},
	     {GamepadButton::MENU, "Menu"},
	     {GamepadButton::VIEW, "View"}}};

	/// <summary>
	/// 操作名が空でなく、64バイト以内のNULを含まないUTF-8か確認する
	/// </summary>
	/// <param name="name">大文字・小文字を区別する操作名</param>
	/// <returns>true:有効 false:空・長すぎる・無効な文字列</returns>
	static bool IsValidName(std::string_view name);

	/// <summary>
	/// 件数・名前の重複・対応機器を確認する。未割り当ての操作も保存できる
	/// </summary>
	/// <param name="bindings">ゲーム全体の操作一覧</param>
	/// <returns>true:有効 false:件数・名前・機器が無効</returns>
	static bool IsValid(std::span<const GameActionBinding> bindings);

	/// <summary>
	/// 名前とすべての割り当てが一致するか確認する
	/// </summary>
	/// <param name="other">比較する操作</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const GameActionBinding &other) const = default;
};
