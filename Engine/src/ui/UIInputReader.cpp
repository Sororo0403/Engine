#include "ui/UIInputReader.h"
#include "input/Gamepad.h"
#include "input/Keyboard.h"
#include "input/Mouse.h"
#include <algorithm>

UIInput UIInputReader::Read(const Keyboard &keyboard, const Mouse *mouse,
                            const Gamepad *gamepad, int width, int height,
                            bool enabled, bool navigationEnabled,
                            const GameInputBindings::UI &bindings) {
	UIInput result;
	result.capturesGameplay = bindings.capturesGameplay;
	if (width < 1 || height < 1 || width > 16384 || height > 16384)
		return result;
	result.width = width;
	result.height = height;
	result.enabled = enabled;
	result.navigationEnabled = enabled && navigationEnabled;
	if (!enabled)
		return result;
	if (mouse) {
		const auto &state = mouse->GetState();
		result.pointerValid = state.connected && state.positionValid &&
		                      state.positionX >= 0 && state.positionY >= 0 &&
		                      state.positionX < width &&
		                      state.positionY < height;
		if (result.pointerValid) {
			result.pointerX =
			    static_cast<float>(state.positionX) / static_cast<float>(width);
			result.pointerY = static_cast<float>(state.positionY) /
			                  static_cast<float>(height);
		}

		result.pointerPressed = bindings.pointer && mouse->IsPressed(*bindings.pointer);
		result.pointerDown = bindings.pointer && mouse->IsDown(*bindings.pointer);
	}

	using UI = GameInputBindings::UI;
	const auto pressed = [&](UI::Action action) {
		const auto key = bindings.keys[action];
		const auto button = bindings.buttons[action];
		return (key && keyboard.IsPressed(*key)) ||
		       (gamepad && button && gamepad->IsPressed(*button));
	};
	constexpr std::array reverseKeys{Key::UNKNOWN, Key::SHIFT, Key::CTRL, Key::ALT};
	const auto modifier = static_cast<std::size_t>(bindings.reverseModifier);
	const bool reverse = modifier > 0 && modifier < reverseKeys.size() &&
	                     keyboard.IsDown(reverseKeys[modifier]);
	const bool cycle = bindings.next && keyboard.IsPressed(*bindings.next);
	result.next = cycle && !reverse;
	result.previous = cycle && reverse;
	result.horizontal = int(pressed(UI::RIGHT)) - int(pressed(UI::LEFT));
	result.vertical = int(pressed(UI::DOWN)) - int(pressed(UI::UP));
	const auto primary = bindings.keys[UI::SUBMIT];
	const auto alternate = bindings.alternateSubmit;
	const auto button = bindings.buttons[UI::SUBMIT];
	result.submitPressed = pressed(UI::SUBMIT) || (alternate && keyboard.IsPressed(*alternate));
	result.submitDown = (primary && keyboard.IsDown(*primary)) ||
	                    (alternate && keyboard.IsDown(*alternate)) ||
	                    (gamepad && button && gamepad->IsDown(*button));

	result.horizontal = std::clamp(result.horizontal, -1, 1);
	result.vertical = std::clamp(result.vertical, -1, 1);
	return result;
}
