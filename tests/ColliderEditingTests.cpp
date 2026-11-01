#include "history/EditorHistory.h"
#include "physics/BoxColliderUtils.h"
#include "scene/Scene.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value, const char *message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

void FitTests() {
	std::array<DirectX::XMFLOAT3, 8> points;
	for (unsigned int i = 0; i < 8; ++i) {
		points[i] = {(i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f,
		             (i & 4) ? 1.0f : -1.0f};
	}
	BoxCollider fit;
	Require(BoxColliderUtils::FitToPoints(points, fit) && fit.center.x == 0 &&
	            fit.halfExtents.x == 1 && fit.halfExtents.y == 1 &&
	            fit.halfExtents.z == 1,
	        "unit model fit");
	const std::array<DirectX::XMFLOAT3, 2> flat{{{1, 2, -4}, {5, 2, 8}}};
	Require(BoxColliderUtils::FitToPoints(flat, fit) && fit.center.x == 3 &&
	            fit.center.y == 2 && fit.center.z == 2 &&
	            fit.halfExtents.x == 2 && fit.halfExtents.y == 0.001f &&
	            fit.halfExtents.z == 6,
	        "flat/offset model fit");
	const auto before = fit;
	points[0].x = std::numeric_limits<float>::quiet_NaN();
	Require(!BoxColliderUtils::FitToPoints(points, fit) && fit == before &&
	            !BoxColliderUtils::FitToPoints({}, fit) && fit == before &&
	            !BoxColliderUtils::FitToPoints(flat, fit, 0) && fit == before,
	        "failed fit changed collider");
}

void EditingTests() {
	Scene scene;
	const auto id = scene.CreateObject("Stage floor");
	EditorHistory history(scene);
	Require(history.SetCollider(id, BoxCollider{}) &&
	            history.GetUndoDescription() == "Add Box Collider" &&
	            scene.FindObject(id)->collider && history.Undo() &&
	            !scene.FindObject(id)->collider && history.Redo() &&
	            scene.FindObject(id)->collider,
	        "add collider Undo/Redo");
	history.MarkSaved();
	Require(history.BeginCollider(id), "begin collider drag");
	for (int i = 0; i < 120; ++i) {
		scene.FindObject(id)->collider->center.x += 0.01f;
	}
	Require(history.IsEditing() && history.IsModified(),
	        "pending collider edit not dirty");
	history.CommitEdit();
	const auto after = scene.FindObject(id)->collider;
	Require(history.GetStateCount() == 3 &&
	            history.GetUndoDescription() == "Edit Box Collider" &&
	            history.Undo() && !history.IsModified() &&
	            scene.FindObject(id)->collider->center.x == 0 &&
	            history.Redo() && scene.FindObject(id)->collider == after,
	        "drag did not form one history entry");
	const auto count = history.GetStateCount();
	Require(history.BeginCollider(id), "begin unchanged collider edit");
	history.CommitEdit();
	Require(history.GetStateCount() == count &&
	            history.SetCollider(id, after) &&
	            history.GetStateCount() == count,
	        "no-op collider edit created history");
	auto invalid = *after;
	invalid.halfExtents.x = 0;
	Require(!history.SetCollider(id, invalid) &&
	            scene.FindObject(id)->collider == after &&
	            history.GetStateCount() == count &&
	            !history.SetCollider(99999, BoxCollider{}),
	        "invalid collider edit changed state");
	Require(history.SetCollider(id, std::nullopt) &&
	            !scene.FindObject(id)->collider &&
	            history.GetUndoDescription() == "Remove Box Collider" &&
	            history.Undo() && scene.FindObject(id)->collider == after,
	        "remove collider Undo");
	const auto copy = history.DuplicateObject(id);
	Require(copy != 0 && scene.FindObject(copy)->collider == after &&
	            history.Undo() && !scene.Contains(copy) && history.Redo() &&
	            scene.FindObject(copy)->collider == after,
	        "duplicate collider history");
	Require(history.DeleteObject(copy) && history.Undo() &&
	            scene.FindObject(copy)->collider == after,
	        "deleted collider restoration");
	std::cout << "PASS: collider model fitting, invalid fit preservation, "
	             "component add/drag/remove Undo, saved state, no-op/invalid "
	             "edits, duplication and deletion\n";
}
} // namespace

void RunColliderEditingTests() {
	FitTests();
	EditingTests();
}
