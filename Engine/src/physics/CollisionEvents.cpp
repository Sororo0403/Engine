#include "physics/CollisionEvents.h"
#include "physics/ColliderUtils.h"
#include "physics/RigidBodyWorld.h"
#include "physics/WorldCollider.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>

bool CollisionEvents::Update(const Scene &scene, const RigidBodyWorld *world,
                             std::span<const CollisionContact> impacts) {
	try {
		if (impacts.size() > kMaximumContacts)
			return false;
		for (const auto &pair : impacts) {
			const auto *a = scene.FindObject(pair.first),
			           *b = scene.FindObject(pair.second);
			if (!pair.first || pair.first >= pair.second || !a || !b ||
			    !a->collider || !b->collider || a->collider->isTrigger ||
			    b->collider->isTrigger)
				return false;
		}

		std::vector<CollisionContact> current;

		if (world) {
			if (!world->FindContacts(current))
				return false;
		} else {
			SceneHierarchy::WorldTransforms transforms;
			SceneActivity::States active;
			if (!SceneHierarchy::BuildWorldTransforms(scene, transforms) ||
			    !SceneActivity::BuildStates(scene, active))
				return false;

			struct Shape {
				WorldCollider geometry;
				CollisionFilter filter;
			};

			std::map<SceneObjectId, Shape> shapes;

			for (const auto &[id, object] : scene.GetObjects()) {
				if (!active.at(id) || !object.collider ||
				    object.collider->isTrigger)
					continue;
				bool belowCharacter = false;
				for (auto parent = object.parentId; parent;
				     parent = scene.FindObject(parent)->parentId)
					belowCharacter |= bool(scene.FindObject(parent)->character);
				if (belowCharacter)
					continue;
				Shape shape;
				std::string error;
				if (!WorldCollider::Create(*object.collider,
				                           transforms.at(id).matrix,
				                           shape.geometry, error))
					return false;
				shape.filter = object.collider->filter;
				shapes.emplace(id, shape);
			}

			for (auto a = shapes.begin(); a != shapes.end(); ++a)
				for (auto b = std::next(a); b != shapes.end(); ++b) {
					const auto &x = a->second, &y = b->second;
					if (!x.filter.CanCollide(y.filter))
						continue;
					bool touching = false;
					if (!x.geometry.Overlaps(y.geometry, -kContactTolerance,
					                         touching))
						return false;
					if (touching) {
						if (current.size() >= kMaximumContacts)
							return false;
						current.push_back({a->first, b->first});
					}
				}
		}

		std::set<CollisionContact> pairs(contacts_.begin(), contacts_.end());
		pairs.insert(current.begin(), current.end());
		pairs.insert(impacts.begin(), impacts.end());
		if (pairs.size() > kMaximumContacts * 3)
			return false;
		std::vector<CollisionEvent> events;

		for (const auto &pair : pairs) {
			const bool previous =
			    std::binary_search(contacts_.begin(), contacts_.end(), pair);
			const bool touching =
			    std::binary_search(current.begin(), current.end(), pair);
			if (touching)
				events.push_back({pair, previous
				                            ? CollisionEvent::Type::STAY
				                            : CollisionEvent::Type::ENTER});
			else if (previous)
				events.push_back({pair, CollisionEvent::Type::EXIT});
			else {
				events.push_back({pair, CollisionEvent::Type::ENTER});
				events.push_back({pair, CollisionEvent::Type::EXIT});
			}
		}

		contacts_.swap(current);
		events_.swap(events);
		return true;
	} catch (...) {
		return false;
	}
}
