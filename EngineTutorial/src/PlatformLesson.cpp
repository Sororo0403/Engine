#include "PlatformLesson.h"
#include "BoxLesson.h"
#include "scene/Scene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace {

struct Platform {
	const char *name;
	float x;
	float top;
	float z;
	float width;
	float depth;
	DirectX::XMFLOAT3 color;
};

constexpr std::array kPlatforms{
    Platform{"スタート", 0.f, 0.f, 0.f, 4.2f, 4.2f, {0.26f, 0.45f, 0.58f}},
    Platform{"最初の足場", -0.6f, 0.45f, 4.7f, 3.6f, 3.2f, {0.30f, 0.48f, 0.65f}},
    Platform{"次の足場", 0.65f, 0.85f, 9.1f, 3.4f, 3.2f, {0.34f, 0.52f, 0.70f}},
    Platform{"最後の足場", -0.65f, 1.15f, 13.4f, 3.15f, 3.15f, {0.37f, 0.56f, 0.73f}},
    // 第6章の課題。ゴールの位置や幅を変えてコースの難しさを比べる。
    Platform{"ゴール", 0.55f, 1.55f, 17.7f, 3.5f, 3.1f, {0.90f, 0.61f, 0.25f}},
};

constexpr std::size_t kPracticePlatformCount = 3;
constexpr float kPlayerSize = 0.64f;
constexpr float kPlatformThickness = 0.50f;
constexpr float kMoveSpeed = 4.2f;
// 第5章の課題。初速と重力を一つずつ変えてジャンプを比べる。
constexpr float kJumpSpeed = 6.1f;
constexpr float kGravity = 14.f;
constexpr float kFallHeight = -3.f;

bool IsOverPlatform(const DirectX::XMFLOAT3 &position, const Platform &platform) {
	const float halfPlayer = kPlayerSize * 0.5f;
	return std::abs(position.x - platform.x) < platform.width * 0.5f + halfPlayer &&
	       std::abs(position.z - platform.z) < platform.depth * 0.5f + halfPlayer;
}

std::span<const Platform> GetPlatforms(PlatformLesson::Mode mode) {
	const std::span<const Platform> allPlatforms{kPlatforms};
	return mode == PlatformLesson::Mode::COURSE ? allPlatforms : allPlatforms.first(kPracticePlatformCount);
}

void AddGoalGate(Scene &scene, const Platform &goal) {
	const float z = goal.z + goal.depth * 0.5f - 0.25f;
	const float inset = goal.width * 0.36f;
	constexpr DirectX::XMFLOAT3 gold{1.f, 0.79f, 0.32f};
	AddLessonCube(scene, "ゴールの左柱", {goal.x - inset, goal.top + 0.95f, z},
	              {0.19f, 1.9f, 0.19f}, gold);
	AddLessonCube(scene, "ゴールの右柱", {goal.x + inset, goal.top + 0.95f, z},
	              {0.19f, 1.9f, 0.19f}, gold);
	AddLessonCube(scene, "ゴールの梁", {goal.x, goal.top + 1.88f, z},
	              {inset * 2.f + 0.19f, 0.22f, 0.23f}, gold);
}

} // namespace

void PlatformLesson::Initialize(Scene &scene, Mode mode) {
	mode_ = mode;
	for (const auto &platform : GetPlatforms(mode_)) {
		AddLessonCube(scene, platform.name,
		              {platform.x, platform.top - kPlatformThickness * 0.5f, platform.z},
		              {platform.width, kPlatformThickness, platform.depth}, platform.color);
	}
	if (mode_ == Mode::COURSE) {
		AddGoalGate(scene, kPlatforms.back());
	}
	player_ = AddLessonCube(scene, "プレイヤー", {}, {kPlayerSize, kPlayerSize, kPlayerSize},
	                        {0.21f, 0.80f, 0.62f});
	AddLessonLight(scene);
	Reset(scene);
}

void PlatformLesson::Reset(Scene &scene) {
	state_ = State::READY;
	elapsed_ = 0.f;
	falls_ = 0;
	Respawn(scene);
}

void PlatformLesson::Respawn(Scene &scene) {
	const auto &start = kPlatforms.front();
	scene.FindObject(player_)->transform.translation = {start.x, start.top + kPlayerSize * 0.5f, start.z};
	verticalSpeed_ = 0.f;
	grounded_ = true;
}

void PlatformLesson::Update(Scene &scene, const DirectX::XMFLOAT2 &direction,
                            bool jumpPressed, float seconds) {
	if (seconds <= 0.f || state_ == State::CLEARED) {
		return;
	}
	if (state_ == State::READY) {
		if (!jumpPressed && direction.x == 0.f && direction.y == 0.f) {
			return;
		}
		state_ = State::RUNNING;
	}
	if (jumpPressed && grounded_) {
		verticalSpeed_ = kJumpSpeed;
		grounded_ = false;
	}

	auto &position = scene.FindObject(player_)->transform.translation;
	// 描画が遅れたときも、着地判定は細かい時間に分けて進める。
	const float elapsed = std::min(seconds, 0.1f);
	const int steps = static_cast<int>(std::ceil(elapsed * 120.f));
	const float stepSeconds = elapsed / static_cast<float>(steps);
	for (int i = 0; i < steps; ++i) {
		elapsed_ += stepSeconds;
		if (Step(position, direction, stepSeconds)) {
			state_ = State::CLEARED;
			return;
		}
		if (position.y < kFallHeight) {
			++falls_;
			Respawn(scene);
			return;
		}
	}
}

bool PlatformLesson::Step(DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT2 &direction,
                          float seconds) {
	position.x += direction.x * kMoveSpeed * seconds;
	position.z += direction.y * kMoveSpeed * seconds;

	constexpr float halfPlayer = kPlayerSize * 0.5f;
	const float previousBottom = position.y - halfPlayer;
	verticalSpeed_ -= kGravity * seconds;
	position.y += verticalSpeed_ * seconds;
	grounded_ = false;
	if (verticalSpeed_ > 0.f) {
		return false;
	}

	// 下降中に箱の底が越えた面のうち、最も高い足場に着地する。
	const Platform *landing = nullptr;
	for (const auto &platform : GetPlatforms(mode_)) {
		if (previousBottom + 0.0001f >= platform.top &&
		    position.y - halfPlayer <= platform.top && IsOverPlatform(position, platform) &&
		    (!landing || platform.top > landing->top)) {
			landing = &platform;
		}
	}
	if (landing) {
		position.y = landing->top + halfPlayer;
		verticalSpeed_ = 0.f;
		grounded_ = true;
	}
	return landing == &kPlatforms.back();
}

const DirectX::XMFLOAT3 &PlatformLesson::GetPosition(const Scene &scene) const {
	return scene.FindObject(player_)->transform.translation;
}

bool PlatformLesson::IsGrounded() const {
	return grounded_;
}

PlatformLesson::State PlatformLesson::GetState() const {
	return state_;
}

float PlatformLesson::GetElapsed() const {
	return elapsed_;
}

int PlatformLesson::GetFalls() const {
	return falls_;
}
