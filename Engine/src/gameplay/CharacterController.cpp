#include "gameplay/CharacterController.h"
#include "math/MathUtils.h"

#include "math/TransformUtils.h"
#include "physics/CollisionWorld.h"

#include <algorithm>
#include <cmath>

namespace {
using DirectX::XMFLOAT3;

XMFLOAT3 Scale(const XMFLOAT3 &v, float factor) {
	return {v.x * factor, v.y * factor, v.z * factor};
}

XMFLOAT3 Add(const XMFLOAT3 &a, const XMFLOAT3 &b) {
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}

float Dot(const XMFLOAT3 &a, const XMFLOAT3 &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

void Translate(DirectX::XMFLOAT4X4 &matrix, const XMFLOAT3 &offset) {
	matrix._41 += offset.x;
	matrix._42 += offset.y;
	matrix._43 += offset.z;
}

} // namespace

std::array<DirectX::XMFLOAT3, 2> CharacterSettings::GetMovementAxes() const {
	using namespace DirectX;
	const auto up = GetUp();
	const auto normal = XMLoadFloat3(&up);
	const auto reference = std::abs(up.z) < .99f
	                           ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, -1, 0, 0);
	const auto right = XMVector3Normalize(XMVector3Cross(normal, reference));
	std::array<XMFLOAT3, 2> result;
	XMStoreFloat3(&result[0], right);
	XMStoreFloat3(&result[1], XMVector3Cross(right, normal));
	return result;
}

DirectX::XMFLOAT3 CharacterSettings::GetUp() const {
	const auto length = std::hypot(double(gravityDirection[0]),
	                               double(gravityDirection[1]), double(gravityDirection[2]));
	if (!std::isfinite(length) || length == 0)
		return {0, 1, 0};
	return {float(-gravityDirection[0] / length), float(-gravityDirection[1] / length),
	        float(-gravityDirection[2] / length)};
}

bool CharacterSettings::IsValid() const {
	const auto &d = gravityDirection;
	if (!std::isfinite(d[0]) || !std::isfinite(d[1]) || !std::isfinite(d[2]) ||
	    (d[0] == 0 && d[1] == 0 && d[2] == 0))
		return false;
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
	const auto up = settings_.GetUp();
	CollisionMoveResult contact;
	grounded_ = world.Move(collider_, worldMatrix_,
	                       Scale(up, -kGroundProbeDistance), contact, up) &&
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
	const auto up = settings_.GetUp();
	CollisionSweepResult contact;

	if (!initialized_ || !world.CanPlace(collider_, worldMatrix_) ||
	    !world.Sweep(collider_, worldMatrix_,
	                 Scale(up, -kGroundProbeDistance), contact)) {
		return false;
	}

	objectId = Dot(contact.normal, up) > .5f ? contact.objectId : 0;
	return true;
}

bool CharacterController::TryStepUp(const CollisionWorld &world,
                                    const DirectX::XMFLOAT3 &horizontal,
                                    CollisionMoveResult &movement) const {
	const auto up = settings_.GetUp();
	if (Dot(horizontal, horizontal) == 0)
		return false;

	// 上限と同じ高さの段差でも、Sweepで接触面からわずかに離す。
	const float lift = settings_.stepHeight + float(kGroundClearance);
	auto raised = worldMatrix_;
	CollisionSweepResult hit;
	if (!world.Sweep(collider_, raised, Scale(up, lift), hit) || hit.objectId)
		return false;
	Translate(raised, Scale(up, lift));
	if (!world.Sweep(collider_, raised, horizontal, hit) || hit.objectId)
		return false;
	Translate(raised, horizontal);

	const float down = lift + kGroundProbeDistance;
	if (!world.Sweep(collider_, raised, Scale(up, -down), hit) || !hit.objectId ||
	    Dot(hit.normal, up) <= .5f)
		return false;
	const auto rise = double(lift) - double(down) * hit.fraction;
	if (rise <= kGroundClearance ||
	    rise > settings_.stepHeight + kGroundClearance)
		return false;
	const auto displacement = Add(horizontal, Scale(up, float(rise + kGroundClearance)));
	raised = worldMatrix_;
	Translate(raised, displacement);
	if (!world.CanPlace(collider_, raised))
		return false;

	movement = {displacement, true};
	return true;
}

bool CharacterController::Step(const CharacterInput &input,
                               const CollisionWorld &world) {
	const auto up = settings_.GetUp();
	CollisionMoveResult ground;
	grounded_ = verticalVelocity_ <= 0.0f &&
	            world.Move(collider_, worldMatrix_,
	                       Scale(up, -kGroundProbeDistance), ground, up) &&
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
	const float speed = settings_.moveSpeed * step;
	const auto [right, forward] = settings_.GetMovementAxes();
	const auto horizontal = Add(Scale(right, float(input.moveX / magnitude * speed)),
	                            Scale(forward, float(input.moveZ / magnitude * speed)));
	const auto requested = Add(horizontal, Scale(up, verticalVelocity_ * step));
	CollisionMoveResult movement;

	if (!world.Move(collider_, worldMatrix_, requested, movement, up)) {
		return false;
	}

	const bool stepped =
	    grounded_ && settings_.stepHeight > 0 && movement.hitWall &&
	    TryStepUp(world, horizontal, movement);
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
		if (!world.Sweep(collider_, matrix, Scale(up, -distance), floor))
			return false;
		movement.grounded = floor.objectId != 0 && Dot(floor.normal, up) > .5f;

		if (movement.grounded) {
			const auto drop =
			    (std::min)(double(settings_.groundSnapDistance),
			               (std::max)(0.0, distance * floor.fraction -
			                                   kGroundClearance));
			const auto offset = Scale(up, -static_cast<float>(drop));
			transform.translation = Add(transform.translation, offset);
			Translate(matrix, offset);
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
	const auto up = settings_.GetUp();
	CollisionMoveResult ground;

	if (!initialized_ || !world.Move(collider_, worldMatrix_,
	                                 Scale(up, -kGroundProbeDistance), ground, up)) {
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
