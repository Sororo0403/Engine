#include "physics/TriggerWorld.h"

#include "physics/BoxColliderUtils.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <array>
#include <exception>
#include <set>
#include <utility>

namespace {
/// <summary>
/// 面だけの接触を除き、正の体積で重なっているか確認する
/// </summary>
/// <param name="a">1つ目の形状</param>
/// <param name="b">2つ目の形状</param>
/// <returns>true:重なりあり false:重なりなし</returns>
bool Overlaps(const AxisAlignedBox &a, const AxisAlignedBox &b) {
	return a.minimum.x < b.maximum.x && a.maximum.x > b.minimum.x &&
	       a.minimum.y < b.maximum.y && a.maximum.y > b.minimum.y &&
	       a.minimum.z < b.maximum.z && a.maximum.z > b.minimum.z;
}

/// <summary>
/// 前後のAABBの境界を直線補間し、同じ時刻に6つの分離条件をすべて解消する区間があるか調べる
/// </summary>
/// <param name="a0">1つ目の開始形状</param>
/// <param name="a1">1つ目の終点形状</param>
/// <param name="b0">2つ目の開始形状</param>
/// <param name="b1">2つ目の終点形状</param>
/// <returns>true:途中で正の体積が重なる false:重ならない、または瞬間的な接触だけ</returns>
bool Crosses(const AxisAlignedBox &a0, const AxisAlignedBox &a1,
             const AxisAlignedBox &b0, const AxisAlignedBox &b1) {
	const auto gaps = [](const AxisAlignedBox &a, const AxisAlignedBox &b) {
		return std::array<double, 6>{double(b.maximum.x) - a.minimum.x,
		                             double(a.maximum.x) - b.minimum.x,
		                             double(b.maximum.y) - a.minimum.y,
		                             double(a.maximum.y) - b.minimum.y,
		                             double(b.maximum.z) - a.minimum.z,
		                             double(a.maximum.z) - b.minimum.z};
	};
	const auto start = gaps(a0, b0), end = gaps(a1, b1);
	double first = 0, last = 1;
	for (std::size_t i = 0; i < start.size(); ++i) {
		if (start[i] <= 0 && end[i] <= 0) {
			return false;
		}
		const auto change = end[i] - start[i];
		if (change > 0) {
			first = (std::max)(first, -start[i] / change);
		} else if (change < 0) {
			last = (std::min)(last, -start[i] / change);
		}
		if (first >= last) {
			return false;
		}
	}
	return first < last;
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
			if (!BoxColliderUtils::GetWorldBounds(
			        *object.collider, transforms.at(id).matrix, bounds)) {
				return false;
			}
			shapes.emplace(id, Shape{bounds, object.collider->filter,
			                         object.collider->isTrigger});
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
			const bool inside =
			    eligible && Overlaps(a->second.bounds, b->second.bounds);
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
				    oldA->second.filter == a->second.filter &&
				    oldB->second.filter == b->second.filter &&
				    oldA->second.trigger == a->second.trigger &&
				    oldB->second.trigger == b->second.trigger &&
				    continuous(pair.first) && continuous(pair.second) &&
				    Crosses(oldA->second.bounds, a->second.bounds,
				            oldB->second.bounds, b->second.bounds)) {
					events.push_back({pair, TriggerEvent::Type::ENTER});
					events.push_back({pair, TriggerEvent::Type::EXIT});
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
