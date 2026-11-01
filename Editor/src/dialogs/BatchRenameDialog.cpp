#include "dialogs/BatchRenameDialog.h"

#include "core/Logger.h"
#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"

#include <algorithm>
#include <exception>
#include <format>
#include <imgui.h>
#include <limits>
#include <set>

bool BatchRenameDialog::Show(const Scene &scene,
                             std::span<const SceneObjectId> objectIds) {
	if (objectIds.empty()) {
		return false;
	}

	std::set<SceneObjectId> seen;
	std::vector<ObjectRename> renames;
	renames.reserve(objectIds.size());

	for (const auto id : objectIds) {
		const auto *object = scene.FindObject(id);

		if (!object || !seen.insert(id).second) {
			return false;
		}

		renames.push_back({id, object->name, {}});
	}

	std::sort(renames.begin(), renames.end(), [](const auto &a, const auto &b) {
		return a.objectId < b.objectId;
	});
	renames_.swap(renames);
	baseName_ = "Object";
	firstNumber_ = 1;
	RebuildPreview();
	openRequested_ = true;
	focusRequested_ = true;
	return true;
}

void BatchRenameDialog::RebuildPreview() {
	error_.clear();

	if (baseName_.empty()) {
		error_ = "Enter a base name.";
	} else if (firstNumber_ < 0 || renames_.empty() ||
	           renames_.size() - 1 >
	               static_cast<std::size_t>((std::numeric_limits<int>::max)() -
	                                        firstNumber_)) {
		error_ = "Numbers must be between 0 and 2147483647 for every object.";
	}

	if (!error_.empty()) {
		for (auto &rename : renames_) {
			rename.after.clear();
		}

		return;
	}

	std::vector<std::string> names;
	names.reserve(renames_.size());

	for (std::size_t i = 0; i < renames_.size(); ++i) {
		names.push_back(std::format("{}_{}", baseName_,
		                            firstNumber_ + static_cast<int>(i)));
	}

	for (std::size_t i = 0; i < renames_.size(); ++i) {
		renames_[i].after.swap(names[i]);
	}
}

std::span<const ObjectRename> BatchRenameDialog::GetRenames() const {
	return renames_;
}

BatchRenameResult BatchRenameDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *kTitle =
	    "Rename selected objects###BatchRenameDialog";

	if (openRequested_) {
		ui.OpenPopup(kTitle);
		openRequested_ = false;
	}

	ImGui::SetNextWindowSize({600.0f, 420.0f}, ImGuiCond_FirstUseEver);

	if (!ui.BeginPopupModal(kTitle)) {
		return BatchRenameResult::NONE;
	}

	ui.Text("Names follow the Hierarchy order: Base_1, Base_2, ...");

	if (focusRequested_) {
		ImGui::SetKeyboardFocusHere();
		focusRequested_ = false;
	}

	const bool nameChanged = ui.InputText("Base name", baseName_);
	const bool numberChanged =
	    ImGui::InputInt("First number", &firstNumber_, 0, 0);

	if (nameChanged || numberChanged) {
		try {
			RebuildPreview();
		} catch (const std::exception &error) {
			Logger::Error(std::format("BatchRenameDialog: {}", error.what()));
			error_ = "Could not prepare names. See the log for details.";
		}
	}

	const bool drawPreview = ui.BeginChild("RenamePreview", 0.0f, 220.0f);

	if (drawPreview) {
		for (const auto &rename : renames_) {
			const auto label =
			    std::format("{} -> {}", rename.before,
			                error_.empty() ? rename.after : "(invalid)");
			ui.Text(label);
			ImGui::SetItemTooltip("%s", label.c_str());
		}
	}

	ui.EndChild();

	if (!error_.empty()) {
		ui.Text(error_);
	}

	BatchRenameResult result = BatchRenameResult::NONE;
	ui.BeginDisabled(!error_.empty() || renames_.empty());

	if (ui.Button("Apply Names")) {
		result = BatchRenameResult::ACCEPTED;
	}

	ui.EndDisabled();
	ui.SameLine();

	if (ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		result = BatchRenameResult::CANCELLED;
	}

	if (result != BatchRenameResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}
