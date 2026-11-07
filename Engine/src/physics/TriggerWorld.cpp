#include "physics/TriggerWorld.h"

#include "physics/ColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <set>
#include <utility>

namespace {
/// <summary>
/// 回転・拡縮が変わらず、位置だけを直線補間できる合成行列か確認する
/// </summary>
bool SameBasis(const DirectX::XMFLOAT4X4 &a, const DirectX::XMFLOAT4X4 &b) {
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			if (std::abs(double(a.m[i][j]) - b.m[i][j]) >
			    .00001 * (std::max)({1., std::abs(double(a.m[i][j])),
			                         std::abs(double(b.m[i][j]))}))
				return false;
	return true;
}
} // namespace

bool TriggerWorld::Reset(const Scene &scene) {
	TriggerWorld candidate;
	if (!candidate.Update(scene)) {
		return false;
	}
	*this = std::move(candidate);
	return true;
}

bool TriggerWorld::Update(const Scene &scene,
                          std::span<const SceneObjectId> teleported) {
	try {
		SceneHierarchy::WorldTransforms transforms;
		if (!SceneHierarchy::BuildWorldTransforms(scene, transforms)) {
			return false;
		}
		SceneActivity::States active;
		if (!SceneActivity::BuildStates(scene, active)) {
			return false;
		}
		std::map<SceneObjectId, Shape> shapes;
		for (const auto &[id, object] : scene.GetObjects()) {
			if (!active.at(id) || !object.collider) {
				continue;
			}
			AxisAlignedBox bounds;
			if (!ColliderUtils::GetWorldBounds(
			        *object.collider, transforms.at(id).matrix, bounds)) {
				return false;
			}
			WorldCollider geometry;
			std::string error;
			if (!WorldCollider::Create(*object.collider,
			                           transforms.at(id).matrix, geometry,
			                           error))
				return false;
			shapes.emplace(id, Shape{bounds, object.collider->filter,
			                         object.collider->isTrigger,
			                         *object.collider, transforms.at(id).matrix,
			                         std::move(geometry)});
		}
		// 現在Triggerを持つ組と、退出が必要になる前回の接触を一度ずつ処理する。
		std::set<TriggerContact> pairs(contacts_.begin(), contacts_.end());
		for (const auto &[id, shape] : shapes) {
			if (!shape.trigger) {
				continue;
			}
			for (const auto &[other, otherShape] : shapes) {
				if (id != other && shape.filter.CanCollide(otherShape.filter)) {
					pairs.insert(
					    {(std::min)(id, other), (std::max)(id, other)});
				}
			}
		}
		std::vector<TriggerContact> contacts;
		std::vector<TriggerEvent> events;
		for (const auto &pair : pairs) {
			const bool wasInside =
			    std::binary_search(contacts_.begin(), contacts_.end(), pair);
			const auto a = shapes.find(pair.first),
			           b = shapes.find(pair.second);
			const bool eligible = a != shapes.end() && b != shapes.end() &&
			                      (a->second.trigger || b->second.trigger) &&
			                      a->second.filter.CanCollide(b->second.filter);
			bool inside = false;
			if (eligible && !a->second.geometry.Overlaps(b->second.geometry,
			                                             kMinimumDepth, inside))
				return false;
			if (inside) {
				contacts.push_back(pair);
				events.push_back({pair, wasInside ? TriggerEvent::Type::STAY
				                                  : TriggerEvent::Type::ENTER});
			} else if (wasInside) {
				events.push_back({pair, TriggerEvent::Type::EXIT});
			} else if (eligible) {
				const auto oldA = shapes_.find(pair.first),
				           oldB = shapes_.find(pair.second);
				const auto continuous = [&](SceneObjectId id) {
					return std::find(teleported.begin(), teleported.end(),
					                 id) == teleported.end();
				};
				if (oldA != shapes_.end() && oldB != shapes_.end() &&
				    oldA->second.collider == a->second.collider &&
				    oldB->second.collider == b->second.collider &&
				    SameBasis(oldA->second.matrix, a->second.matrix) &&
				    SameBasis(oldB->second.matrix, b->second.matrix) &&
				    continuous(pair.first) && continuous(pair.second)) {
					const auto &a0 = oldA->second.matrix,
					           &a1 = a->second.matrix,
					           &b0 = oldB->second.matrix,
					           &b1 = b->second.matrix;
					DirectX::XMFLOAT3 relative{
					    (a1._41 - a0._41) - (b1._41 - b0._41),
					    (a1._42 - a0._42) - (b1._42 - b0._42),
					    (a1._43 - a0._43) - (b1._43 - b0._43)};
					bool crosses = false;
					if (!oldA->second.geometry.Crosses(oldB->second.geometry,
					                                   relative, kMinimumDepth,
					                                   crosses))
						return false;
					if (crosses) {
						events.push_back({pair, TriggerEvent::Type::ENTER});
						events.push_back({pair, TriggerEvent::Type::EXIT});
					}
				}
			}
		}
		shapes_.swap(shapes);
		contacts_.swap(contacts);
		events_.swap(events);
		return true;
	} catch (const std::exception &) {
		return false;
	}
}

void TriggerWorld::Clear() {
	shapes_.clear();
	contacts_.clear();
	events_.clear();
}
