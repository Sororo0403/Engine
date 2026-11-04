#include "dialogs/BoxCreationDialog.h"

#include "graphics/ui/ImGuiSystem.h"
#include "physics/BoxColliderUtils.h"
#include "scene/PrimitiveObjects.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <limits>
#include <utility>

void BoxCreationDialog::Show(const BoxCollider &bounds, float planeHeight) {
	settings_ = {};
	bounds_ = bounds;
	planeHeight_ = planeHeight;
	opening_ = true;
}

BoxCreationResult BoxCreationDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *title = "Box with Size###BoxCreationDialog";
	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}
	ImGui::SetNextWindowSize({460, 0}, ImGuiCond_Appearing);
	if (!ui.BeginPopupModal(title)) {
		return BoxCreationResult::NONE;
	}

	ui.InputText("Name", settings_.name);
	ImGui::InputFloat3("Center", &settings_.center.x);
	ImGui::InputFloat3("Size", &settings_.size.x);
	auto placed = settings_;
	const double centerY = double(planeHeight_) + double(placed.size.y) * 0.5;
	bool canPlace = std::isfinite(planeHeight_) && std::isfinite(centerY) &&
	                std::abs(centerY) <= (std::numeric_limits<float>::max)();

	if (canPlace) {
		placed.center.y = static_cast<float>(centerY);
		SceneObject checked;
		std::string message;
		canPlace =
		    PrimitiveObjects::TryCreateBox(placed, bounds_, checked, message);
		AxisAlignedBox world;
		canPlace = canPlace && checked.collider &&
		           BoxColliderUtils::GetWorldBounds(*checked.collider,
		                                            checked.transform, world) &&
		           std::abs(double(world.minimum.y) - planeHeight_) <=
		               BoxCreationSettings::kBoundsRelativeTolerance *
		                   (std::max)(1.0, std::abs(double(planeHeight_)));
	}

	ui.BeginDisabled(!canPlace);

	if (ui.Button("Place Bottom on Work Plane") && canPlace) {
		settings_ = std::move(placed);
	}

	ui.EndDisabled();
	SceneObject candidate;
	std::string error;
	const bool valid =
	    PrimitiveObjects::TryCreateBox(settings_, bounds_, candidate, error);
	if (!valid) {
		ImGui::TextWrapped("%s", error.c_str());
	}

	BoxCreationResult result = BoxCreationResult::NONE;
	ui.BeginDisabled(!valid);
	if (ui.Button("Create Box")) {
		result = BoxCreationResult::ACCEPTED;
	}
	ui.EndDisabled();
	ui.SameLine();
	if (result == BoxCreationResult::NONE &&
	    (ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
		result = BoxCreationResult::CANCELLED;
	}
	if (result != BoxCreationResult::NONE) {
		ui.CloseCurrentPopup();
	}
	ui.EndPopup();
	return result;
}

bool BoxCreationDialog::GetPreviewBounds(AxisAlignedBox &bounds) const {
	SceneObject object;
	std::string error;
	return PrimitiveObjects::TryCreateBox(settings_, bounds_, object, error) &&
	       object.collider &&
	       BoxColliderUtils::GetWorldBounds(*object.collider, object.transform,
	                                        bounds);
}
