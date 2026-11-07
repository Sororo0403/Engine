#include "gameplay/CharacterController.h"

#include "math/TransformUtils.h"
#include "physics/ColliderUtils.h"
#include "physics/CollisionWorld.h"

#include <algorithm>
#include <cmath>

bool CharacterSettings::IsValid() const {
	return std::isfinite(moveSpeed) && moveSpeed > 0.0f &&
	       moveSpeed <= kMaximumSpeed && std::isfinite(jumpSpeed) &&
	       jumpSpeed > 0.0f && jumpSpeed <= kMaximumSpeed &&
	       std::isfinite(gravity) && gravity > 0.0f &&
	       gravity <= kMaximumGravity && std::isfinite(terminalSpeed) &&
	       terminalSpeed > 0.0f && terminalSpeed <= kMaximumSpeed &&
	       std::isfinite(coyoteTime) && coyoteTime >= 0.0f &&
	       coyoteTime <= kMaximumJumpGraceSeconds &&
	       std::isfinite(jumpBufferTime) && jumpBufferTime >= 0.0f &&
	       jumpBufferTime <= kMaximumJumpGraceSeconds;
}

bool CharacterController::Initialize(const Transform &transform,
                                     const Collider &collider,
                                     const CharacterSettings &settings,
                                     const CollisionWorld &world) {
	if (!TransformUtils::IsValid(transform)) {
		return false;
	}
	DirectX::XMFLOAT4X4 matrix;
	DirectX::XMStoreFloat4x4(&matrix,
	                         TransformUtils::MakeWorldMatrix(transform));
	auto candidate = *this;
	if (!candidate.Initialize(matrix, collider, settings, world)) {
		return false;
	}
	candidate.transform_ = transform;
	*this = candidate;
	return true;
}

bool CharacterController::Initialize(const DirectX::XMFLOAT4X4 &worldMatrix,
                                     const Collider &collider,
                                     const CharacterSettings &settings,
                                     const CollisionWorld &world) {
	AxisAlignedBox body;

	if (collider.shape != Collider::Shape::BOX || collider.isTrigger ||
	    !settings.IsValid() ||
	    !ColliderUtils::GetWorldBounds(collider, worldMatrix, body) ||
	    !world.CanPlace(body, collider.filter)) {
		return false;
	}

	transform_ = {};
	transform_.translation = {worldMatrix._41, worldMatrix._42,
	                          worldMatrix._43};
	worldMatrix_ = worldMatrix;
	collider_ = collider;
	settings_ = settings;
	verticalVelocity_ = 0.0f;
	accumulatedTime_ = 0.0;
	jumpPending_ = false;
	coyoteRemaining_ = 0.0;
	jumpBufferRemaining_ = 0.0;
	initialized_ = true;
	CollisionMoveResult contact;
	grounded_ = world.Move(body, {0.0f, -kGroundProbeDistance, 0.0f}, contact,
	                       collider_.filter) &&
	            contact.grounded;
	coyoteRemaining_ = grounded_ ? settings_.coyoteTime : 0.0;
	return true;
}

bool CharacterController::Teleport(const DirectX::XMFLOAT3 &position,
                                   const CollisionWorld &world) {
	if (!initialized_) {
		return false;
	}

	auto matrix = worldMatrix_;
	matrix._41 = position.x;
	matrix._42 = position.y;
	matrix._43 = position.z;
	auto candidate = *this;
	if (!candidate.Initialize(matrix, collider_, settings_, world)) {
		return false;
	}
	candidate.transform_.scale = transform_.scale;
	candidate.transform_.rotation = transform_.rotation;
	*this = candidate;
	return true;
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

bool CharacterController::FindGroundObject(const CollisionWorld &world,
                                           SceneObjectId &objectId) const {
	AxisAlignedBox body;
	CollisionSweepResult contact;

	if (!initialized_ ||
	    !ColliderUtils::GetWorldBounds(collider_, worldMatrix_, body) ||
	    !world.CanPlace(body, collider_.filter) ||
	    !world.Sweep(body, {0.0f, -kGroundProbeDistance, 0.0f}, contact,
	                 collider_.filter)) {
		return false;
	}

	objectId = contact.objectId;
	return true;
}

bool CharacterController::Step(const CharacterInput &input,
                               const CollisionWorld &world) {
	AxisAlignedBox body;

	if (!ColliderUtils::GetWorldBounds(collider_, worldMatrix_, body)) {
		return false;
	}

	CollisionMoveResult ground;
	grounded_ = verticalVelocity_ <= 0.0f &&
	            world.Move(body, {0.0f, -kGroundProbeDistance, 0.0f}, ground,
	                       collider_.filter) &&
	            ground.grounded;

	if (grounded_) {
		coyoteRemaining_ = settings_.coyoteTime;
	} else {
		coyoteRemaining_ =
		    (std::max)(0.0, coyoteRemaining_ - kFixedStepSeconds);
	}

	if (jumpPending_) {
		jumpBufferRemaining_ = settings_.jumpBufferTime;
	}

	if ((jumpPending_ || jumpBufferRemaining_ > 0.0) &&
	    (grounded_ || coyoteRemaining_ > 0.0)) {
		verticalVelocity_ = settings_.jumpSpeed;
		grounded_ = false;
		coyoteRemaining_ = 0.0;
		jumpBufferRemaining_ = 0.0;
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

	if (!world.Move(body, requested, movement, collider_.filter)) {
		return false;
	}

	auto transform = transform_;
	transform.translation.x += movement.displacement.x;
	transform.translation.y += movement.displacement.y;
	transform.translation.z += movement.displacement.z;
	auto matrix = worldMatrix_;
	matrix._41 = transform.translation.x;
	matrix._42 = transform.translation.y;
	matrix._43 = transform.translation.z;
	AxisAlignedBox movedBody;

	if (!ColliderUtils::GetWorldBounds(collider_, matrix, movedBody) ||
	    !world.CanPlace(movedBody, collider_.filter)) {
		return false;
	}

	transform_ = transform;
	worldMatrix_ = matrix;
	grounded_ = movement.grounded;

	if (movement.grounded || movement.hitCeiling) {
		verticalVelocity_ = 0.0f;
	}

	if (grounded_) {
		coyoteRemaining_ = settings_.coyoteTime;

		// 着地した固定更新内で入力を消費し、次の更新から上昇する。
		if (jumpBufferRemaining_ > 0.0) {
			verticalVelocity_ = settings_.jumpSpeed;
			grounded_ = false;
			coyoteRemaining_ = 0.0;
			jumpBufferRemaining_ = 0.0;
		}
	}

	jumpBufferRemaining_ =
	    (std::max)(0.0, jumpBufferRemaining_ - kFixedStepSeconds);

	return true;
}

bool CharacterController::SynchronizeWorldMatrix(
    const DirectX::XMFLOAT4X4 &worldMatrix, const CollisionWorld &world) {
	AxisAlignedBox body;
	if (!initialized_ ||
	    !ColliderUtils::GetWorldBounds(collider_, worldMatrix, body) ||
	    !world.CanPlace(body, collider_.filter)) {
		return false;
	}
	worldMatrix_ = worldMatrix;
	transform_.translation = {worldMatrix._41, worldMatrix._42,
	                          worldMatrix._43};
	return true;
}

bool CharacterController::RefreshContacts(const CollisionWorld &world) {
	AxisAlignedBox body;
	CollisionMoveResult ground;
	if (!initialized_ ||
	    !ColliderUtils::GetWorldBounds(collider_, worldMatrix_, body) ||
	    !world.Move(body, {0, -kGroundProbeDistance, 0}, ground,
	                collider_.filter)) {
		return false;
	}
	grounded_ = verticalVelocity_ <= 0 && ground.grounded;
	if (grounded_) {
		coyoteRemaining_ = settings_.coyoteTime;
	}
	return true;
}
