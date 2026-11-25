#include "gameplay/CharacterController.h"
#include "math/MathUtils.h"

#include "math/TransformUtils.h"
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
	       jumpBufferTime <= kMaximumJumpGraceSeconds &&
	       std::isfinite(groundSnapDistance) && groundSnapDistance >= 0.0f &&
	       groundSnapDistance <= kMaximumGroundSnapDistance &&
	       std::isfinite(stepHeight) && stepHeight >= 0.0f &&
	       stepHeight <= kMaximumStepHeight;
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
	if (collider.isTrigger || !settings.IsValid() ||
	    !world.CanPlace(collider, worldMatrix)) {
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
	grounded_ = world.Move(collider_, worldMatrix_,
	                       {0.0f, -kGroundProbeDistance, 0.0f}, contact) &&
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
	const auto controls = settings_.inputEnabled ? input : CharacterInput{};
	candidate.jumpPending_ |= controls.jumpPressed;
	candidate.accumulatedTime_ += (std::min)(deltaTime, kMaximumFrameSeconds);

	while (candidate.accumulatedTime_ >= kFixedStepSeconds) {
		if (!candidate.Step(controls, world)) {
			return false;
		}

		candidate.accumulatedTime_ -= kFixedStepSeconds;
	}

	*this = candidate;
	return true;
}

bool CharacterController::FindGroundObject(const CollisionWorld &world,
                                           SceneObjectId &objectId) const {
	CollisionSweepResult contact;

	if (!initialized_ || !world.CanPlace(collider_, worldMatrix_) ||
	    !world.Sweep(collider_, worldMatrix_,
	                 {0.0f, -kGroundProbeDistance, 0.0f}, contact)) {
		return false;
	}

	objectId = contact.objectId;
	return true;
}

bool CharacterController::TryStepUp(const CollisionWorld &world,
                                    const DirectX::XMFLOAT3 &horizontal,
                                    CollisionMoveResult &movement) const {
	if (horizontal.x == 0 && horizontal.z == 0)
		return false;

	// 上限と同じ高さの段差でも、Sweepで接触面からわずかに離す。
	const float lift = settings_.stepHeight + float(kGroundClearance);
	auto raised = worldMatrix_;
	CollisionSweepResult hit;
	if (!world.Sweep(collider_, raised, {0, lift, 0}, hit) || hit.objectId)
		return false;
	raised._42 += lift;
	if (!world.Sweep(collider_, raised, horizontal, hit) || hit.objectId)
		return false;
	raised._41 += horizontal.x;
	raised._43 += horizontal.z;

	const float down = lift + kGroundProbeDistance;
	if (!world.Sweep(collider_, raised, {0, -down, 0}, hit) || !hit.objectId ||
	    hit.normal.y <= .5f)
		return false;
	const auto rise = double(lift) - double(down) * hit.fraction;
	if (rise <= kGroundClearance ||
	    rise > settings_.stepHeight + kGroundClearance)
		return false;
	const float landed = worldMatrix_._42 + float(rise + kGroundClearance);
	raised._42 = landed;
	if (!world.CanPlace(collider_, raised))
		return false;

	movement = {{horizontal.x, landed - worldMatrix_._42, horizontal.z}, true};
	return true;
}

bool CharacterController::Step(const CharacterInput &input,
                               const CollisionWorld &world) {
	CollisionMoveResult ground;
	grounded_ = verticalVelocity_ <= 0.0f &&
	            world.Move(collider_, worldMatrix_,
	                       {0.0f, -kGroundProbeDistance, 0.0f}, ground) &&
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

	const bool canSnap =
	    grounded_ && settings_.groundSnapDistance > 0;
	const float step = static_cast<float>(kFixedStepSeconds);
	jumpPending_ = false;
	verticalVelocity_ = (std::max)(-settings_.terminalSpeed,
	                               verticalVelocity_ - settings_.gravity * step);
	const double magnitude =
	    (std::max)(1.0, std::hypot(static_cast<double>(input.moveX),
	                               static_cast<double>(input.moveZ)));
	const DirectX::XMFLOAT3 requested{
	    static_cast<float>(
	        (input.moveX / magnitude * settings_.moveSpeed) *
	        step),
	    verticalVelocity_ * step,
	    static_cast<float>(
	        (input.moveZ / magnitude * settings_.moveSpeed) *
	        step)};
	CollisionMoveResult movement;

	if (!world.Move(collider_, worldMatrix_, requested, movement)) {
		return false;
	}

	const bool stepped =
	    grounded_ && settings_.stepHeight > 0 && movement.hitWall &&
	    TryStepUp(world, {requested.x, 0, requested.z}, movement);
	auto transform = transform_;
	transform.translation.x += movement.displacement.x;
	transform.translation.y += movement.displacement.y;
	transform.translation.z += movement.displacement.z;
	auto matrix = worldMatrix_;
	matrix._41 = transform.translation.x;
	matrix._42 = transform.translation.y;
	matrix._43 = transform.translation.z;

	// 直前まで床にいた場合だけ、移動後の位置から下方向の最初の面へ寄せる。
	// Sweepで停止位置を得るため、斜面の横方向へは滑らせない。
	if (canSnap && !stepped) {
		const auto distance =
		    (std::max)(settings_.groundSnapDistance, kGroundProbeDistance);
		CollisionSweepResult floor;
		if (!world.Sweep(collider_, matrix, {0, -distance, 0}, floor))
			return false;
		movement.grounded = floor.objectId != 0 && floor.normal.y > .5f;

		if (movement.grounded) {
			const auto drop =
			    (std::min)(double(settings_.groundSnapDistance),
			               (std::max)(0.0, distance * floor.fraction -
			                                   kGroundClearance));
			transform.translation.y -= static_cast<float>(drop);
			matrix._42 = transform.translation.y;
		}
	}

	if (!world.CanPlace(collider_, matrix)) {
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
	if (!initialized_ || !world.CanPlace(collider_, worldMatrix)) {
		return false;
	}

	worldMatrix_ = worldMatrix;
	transform_.translation = {worldMatrix._41, worldMatrix._42,
	                          worldMatrix._43};
	return true;
}

bool CharacterController::RefreshContacts(const CollisionWorld &world) {
	CollisionMoveResult ground;

	if (!initialized_ || !world.Move(collider_, worldMatrix_,
	                                 {0, -kGroundProbeDistance, 0}, ground)) {
		return false;
	}

	grounded_ = verticalVelocity_ <= 0 && ground.grounded;

	if (grounded_) {
		coyoteRemaining_ = settings_.coyoteTime;
	}

	return true;
}

bool CharacterController::Launch(float speed) {
	if (!initialized_ || !std::isfinite(speed) || speed <= 0 ||
	    speed > CharacterSettings::kMaximumSpeed)
		return false;
	verticalVelocity_ = speed;
	grounded_ = false;
	jumpPending_ = false;
	coyoteRemaining_ = 0;
	jumpBufferRemaining_ = 0;
	return true;
}

bool CharacterController::SetSettings(const CharacterSettings &settings) {
	if (!initialized_ || !settings.IsValid())
		return false;
	settings_ = settings;
	if (!settings_.inputEnabled) {
		jumpPending_ = false;
		jumpBufferRemaining_ = 0;
	}
	return true;
}
