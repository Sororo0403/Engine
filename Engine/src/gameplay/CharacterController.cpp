#include "gameplay/CharacterController.h"

#include "physics/BoxColliderUtils.h"
#include "physics/CollisionWorld.h"

#include <algorithm>
#include <cmath>

bool CharacterSettings::IsValid() const {
	return std::isfinite(moveSpeed) && moveSpeed > 0.0f &&
	       moveSpeed <= kMaximumSpeed && std::isfinite(jumpSpeed) &&
	       jumpSpeed > 0.0f && jumpSpeed <= kMaximumSpeed &&
	       std::isfinite(gravity) && gravity > 0.0f &&
	       gravity <= kMaximumGravity && std::isfinite(terminalSpeed) &&
	       terminalSpeed > 0.0f && terminalSpeed <= kMaximumSpeed;
}

bool CharacterController::Initialize(const Transform &transform,
                                     const BoxCollider &collider,
                                     const CharacterSettings &settings,
                                     const CollisionWorld &world) {
	AxisAlignedBox body;

	if (!settings.IsValid() ||
	    !BoxColliderUtils::GetWorldBounds(collider, transform, body) ||
	    !world.CanPlace(body)) {
		return false;
	}

	transform_ = transform;
	collider_ = collider;
	settings_ = settings;
	verticalVelocity_ = 0.0f;
	accumulatedTime_ = 0.0;
	jumpPending_ = false;
	initialized_ = true;
	CollisionMoveResult contact;
	grounded_ =
	    world.Move(body, {0.0f, -kGroundProbeDistance, 0.0f}, contact) &&
	    contact.grounded;
	return true;
}

bool CharacterController::Teleport(const DirectX::XMFLOAT3 &position,
                                   const CollisionWorld &world) {
	if (!initialized_) {
		return false;
	}

	auto transform = transform_;
	transform.translation = position;
	return Initialize(transform, collider_, settings_, world);
}

bool CharacterController::Update(float deltaTime, const CharacterInput &input,
                                 const CollisionWorld &world) {
	if (!initialized_ || !std::isfinite(deltaTime) || deltaTime < 0.0f ||
	    !std::isfinite(input.moveX) || !std::isfinite(input.moveZ)) {
		return false;
	}

	auto candidate = *this;
	candidate.jumpPending_ |= input.jumpPressed;
	candidate.accumulatedTime_ += (std::min)(deltaTime, kMaximumFrameSeconds);

	while (candidate.accumulatedTime_ >= kFixedStepSeconds) {
		if (!candidate.Step(input, world)) {
			return false;
		}

		candidate.accumulatedTime_ -= kFixedStepSeconds;
	}

	*this = candidate;
	return true;
}

bool CharacterController::Step(const CharacterInput &input,
                               const CollisionWorld &world) {
	AxisAlignedBox body;

	if (!BoxColliderUtils::GetWorldBounds(collider_, transform_, body)) {
		return false;
	}

	CollisionMoveResult ground;
	grounded_ = verticalVelocity_ <= 0.0f &&
	            world.Move(body, {0.0f, -kGroundProbeDistance, 0.0f}, ground) &&
	            ground.grounded;

	if (jumpPending_ && grounded_) {
		verticalVelocity_ = settings_.jumpSpeed;
		grounded_ = false;
	}

	jumpPending_ = false;
	const float step = static_cast<float>(kFixedStepSeconds);
	verticalVelocity_ =
	    (std::max)(-settings_.terminalSpeed,
	               verticalVelocity_ - settings_.gravity * step);
	const double magnitude =
	    (std::max)(1.0, std::hypot(static_cast<double>(input.moveX),
	                               static_cast<double>(input.moveZ)));
	const DirectX::XMFLOAT3 requested{
	    static_cast<float>(input.moveX / magnitude * settings_.moveSpeed *
	                       step),
	    verticalVelocity_ * step,
	    static_cast<float>(input.moveZ / magnitude * settings_.moveSpeed *
	                       step)};
	CollisionMoveResult movement;

	if (!world.Move(body, requested, movement)) {
		return false;
	}

	auto transform = transform_;
	transform.translation.x += movement.displacement.x;
	transform.translation.y += movement.displacement.y;
	transform.translation.z += movement.displacement.z;
	AxisAlignedBox movedBody;

	if (!BoxColliderUtils::GetWorldBounds(collider_, transform, movedBody) ||
	    !world.CanPlace(movedBody)) {
		return false;
	}

	transform_ = transform;
	grounded_ = movement.grounded;

	if (movement.grounded || movement.hitCeiling) {
		verticalVelocity_ = 0.0f;
	}

	return true;
}
