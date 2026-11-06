// Joltの他のヘッダーより先に共通定義を読み込む。
#include <Jolt/Jolt.h>

#include "JoltRuntime.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include "physics/WorldCollider.h"
#include <Jolt/Geometry/ConvexSupport.h>
#include <Jolt/Geometry/EPAPenetrationDepth.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
JPH::Vec3 Vector(const DirectX::XMFLOAT3 &v) {
	return {v.x, v.y, v.z};
}

DirectX::XMFLOAT3 Vector(JPH::Vec3Arg v) {
	return {v.GetX(), v.GetY(), v.GetZ()};
}

bool Bounded(const DirectX::XMFLOAT3 &v, float bound) {
	return MathUtils::IsFinite(v) && std::abs(v.x) <= bound &&
	       std::abs(v.y) <= bound && std::abs(v.z) <= bound;
}

bool Depth(float v) {
	return std::isfinite(v) && std::abs(v) <= .01f;
}
} // namespace

struct WorldCollider::Impl {
	// 形状を解放してから共通登録を終了する。
	std::shared_ptr<JoltRuntime::Registry> registry;
	JPH::RefConst<JPH::Shape> shape;
	JPH::Vec3 scale = JPH::Vec3::sReplicate(1);
	JPH::Quat rotation = JPH::Quat::sIdentity();
	JPH::Vec3 center = JPH::Vec3::sZero();
	AxisAlignedBox bounds;
};

bool WorldCollider::Create(const Collider &collider,
                           const DirectX::XMFLOAT4X4 &world,
                           WorldCollider &result, std::string &error) {
	try {
		DirectX::XMFLOAT4X4 inverse;
		if (!collider.IsValid() ||
		    !TransformUtils::TryInverseWorldMatrix(world, inverse))
			throw std::runtime_error("Collider or world matrix is invalid.");
		auto next = std::make_shared<Impl>();
		next->registry = JoltRuntime::Initialize();
		const auto matrix = DirectX::XMLoadFloat4x4(&world);
		DirectX::XMFLOAT3 center;
		DirectX::XMStoreFloat3(
		    &center, DirectX::XMVector3TransformCoord(
		                 DirectX::XMLoadFloat3(&collider.center), matrix));
		if (!Bounded(center, kMaximumCoordinate))
			throw std::runtime_error(
			    "Collider world center is outside the supported range.");
		const std::array<JPH::Vec3, 3> basis{
		    JPH::Vec3{world._11, world._12, world._13},
		    JPH::Vec3{world._21, world._22, world._23},
		    JPH::Vec3{world._31, world._32, world._33}};
		JPH::ShapeSettings::ShapeResult created;
		if (collider.shape == Collider::Shape::BOX) {
			const auto &h = collider.halfExtents;
			std::array<JPH::Vec3, 8> points;
			for (unsigned i = 0; i < 8; ++i) {
				points[i] = basis[0] * ((i & 1) ? h.x : -h.x) +
				            basis[1] * ((i & 2) ? h.y : -h.y) +
				            basis[2] * ((i & 4) ? h.z : -h.z);
				if (!Bounded(Vector(points[i]), kMaximumCoordinate))
					throw std::runtime_error(
					    "Collider extent is outside the supported range.");
			}

			// ごく小さい箱も凸包の初期面積の判定で潰れないよう、構築時だけ単位付近へ正規化する。
			float extent = 0;
			for (const auto &point : points)
				extent = (std::max)(extent, point.Abs().ReduceMax());
			if (!std::isfinite(extent) || extent <= 0)
				throw std::runtime_error("Box Collider extent is invalid.");
			for (auto &point : points)
				point /= extent;
			next->scale = JPH::Vec3::sReplicate(extent);
			JPH::ConvexHullShapeSettings hull(points.data(), 8, 0);
			hull.mHullTolerance = .000001f;
			created = hull.Create();
		} else {
			const std::array<float, 3> lengths{
			    basis[0].Length(), basis[1].Length(), basis[2].Length()};
			const float scale = lengths[0];
			if (!std::isfinite(scale) || scale <= 0)
				throw std::runtime_error("Collider scale is invalid.");
			for (int i = 0; i < 3; ++i) {
				if (!std::isfinite(lengths[i]) ||
				    std::abs(lengths[i] - scale) > scale * kGeometryTolerance)
					throw std::runtime_error(
					    "Sphere and Capsule require uniform scale.");
				for (int j = 0; j < i; ++j)
					if (std::abs(basis[i].Dot(basis[j])) >
					    lengths[i] * lengths[j] * kGeometryTolerance)
						throw std::runtime_error(
						    "Sphere and Capsule do not support shear.");
			}
			const float radius = collider.radius * scale;
			const float height = collider.halfHeight * scale;
			if (!std::isfinite(radius) || radius < kMinimumExtent ||
			    radius > kMaximumCoordinate ||
			    (collider.shape == Collider::Shape::CAPSULE &&
			     (!std::isfinite(height) || height < kMinimumExtent ||
			      height + radius > kMaximumCoordinate)))
				throw std::runtime_error("Round Collider dimensions are "
				                         "outside the supported range.");
			if (collider.shape == Collider::Shape::SPHERE)
				created = JPH::SphereShapeSettings(radius).Create();
			else {
				created = JPH::CapsuleShapeSettings(height, radius).Create();
				next->rotation = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(),
				                                    basis[1] / lengths[1]);
			}
		}
		if (created.HasError())
			throw std::runtime_error(created.GetError().c_str());
		next->shape = created.Get();
		if (next->shape->GetInnerRadius() * next->scale.GetX() <
		    kMinimumExtent * (1 - kGeometryTolerance))
			throw std::runtime_error(
			    "Collider is thinner than the supported minimum extent.");
		next->center =
		    Vector(center) +
		    next->rotation * (next->shape->GetCenterOfMass() * next->scale);
		const auto box = next->shape->GetWorldSpaceBounds(
		    JPH::Mat44::sRotationTranslation(next->rotation, next->center),
		    next->scale);
		next->bounds = {Vector(box.mMin), Vector(box.mMax)};
		if (!MathUtils::IsFinite(next->bounds.minimum) ||
		    !MathUtils::IsFinite(next->bounds.maximum) ||
		    next->bounds.minimum.x >= next->bounds.maximum.x ||
		    next->bounds.minimum.y >= next->bounds.maximum.y ||
		    next->bounds.minimum.z >= next->bounds.maximum.z)
			throw std::runtime_error("Collider world bounds are invalid.");
		result.impl_ = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool WorldCollider::GetBounds(AxisAlignedBox &bounds) const {
	if (!impl_)
		return false;
	bounds = impl_->bounds;
	return true;
}

bool WorldCollider::DepthAt(const WorldCollider &other,
                            const DirectX::XMFLOAT3 &offset, float minimumDepth,
                            float &depth) const {
	if (!impl_ || !other.impl_ || !Depth(minimumDepth))
		return false;
	try {
		using Convex = JPH::ConvexShape;
		const auto *a = static_cast<const Convex *>(impl_->shape.GetPtr());
		const auto *b =
		    static_cast<const Convex *>(other.impl_->shape.GetPtr());
		const auto transform =
		    JPH::Mat44::sRotation(impl_->rotation)
		        .InversedRotationTranslation() *
		    JPH::Mat44::sRotationTranslation(
		        other.impl_->rotation,
		        other.impl_->center - impl_->center - Vector(offset));
		Convex::SupportBuffer aCoreBuffer, bCoreBuffer;
		const auto *aCore =
		    a->GetSupportFunction(Convex::ESupportMode::ExcludeConvexRadius,
		                          aCoreBuffer, impl_->scale);
		const auto *bCore =
		    b->GetSupportFunction(Convex::ESupportMode::ExcludeConvexRadius,
		                          bCoreBuffer, other.impl_->scale);
		JPH::TransformedConvexObject bCoreTransformed(transform, *bCore);
		JPH::EPAPenetrationDepth coreSolver;
		auto axis = transform.GetTranslation();
		if (axis.IsNearZero())
			axis = JPH::Vec3::sAxisX();
		JPH::Vec3 p1, p2;
		const float separation = (std::max)(0.f, -minimumDepth);
		const auto status = coreSolver.GetPenetrationDepthStepGJK(
		    *aCore, aCore->GetConvexRadius() + separation, bCoreTransformed,
		    bCore->GetConvexRadius(), .000001f, axis, p1, p2);
		float next = -std::numeric_limits<float>::infinity();
		if (status == JPH::EPAPenetrationDepth::EStatus::Colliding)
			next = (p2 - p1).Length() - separation;
		else if (status == JPH::EPAPenetrationDepth::EStatus::Indeterminate) {
			// 内側の形状が重なる場合は、半径を除いた単体をEPAへ持ち越さず、完全な支持形状から開始する。
			Convex::SupportBuffer aBuffer, bBuffer;
			const auto *aFull =
			    a->GetSupportFunction(Convex::ESupportMode::IncludeConvexRadius,
			                          aBuffer, impl_->scale);
			const auto *bFull =
			    b->GetSupportFunction(Convex::ESupportMode::IncludeConvexRadius,
			                          bBuffer, other.impl_->scale);
			JPH::TransformedConvexObject bTransformed(transform, *bFull);
			JPH::EPAPenetrationDepth solver;
			axis = transform.GetTranslation();
			if (axis.IsNearZero())
				axis = JPH::Vec3::sAxisX();
			next = solver.GetPenetrationDepth(*aFull, *aFull, 0, bTransformed,
			                                  bTransformed, 0, .000001f,
			                                  .000001f, axis, p1, p2)
			           ? (p2 - p1).Length()
			           : 0;
		}
		depth = next;
		return true;
	} catch (const std::exception &) {
		return false;
	}
}

bool WorldCollider::OverlapAt(const WorldCollider &other,
                              const DirectX::XMFLOAT3 &offset,
                              float minimumDepth, bool &overlap) const {
	float depth;
	if (!DepthAt(other, offset, minimumDepth, depth))
		return false;
	overlap = depth > minimumDepth;
	return true;
}

bool WorldCollider::Overlaps(const WorldCollider &other, float minimumDepth,
                             bool &overlap) const {
	return OverlapAt(other, {}, minimumDepth, overlap);
}

bool WorldCollider::CastAt(const WorldCollider &other,
                           const DirectX::XMFLOAT3 &offset,
                           const DirectX::XMFLOAT3 &displacement,
                           std::optional<ColliderCastHit> &hit) const {
	if (!impl_ || !other.impl_ ||
	    !Bounded(displacement, 2 * kMaximumCoordinate))
		return false;
	try {
		JPH::ShapeCastSettings settings;
		settings.mCollisionTolerance = .000001f;
		settings.mPenetrationTolerance = .000001f;
		settings.mUseShrunkenShapeAndConvexRadius = true;
		settings.mReturnDeepestPoint = true;
		settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
		const JPH::ShapeCast cast(
		    impl_->shape, impl_->scale,
		    JPH::Mat44::sRotationTranslation(impl_->rotation,
		                                     impl_->center + Vector(offset) -
		                                         other.impl_->center),
		    Vector(displacement));
		JPH::CollisionDispatch::sCastShapeVsShapeWorldSpace(
		    cast, settings, other.impl_->shape, other.impl_->scale, {},
		    JPH::Mat44::sRotation(other.impl_->rotation), {}, {}, collector);
		std::optional<ColliderCastHit> next;
		if (collector.HadHit()) {
			const auto &value = collector.mHit;
			next =
			    ColliderCastHit{(std::clamp)(double(value.mFraction), 0., 1.),
			                    Vector(-value.mPenetrationAxis.NormalizedOr(
			                        JPH::Vec3::sAxisY()))};
		}
		hit = next;
		return true;
	} catch (const std::exception &) {
		return false;
	}
}

bool WorldCollider::Cast(const WorldCollider &other,
                         const DirectX::XMFLOAT3 &displacement,
                         std::optional<ColliderCastHit> &hit) const {
	return CastAt(other, {}, displacement, hit);
}

bool WorldCollider::Crosses(const WorldCollider &other,
                            const DirectX::XMFLOAT3 &displacement,
                            float minimumDepth, bool &crosses) const {
	if (!impl_ || !other.impl_ || !Depth(minimumDepth) || minimumDepth < 0 ||
	    !Bounded(displacement, 2 * kMaximumCoordinate))
		return false;
	bool inside = false;
	if (!OverlapAt(other, {}, minimumDepth, inside))
		return false;
	if (inside) {
		crosses = true;
		return true;
	}
	if (!OverlapAt(other, displacement, minimumDepth, inside))
		return false;
	if (inside) {
		crosses = true;
		return true;
	}
	std::optional<ColliderCastHit> first, last;
	if (!CastAt(other, {}, displacement, first))
		return false;
	if (!first) {
		crosses = false;
		return true;
	}
	const DirectX::XMFLOAT3 reverse{-displacement.x, -displacement.y,
	                                -displacement.z};
	if (!CastAt(other, displacement, reverse, last))
		return false;
	if (!last || first->fraction >= 1 - last->fraction) {
		crosses = false;
		return true;
	}
	const float fraction =
	    static_cast<float>((first->fraction + 1 - last->fraction) * .5);
	const DirectX::XMFLOAT3 offset{displacement.x * fraction,
	                               displacement.y * fraction,
	                               displacement.z * fraction};
	if (!OverlapAt(other, offset, minimumDepth, inside))
		return false;
	if (inside || minimumDepth == 0) {
		crosses = inside;
		return true;
	}
	// 斜めの箱などでは、接触区間の中央と最大の深さの位置が一致しない。
	// 凸形状の内部距離を区間内で比較し、指定した深さを途中で超える場合も検知する。
	double low = first->fraction, high = 1 - last->fraction;
	for (int iteration = 0; iteration < 40; ++iteration) {
		const double left = (2 * low + high) / 3, right = (low + 2 * high) / 3;
		const auto depthAt = [&](double t, float &depth) {
			const DirectX::XMFLOAT3 moved{
			    static_cast<float>(displacement.x * t),
			    static_cast<float>(displacement.y * t),
			    static_cast<float>(displacement.z * t)};
			return DepthAt(other, moved, 0, depth);
		};
		float a, b;
		if (!depthAt(left, a) || !depthAt(right, b))
			return false;
		if (a > minimumDepth || b > minimumDepth) {
			crosses = true;
			return true;
		}
		if (a < b)
			low = left;
		else
			high = right;
	}
	crosses = false;
	return true;
}
