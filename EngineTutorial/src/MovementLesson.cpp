#include "MovementLesson.h"
#include "input/Keyboard.h"
#include "math/Transform.h"
#include <cmath>

namespace {

/// <summary>
/// この更新で進む距離を計算する
/// </summary>
float StepDistance(float speed, float seconds) {
	// 第3章の課題。更新回数を変えても1秒間の移動距離が揃うようにする。
	(void)seconds;
	return speed / 60.f;
}

} // namespace

void MoveLessonBox(Transform &box, const Keyboard &keyboard, float seconds) {
	const float x = float(keyboard.IsDown(Key::D)) - float(keyboard.IsDown(Key::A));
	const float z = float(keyboard.IsDown(Key::W)) - float(keyboard.IsDown(Key::S));
	const float length = std::hypot(x, z);
	if (length == 0) {
		return;
	}

	constexpr float speed = 3.f;
	const float distance = StepDistance(speed, seconds);
	box.translation.x += x / length * distance;
	box.translation.z += z / length * distance;
}
