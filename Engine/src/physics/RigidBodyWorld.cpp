// Joltの他のヘッダーより先に共通定義を読み込む。
#include <Jolt/Jolt.h>

#include "math/TransformUtils.h"
#include "physics/RigidBodyWorld.h"
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
/// <summary>
/// Jolt共通の型登録を、物理ワールドより先に作り、最後に終了する
/// </summary>
struct Registry {
	Registry() {
		if (JPH::Factory::sInstance)
			throw std::runtime_error(
			    "Jolt is already initialized outside the engine.");
		JPH::RegisterDefaultAllocator();
		if (!JPH::VerifyJoltVersionID())
			throw std::runtime_error(
			    "Jolt library configuration does not match the engine.");
		factory = std::make_unique<JPH::Factory>();
		JPH::Factory::sInstance = factory.get();
		JPH::RegisterTypes();
	}

	~Registry() {
		JPH::UnregisterTypes();
		JPH::Factory::sInstance = nullptr;
	}

	std::unique_ptr<JPH::Factory> factory;
};

/// <summary>
/// 初めて物理ワールドを作るときにだけ共通機能を初期化する
/// </summary>
std::shared_ptr<Registry> InitializeJolt() {
	static const auto registry = std::make_shared<Registry>();
	return registry;
}

/// <summary>
/// 静的な物体と、動的・Kinematicな物体を別々の衝突探索へ分ける
/// </summary>
class BroadLayers final : public JPH::BroadPhaseLayerInterface {
  public:
	JPH::uint GetNumBroadPhaseLayers() const override {
		return 2;
	}

	JPH::BroadPhaseLayer
	GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
		return JPH::BroadPhaseLayer(
		    static_cast<JPH::BroadPhaseLayer::Type>(layer));
	}
};

/// <summary>
/// 少なくとも片方が動く組だけを探索する
/// </summary>
class BroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
	bool ShouldCollide(JPH::ObjectLayer a,
	                   JPH::BroadPhaseLayer b) const override {
		return a != 0 || b.GetValue() != 0;
	}
};

/// <summary>
/// 静的な障害物同士の接触計算を省く。所属レイヤーとマスクはGroupFilterで判定する
/// </summary>
class LayerFilter final : public JPH::ObjectLayerPairFilter {
  public:
	bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
		return a != 0 || b != 0;
	}
};

/// <summary>
/// 有限かつ物理ワールドの対応範囲内にある座標か確認する
/// </summary>
bool Position(const DirectX::XMFLOAT3 &v) {
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
	       std::abs(v.x) <= 1000000 && std::abs(v.y) <= 1000000 &&
	       std::abs(v.z) <= 1000000;
}

/// <summary>
/// 拡大率・単位回転と物体の原点が、剛体で使用できる範囲内か確認する
/// </summary>
bool Pose(const Transform &t) {
	return TransformUtils::IsValid(t) && Position(t.translation) &&
	       t.scale.x >= .0001f && t.scale.y >= .0001f && t.scale.z >= .0001f &&
	       t.scale.x <= 10000 && t.scale.y <= 10000 && t.scale.z <= 10000;
}

/// <summary>
/// 拡大率を変更しない移動か確認する
/// </summary>
bool SameScale(const Transform &a, const Transform &b) {
	return a.scale.x == b.scale.x && a.scale.y == b.scale.y &&
	       a.scale.z == b.scale.z;
}

/// <summary>
/// エンジンの3成分をJoltへ渡す
/// </summary>
JPH::Vec3 Vector(const DirectX::XMFLOAT3 &v) {
	return {v.x, v.y, v.z};
}

/// <summary>
/// Joltの3成分をエンジンの値へ戻す
/// </summary>
DirectX::XMFLOAT3 Vector(JPH::Vec3Arg v) {
	return {v.GetX(), v.GetY(), v.GetZ()};
}

/// <summary>
/// 単位クォータニオンをJoltへ渡す
/// </summary>
JPH::Quat Rotation(const Transform &t) {
	return JPH::Quat(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w)
	    .Normalized();
}

/// <summary>
/// 物体の拡大率を掛けたCollider中心のオフセットを計算する
/// </summary>
JPH::Vec3 Offset(const RigidBodyDescription &d) {
	return {d.collider.center.x * d.transform.scale.x,
	        d.collider.center.y * d.transform.scale.y,
	        d.collider.center.z * d.transform.scale.z};
}

/// <summary>
/// 物体の原点から、回転後のCollider中心へ変換する
/// </summary>
JPH::RVec3 Center(const RigidBodyDescription &d, const Transform &pose) {
	return Vector(pose.translation) + Rotation(pose) * Offset(d);
}

/// <summary>
/// 無効化した剛体設定と、剛体を持たないColliderを静的な障害物として扱う
/// </summary>
JPH::EMotionType Motion(const RigidBodyDescription &d) {
	return !d.rigidBody || !d.rigidBody->enabled ? JPH::EMotionType::Static
	       : d.rigidBody->isKinematic            ? JPH::EMotionType::Kinematic
	                                             : JPH::EMotionType::Dynamic;
}
} // namespace

/// <summary>
/// 剛体の定義・衝突フィルターと、Joltが更新する状態を保持する
/// </summary>
struct RigidBodyWorld::Impl {
	// すべてのJoltオブジェクトより後に共通機能を解放する。
	std::shared_ptr<Registry> registry;
	BroadLayers layers;
	BroadFilter broadFilter;
	LayerFilter layerFilter;
	std::vector<RigidBodyDescription> descriptions;
	JPH::Ref<JPH::GroupFilterTable> group;
	// PhysicsSystemより先に共有フィルターを用意し、後に解放する
	JPH::PhysicsSystem system;
	JPH::TempAllocatorImplWithMallocFallback allocator{16 * 1024 * 1024};
	JPH::JobSystemSingleThreaded jobs{2048};
	std::map<SceneObjectId, std::pair<JPH::BodyID, std::size_t>> bodies;

	explicit Impl(std::span<const RigidBodyDescription> source,
	              std::shared_ptr<Registry> registration,
	              const JPH::Ref<JPH::GroupFilterTable> &filter = nullptr)
	    : registry(std::move(registration)),
	      descriptions(source.begin(), source.end()), group(filter) {
		system.Init(static_cast<JPH::uint>(kMaximumBodies), 0, 8192, 8192,
		            layers, broadFilter, layerFilter);
		system.SetGravity({0, -9.8f, 0});
		if (!group) {
			group = new JPH::GroupFilterTable(
			    static_cast<JPH::uint>(descriptions.size()));
			for (std::size_t i = 0; i < descriptions.size(); ++i)
				for (std::size_t j = i + 1; j < descriptions.size(); ++j)
					if (!descriptions[i].collider.filter.CanCollide(
					        descriptions[j].collider.filter))
						group->DisableCollision(static_cast<JPH::uint>(i),
						                        static_cast<JPH::uint>(j));
		}
		auto &interface = system.GetBodyInterface();
		for (std::size_t index = 0; index < descriptions.size(); ++index) {
			const auto &d = descriptions[index];
			const auto &h = d.collider.halfExtents;
			const auto &s = d.transform.scale;
			JPH::BoxShapeSettings shape({h.x * s.x, h.y * s.y, h.z * s.z}, 0);
			const auto created = shape.Create();
			if (created.HasError())
				throw std::runtime_error(std::format(
				    "Object ID {}: {}", d.object, created.GetError().c_str()));
			const auto motion = Motion(d);
			JPH::BodyCreationSettings settings(
			    created.Get(), Center(d, d.transform), Rotation(d.transform),
			    motion, motion == JPH::EMotionType::Static ? 0 : 1);
			settings.mCollisionGroup =
			    JPH::CollisionGroup(group, 0, static_cast<JPH::uint>(index));
			settings.mUserData = d.object;
			if (d.rigidBody) {
				const auto &r = *d.rigidBody;
				settings.mFriction = r.friction;
				settings.mRestitution = r.restitution;
				settings.mLinearDamping = r.linearDamping;
				settings.mAngularDamping = r.angularDamping;
				settings.mGravityFactor = r.gravityFactor;
				settings.mMaxAngularVelocity = 100;
				settings.mOverrideMassProperties =
				    JPH::EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = r.mass;
				if (motion != JPH::EMotionType::Static) {
					settings.mLinearVelocity = Vector(r.linearVelocity);
					settings.mAngularVelocity = Vector(r.angularVelocity);
				}
				for (unsigned axis = 0; axis < 3; ++axis)
					if (r.freezeRotation[axis])
						settings.mAllowedDOFs &=
						    ~static_cast<JPH::EAllowedDOFs>(1 << (axis + 3));
			}
			// 動的な箱はCCDを使い、薄い床を高速で通り抜けることを防ぐ。
			settings.mMotionQuality = motion == JPH::EMotionType::Dynamic
			                              ? JPH::EMotionQuality::LinearCast
			                              : JPH::EMotionQuality::Discrete;
			const auto id = interface.CreateAndAddBody(
			    settings,
			    (motion == JPH::EMotionType::Static || d.startSleeping)
			        ? JPH::EActivation::DontActivate
			        : JPH::EActivation::Activate);
			if (id.IsInvalid())
				throw std::runtime_error(
				    "The rigid body count limit was reached.");
			bodies.emplace(d.object, std::pair{id, index});
		}
		system.OptimizeBroadPhase();
	}

	/// <summary>
	/// 現在の姿勢・速度に加え、接触キャッシュと休止状態も独立したワールドへ復元する
	/// </summary>
	std::shared_ptr<Impl> Clone() const {
		JPH::StateRecorderImpl saved;
		system.SaveState(saved);
		if (saved.IsFailed())
			throw std::runtime_error("Could not record rigid body state.");
		auto copy = std::make_shared<Impl>(descriptions, registry, group);
		saved.Rewind();
		if (!copy->system.RestoreState(saved) || saved.IsFailed())
			throw std::runtime_error("Could not restore rigid body state.");
		return copy;
	}

	/// <summary>
	/// JoltのCollider中心を、登録時と同じ物体の原点へ戻す
	/// </summary>
	RigidBodyState State(SceneObjectId id) const {
		const auto &[body, index] = bodies.at(id);
		const auto &d = descriptions[index];
		const auto &interface = system.GetBodyInterface();
		JPH::RVec3 center;
		JPH::Quat rotation;
		interface.GetPositionAndRotation(body, center, rotation);
		JPH::Vec3 linear, angular;
		interface.GetLinearAndAngularVelocity(body, linear, angular);
		auto transform = d.transform;
		transform.translation = Vector(center - rotation * Offset(d));
		transform.rotation = {rotation.GetX(), rotation.GetY(), rotation.GetZ(),
		                      rotation.GetW()};
		return {id, transform, Vector(linear), Vector(angular),
		        interface.IsActive(body)};
	}
};

bool RigidBodyWorld::Rebuild(std::span<const RigidBodyDescription> bodies,
                             std::string &error, SceneObjectId *invalidObject) {
	SceneObjectId current = 0;
	if (invalidObject)
		*invalidObject = 0;
	try {
		if (bodies.size() > kMaximumBodies)
			throw std::runtime_error(
			    "A physics world supports at most 1024 boxes.");
		std::map<SceneObjectId, bool> ids;
		for (const auto &d : bodies) {
			current = d.object;
			if (!d.object || !ids.emplace(d.object, true).second ||
			    !Pose(d.transform) || d.collider.isTrigger ||
			    !d.collider.filter.IsValid() || !Position(d.collider.center) ||
			    !Position(d.collider.halfExtents) ||
			    d.collider.halfExtents.x <= 0 ||
			    d.collider.halfExtents.y <= 0 ||
			    d.collider.halfExtents.z <= 0 ||
			    (d.rigidBody && !d.rigidBody->IsValid()))
				throw std::runtime_error(
				    "Rigid bodies require unique IDs, valid settings and "
				    "non-Trigger boxes.");
			const auto center = Center(d, d.transform);
			const auto &h = d.collider.halfExtents;
			const auto &s = d.transform.scale;
			if (!Position(Vector(center)) || h.x * s.x > 1000000 ||
			    h.y * s.y > 1000000 || h.z * s.z > 1000000 ||
			    h.x * s.x < .0001f || h.y * s.y < .0001f || h.z * s.z < .0001f)
				throw std::runtime_error(
				    "Rigid body center or scaled half extents are outside the "
				    "supported range.");
		}
		current = 0;
		auto next = std::make_shared<Impl>(bodies, InitializeJolt());
		impl_ = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		if (invalidObject)
			*invalidObject = current;
		error = std::format("Rigid body ID {}: {}", current, exception.what());
		return false;
	}
}

bool RigidBodyWorld::Update(float seconds, std::string &error) {
	try {
		if (!impl_ || !std::isfinite(seconds) || seconds < 0)
			throw std::runtime_error(
			    "Create a physics world and use finite, non-negative time.");
		if (seconds < (std::numeric_limits<float>::min)()) {
			error.clear();
			return true;
		}
		auto next = impl_.use_count() == 1 ? impl_ : impl_->Clone();
		JPH::StateRecorderImpl saved;
		next->system.SaveState(saved);
		if (saved.IsFailed())
			throw std::runtime_error("Could not record rigid body state.");
		try {
			const float duration = (std::min)(seconds, kMaximumFrameSeconds);
			const int steps =
			    static_cast<int>(std::ceil(duration / kMaximumStepSeconds));
			for (int step = 0; step < steps; ++step)
				if (next->system.Update(duration / static_cast<float>(steps), 1,
				                        &next->allocator, &next->jobs) !=
				    JPH::EPhysicsUpdateError::None)
					throw std::runtime_error(
					    "The physics contact limit was reached.");
			for (const auto &[id, body] : next->bodies) {
				(void)body;
				const auto state = next->State(id);
				if (!Pose(state.transform) || !Position(state.linearVelocity) ||
				    !Position(state.angularVelocity))
					throw std::runtime_error(
					    "A rigid body moved outside the supported range.");
			}
		} catch (...) {
			saved.Rewind();
			if (!next->system.RestoreState(saved) || saved.IsFailed())
				throw std::runtime_error(
				    "Could not restore failed physics step.");
			throw;
		}
		impl_ = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

std::optional<RigidBodyState> RigidBodyWorld::GetState(SceneObjectId id) const {
	return impl_ && impl_->bodies.contains(id)
	           ? std::optional<RigidBodyState>{impl_->State(id)}
	           : std::nullopt;
}

bool RigidBodyWorld::Teleport(SceneObjectId id, const Transform &transform,
                              std::string &error) {
	try {
		if (!impl_ || !impl_->bodies.contains(id) || !Pose(transform))
			throw std::runtime_error(
			    "Teleport requires an existing body and valid world pose.");
		const auto &[body, index] = impl_->bodies.at(id);
		const auto &d = impl_->descriptions[index];
		if (!SameScale(d.transform, transform) ||
		    !Position(Vector(Center(d, transform))))
			throw std::runtime_error("Teleport cannot change scale or exceed "
			                         "the physics world bounds.");
		auto next = impl_.use_count() == 1 ? impl_ : impl_->Clone();
		next->system.GetBodyInterface().SetPositionAndRotation(
		    body, Center(d, transform), Rotation(transform),
		    Motion(d) == JPH::EMotionType::Static
		        ? JPH::EActivation::DontActivate
		        : JPH::EActivation::Activate);
		impl_ = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool RigidBodyWorld::MoveKinematic(SceneObjectId id, const Transform &transform,
                                   float seconds, std::string &error) {
	try {
		if (!impl_ || !impl_->bodies.contains(id) || !Pose(transform) ||
		    !std::isfinite(seconds) || seconds < 0.000001f ||
		    seconds > kMaximumFrameSeconds)
			throw std::runtime_error(
			    "Kinematic movement requires an existing body, valid pose and "
			    "time from 0.000001 to 0.1 seconds.");
		const auto &[body, index] = impl_->bodies.at(id);
		const auto &d = impl_->descriptions[index];
		if (Motion(d) != JPH::EMotionType::Kinematic ||
		    !SameScale(d.transform, transform) ||
		    !Position(Vector(Center(d, transform))))
			throw std::runtime_error("Kinematic movement requires a Kinematic "
			                         "body and unchanged scale.");
		const auto &interface = impl_->system.GetBodyInterface();
		JPH::RVec3 position;
		JPH::Quat rotation;
		interface.GetPositionAndRotation(body, position, rotation);
		const auto linear = Vector((Center(d, transform) - position) / seconds);
		const auto angular =
		    Vector((Rotation(transform) * rotation.Conjugated())
		               .GetAngularVelocity(seconds));
		RigidBodySettings limits;
		limits.linearVelocity = linear;
		limits.angularVelocity = angular;
		if (!limits.IsValid())
			throw std::runtime_error(
			    "Kinematic movement exceeds the supported velocity range.");
		auto next = impl_.use_count() == 1 ? impl_ : impl_->Clone();
		next->system.GetBodyInterface().MoveKinematic(
		    body, Center(d, transform), Rotation(transform), seconds);
		impl_ = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool RigidBodyWorld::AddImpulse(SceneObjectId id,
                                const DirectX::XMFLOAT3 &impulse,
                                std::string &error) {
	try {
		if (!impl_ || !impl_->bodies.contains(id) || !Position(impulse))
			throw std::runtime_error("Impulse requires an existing dynamic "
			                         "body and a finite vector.");
		const auto &[body, index] = impl_->bodies.at(id);
		if (Motion(impl_->descriptions[index]) != JPH::EMotionType::Dynamic)
			throw std::runtime_error("Only dynamic bodies accept impulses.");
		auto next = impl_.use_count() == 1 ? impl_ : impl_->Clone();
		auto &interface = next->system.GetBodyInterface();
		interface.AddImpulse(body, Vector(impulse));
		// 力積を加えた直後の問い合わせでも、設定した最大速度を超えない。
		JPH::Vec3 linear, angular;
		interface.GetLinearAndAngularVelocity(body, linear, angular);
		interface.SetLinearAndAngularVelocity(body, linear, angular);
		impl_ = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool RigidBodyWorld::SetVelocity(SceneObjectId id,
                                 const DirectX::XMFLOAT3 &linear,
                                 const DirectX::XMFLOAT3 &angular,
                                 std::string &error) {
	try {
		RigidBodySettings limits;
		limits.linearVelocity = linear;
		limits.angularVelocity = angular;
		if (!impl_ || !impl_->bodies.contains(id) || !limits.IsValid())
			throw std::runtime_error("Velocity requires an existing moving "
			                         "body and supported finite vectors.");
		const auto &[body, index] = impl_->bodies.at(id);
		if (Motion(impl_->descriptions[index]) == JPH::EMotionType::Static)
			throw std::runtime_error("Static bodies do not have velocity.");
		auto next = impl_.use_count() == 1 ? impl_ : impl_->Clone();
		next->system.GetBodyInterface().SetLinearAndAngularVelocity(
		    body, Vector(linear), Vector(angular));
		impl_ = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

namespace {
/// <summary>
/// 問い合わせ用の箱を、正の半径と中心へ変換する
/// </summary>
/// <param name="box">ワールド軸に沿った箱</param>
/// <param name="center">成功時に設定する中心</param>
/// <param name="half">成功時に設定する半径</param>
/// <returns>true:変換成功 false:非有限値、範囲外、または潰れた箱</returns>
bool QueryBox(const AxisAlignedBox &box, JPH::Vec3 &center, JPH::Vec3 &half) {
	if (!Position(box.minimum) || !Position(box.maximum))
		return false;
	half = (Vector(box.maximum) - Vector(box.minimum)) * .5f;
	center = Vector(box.minimum) + half;
	return half.GetX() > 0.000001f && half.GetY() > 0.000001f &&
	       half.GetZ() > 0.000001f;
}

/// <summary>
/// 自分自身と対象外のレイヤーを、Joltの形状問い合わせから除く
/// </summary>
class QueryBodies final : public JPH::BodyFilter {
  public:
	const std::map<SceneObjectId, std::pair<JPH::BodyID, std::size_t>> &bodies;
	const std::vector<RigidBodyDescription> &descriptions;
	std::span<const SceneObjectId> ignored;
	const std::optional<CollisionFilter> &filter;

	QueryBodies(const auto &entries, const auto &source,
	            std::span<const SceneObjectId> skip,
	            const std::optional<CollisionFilter> &collision)
	    : bodies(entries), descriptions(source), ignored(skip),
	      filter(collision) {
	}

	bool ShouldCollideLocked(const JPH::Body &body) const override {
		const auto id = body.GetUserData();
		const auto found = bodies.find(id);
		return found != bodies.end() &&
		       std::ranges::find(ignored, id) == ignored.end() &&
		       (!filter ||
		        filter->CanCollide(
		            descriptions[found->second.second].collider.filter));
	}
};

/// <summary>
/// 開始位置の箱を分離軸で確認し、GJKが移動方向から作った不安定な法線を補う
/// </summary>
/// <param name="center">問い合わせAABBの中心</param>
/// <param name="half">問い合わせAABBの半径</param>
/// <param name="movement">今回の移動量</param>
/// <param name="body">相手の現在の物理形状</param>
/// <param name="extent">相手の拡大率を反映した箱の半径</param>
/// <param name="normal">接触面が一意でない場合も、最小の重なりの外向き軸を設定する</param>
/// <returns>true:接触または重なり false:離れている、または接触面を滑る・離れるだけ</returns>
bool ResolveInitialContact(JPH::Vec3Arg center, JPH::Vec3Arg half,
                           JPH::Vec3Arg movement, const JPH::Body &body,
                           JPH::Vec3Arg extent, JPH::Vec3 &normal) {
	const std::array<JPH::Vec3, 3> world{
	    JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY(), JPH::Vec3::sAxisZ()};
	const auto rotation = body.GetRotation();
	const std::array<JPH::Vec3, 3> local{
	    rotation * world[0], rotation * world[1], rotation * world[2]};
	std::array<JPH::Vec3, 15> axes;
	for (int i = 0; i < 3; ++i) {
		axes[i] = world[i];
		axes[i + 3] = local[i];
	}
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			axes[6 + i * 3 + j] = world[i].Cross(local[j]);
	const auto delta = center - body.GetCenterOfMassPosition();
	float minimum = (std::numeric_limits<float>::max)();
	for (auto axis : axes) {
		const float length = axis.Length();
		if (length < .000001f)
			continue;
		axis /= length;
		const float radius = half.Dot(axis.Abs()) +
		                     extent.GetX() * std::abs(axis.Dot(local[0])) +
		                     extent.GetY() * std::abs(axis.Dot(local[1])) +
		                     extent.GetZ() * std::abs(axis.Dot(local[2]));
		const float offset = delta.Dot(axis), depth = radius - std::abs(offset);
		if (depth < -.00001f)
			return false;
		const auto outward = offset >= 0 ? axis : -axis;
		if (depth <= .00001f && outward.Dot(movement) >= -.0000001f)
			return false;
		if (depth < minimum) {
			minimum = depth;
			normal = outward;
		}
	}
	return true;
}

} // namespace

bool RigidBodyWorld::CanPlace(
    const AxisAlignedBox &box, std::span<const SceneObjectId> ignored,
    const std::optional<CollisionFilter> &filter) const {
	try {
		JPH::Vec3 center, half;
		if (!impl_ || !QueryBox(box, center, half) ||
		    (filter && !filter->IsValid()))
			return false;
		JPH::BoxShape shape(half, 0);
		JPH::CollideShapeSettings settings;
		settings.mCollisionTolerance = 0.000001f;
		settings.mPenetrationTolerance = 0.000001f;
		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> contacts;
		QueryBodies bodies(impl_->bodies, impl_->descriptions, ignored, filter);
		impl_->system.GetNarrowPhaseQuery().CollideShape(
		    &shape, JPH::Vec3::sReplicate(1), JPH::RMat44::sTranslation(center),
		    settings, center, contacts, {}, {}, bodies);
		// 面への接触とGJKの数値誤差だけでは配置を拒否しない。
		return std::ranges::none_of(contacts.mHits, [](const auto &hit) {
			return hit.mPenetrationDepth > 0.00001f;
		});
	} catch (const std::exception &) {
		return false;
	}
}

bool RigidBodyWorld::Sweep(const AxisAlignedBox &box,
                           const DirectX::XMFLOAT3 &displacement,
                           CollisionSweepResult &result,
                           std::span<const SceneObjectId> ignored,
                           const std::optional<CollisionFilter> &filter) const {
	try {
		JPH::Vec3 center, half;
		if (!impl_ || !QueryBox(box, center, half) || !Position(displacement) ||
		    (filter && !filter->IsValid()))
			return false;
		JPH::BoxShape shape(half, 0);
		JPH::ShapeCastSettings settings;
		settings.mCollisionTolerance = 0.000001f;
		settings.mPenetrationTolerance = 0.000001f;
		settings.mReturnDeepestPoint = true;
		settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
		JPH::AllHitCollisionCollector<JPH::CastShapeCollector> contacts;
		QueryBodies bodies(impl_->bodies, impl_->descriptions, ignored, filter);
		impl_->system.GetNarrowPhaseQuery().CastShape(
		    JPH::RShapeCast(&shape, JPH::Vec3::sReplicate(1),
		                    JPH::RMat44::sTranslation(center),
		                    Vector(displacement)),
		    settings, center, contacts, {}, {}, bodies);
		CollisionSweepResult next;
		for (const auto &hit : contacts.mHits) {
			auto normal =
			    -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
			if (hit.mFraction <= 0) {
				const JPH::BodyLockRead locked(
				    impl_->system.GetBodyLockInterface(), hit.mBodyID2);
				if (!locked.Succeeded())
					return false;
				const auto id = locked.GetBody().GetUserData();
				const auto &source =
				    impl_->descriptions[impl_->bodies.at(id).second];
				const auto &h = source.collider.halfExtents,
				           &s = source.transform.scale;
				if (!ResolveInitialContact(
				        center, half, Vector(displacement), locked.GetBody(),
				        JPH::Vec3{h.x * s.x, h.y * s.y, h.z * s.z}, normal))
					continue;
			}
			if (hit.mPenetrationDepth <= 0.00001f &&
			    normal.Dot(Vector(displacement)) >= -0.0000001f)
				continue;
			const auto id =
			    impl_->system.GetBodyInterface().GetUserData(hit.mBodyID2);
			const double fraction =
			    (std::max)(0.0, static_cast<double>(hit.mFraction));
			if (!next.objectId || fraction < next.fraction ||
			    (fraction == next.fraction && id < next.objectId))
				next = {id, fraction, Vector(normal)};
		}
		result = next;
		return true;
	} catch (const std::exception &) {
		return false;
	}
}

bool RigidBodyWorld::SetLinearVelocity(SceneObjectId id,
                                       const DirectX::XMFLOAT3 &velocity,
                                       std::string &error) {
	return SetVelocityComponent(id, velocity, false, error);
}

bool RigidBodyWorld::SetAngularVelocity(SceneObjectId id,
                                        const DirectX::XMFLOAT3 &velocity,
                                        std::string &error) {
	return SetVelocityComponent(id, velocity, true, error);
}

bool RigidBodyWorld::SetVelocityComponent(SceneObjectId id,
                                          const DirectX::XMFLOAT3 &velocity,
                                          bool angular, std::string &error) {
	try {
		RigidBodySettings limits;
		(angular ? limits.angularVelocity : limits.linearVelocity) = velocity;
		if (!impl_ || !impl_->bodies.contains(id) || !limits.IsValid())
			throw std::runtime_error("Velocity requires an existing moving "
			                         "body and a supported finite vector.");
		const auto &[body, index] = impl_->bodies.at(id);
		if (Motion(impl_->descriptions[index]) == JPH::EMotionType::Static)
			throw std::runtime_error("Static bodies do not have velocity.");
		auto next = impl_.use_count() == 1 ? impl_ : impl_->Clone();
		auto &interface = next->system.GetBodyInterface();
		if (angular)
			interface.SetAngularVelocity(body, Vector(velocity));
		else
			interface.SetLinearVelocity(body, Vector(velocity));
		impl_ = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
