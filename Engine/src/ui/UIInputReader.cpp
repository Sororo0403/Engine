#include "ui/UIInputReader.h"
#include "input/Gamepad.h"
#include "input/Keyboard.h"
#include "input/Mouse.h"
#include <algorithm>

UIInput UIInputReader::Read(const Keyboard &keyboard, const Mouse *mouse,
                            const Gamepad *gamepad, int width, int height,
                            bool enabled, bool navigationEnabled) {
	UIInput result;
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

		result.pointerPressed = mouse->IsPressed(MouseButton::LEFT);
		result.pointerDown = mouse->IsDown(MouseButton::LEFT);
	}

	const bool shift = keyboard.IsDown(Key::SHIFT),
	           tab = keyboard.IsPressed(Key::TAB);
	result.next = tab && !shift;
	result.previous = tab && shift;
	result.horizontal = int(keyboard.IsPressed(Key::RIGHT)) -
	                    int(keyboard.IsPressed(Key::LEFT));
	result.vertical =
	    int(keyboard.IsPressed(Key::DOWN)) - int(keyboard.IsPressed(Key::UP));
	result.submitPressed =
	    keyboard.IsPressed(Key::ENTER) || keyboard.IsPressed(Key::SPACE);
	result.submitDown =
	    keyboard.IsDown(Key::ENTER) || keyboard.IsDown(Key::SPACE);
	if (gamepad) {
		result.horizontal +=
		    int(gamepad->IsPressed(GamepadButton::DPAD_RIGHT)) -
		    int(gamepad->IsPressed(GamepadButton::DPAD_LEFT));
		result.vertical += int(gamepad->IsPressed(GamepadButton::DPAD_DOWN)) -
		                   int(gamepad->IsPressed(GamepadButton::DPAD_UP));
		result.submitPressed |= gamepad->IsPressed(GamepadButton::SOUTH);
		result.submitDown |= gamepad->IsDown(GamepadButton::SOUTH);
	}

	result.horizontal = std::clamp(result.horizontal, -1, 1);
	result.vertical = std::clamp(result.vertical, -1, 1);
	return result;
}
