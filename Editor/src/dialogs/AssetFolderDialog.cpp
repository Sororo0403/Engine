#include "dialogs/AssetFolderDialog.h"
#include "assets/AssetFolders.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <exception>
#include <imgui.h>

void AssetFolderDialog::Show(const std::filesystem::path &assetRoot,
                             const std::filesystem::path &parent) {
	root_ = assetRoot;
	parent_ = parent;
	created_.clear();
	name_ = "New Folder";
	error_.clear();
	opening_ = true;
}

AssetFolderResult AssetFolderDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *title = "New Folder###AssetFolderDialog";
	const bool focus = opening_;

	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}

	ImGui::SetNextWindowSize({480, 0}, ImGuiCond_Appearing);

	if (!ui.BeginPopupModal(title)) {
		return AssetFolderResult::NONE;
	}

	ui.Text(PathUtils::ToUtf8(root_ / parent_));

	if (focus) {
		ImGui::SetKeyboardFocusHere();
	}

	ui.InputText("Folder Name", name_);

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	AssetFolderResult result = AssetFolderResult::NONE;

	if (ui.Button("Create Folder")) {
		try {
			if (AssetFolders::Create(root_, parent_, PathUtils::FromUtf8(name_),
			                         created_, error_)) {
				result = AssetFolderResult::ACCEPTED;
			}
		} catch (const std::exception &exception) {
			error_ = exception.what();
		}
	}

	ui.SameLine();

	if (ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		result = AssetFolderResult::CANCELLED;
	}

	if (result != AssetFolderResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}

const std::filesystem::path &AssetFolderDialog::GetCreatedDirectory() const {
	return created_;
}
