#pragma once
#include "JoltRuntime.h"
#include "physics/WorldCollider.h"
#include <Jolt/Physics/Collision/Shape/Shape.h>

struct WorldCollider::Impl {
	// 形状を解放してから共通登録を終了する。
	std::shared_ptr<JoltRuntime::Registry> registry;
	JPH::RefConst<JPH::Shape> shape;
	JPH::Vec3 scale = JPH::Vec3::sReplicate(1);
	JPH::Quat rotation = JPH::Quat::sIdentity();
	JPH::Vec3 center = JPH::Vec3::sZero();
	AxisAlignedBox bounds;
};
