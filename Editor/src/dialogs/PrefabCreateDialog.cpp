#include "dialogs/PrefabCreateDialog.h"

#include "assets/AssetFolders.h"
#include "assets/PrefabAsset.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <exception>
#include <imgui.h>

bool PrefabCreateDialog::Show(const Scene &scene, SceneObjectId root,
                              const std::filesystem::path &assetRoot,
                              const std::filesystem::path &parent,
                              std::string &error) {
	try {
		AssetFolders::Resolve(assetRoot, parent);
		if (!PrefabAsset::Capture(scene, root, snapshot_, error)) {
			return false;
		}
		assets_ = assetRoot;
		folder_ = parent;
		root_ = root;
		created_.clear();
		name_ = snapshot_.FindObject(root)->name;
		if (!PathUtils::IsValidFileName(PathUtils::FromUtf8(name_))) {
			name_ = "New Prefab";
		}
		error_.clear();
		opening_ = true;
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

PrefabCreateResult PrefabCreateDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *title = "Create Prefab###PrefabCreateDialog";
	const bool focus = opening_;
	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}
	ImGui::SetNextWindowSize({480, 0}, ImGuiCond_Appearing);
	if (!ui.BeginPopupModal(title)) {
		return PrefabCreateResult::NONE;
	}
	ui.Text(PathUtils::ToUtf8(assets_ / folder_));
	if (focus) {
		ImGui::SetKeyboardFocusHere();
	}
	ui.InputText("Prefab Name", name_);
	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	PrefabCreateResult result = PrefabCreateResult::NONE;
	if (ui.Button("Create Prefab")) {
		try {
			auto name = PathUtils::FromUtf8(name_);
			if (!PrefabAsset::IsPrefabPath(name)) {
				name += L".prefab.json";
			}
			if (!PathUtils::IsValidFileName(name)) {
				error_ = "Enter a single valid Prefab name.";
			} else {
				const auto destination =
				    AssetFolders::Resolve(assets_, folder_) / name;
				auto reference =
				    AssetFolders::MakeReference(assets_, destination);
				if (PrefabAsset::Save(snapshot_, root_, destination, error_)) {
					created_.swap(reference);
					result = PrefabCreateResult::ACCEPTED;
				}
			}
		} catch (const std::exception &exception) {
			error_ = exception.what();
		}
	}
	ui.SameLine();
	if (ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		result = PrefabCreateResult::CANCELLED;
	}
	if (result != PrefabCreateResult::NONE) {
		ui.CloseCurrentPopup();
	}
	ui.EndPopup();
	return result;
}

const std::filesystem::path &PrefabCreateDialog::GetCreatedPath() const {
	return created_;
}
