#include "dialogs/SceneImportDialog.h"
#include "assets/AssetDatabase.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <exception>
#include <imgui.h>

void SceneImportDialog::Show(const AssetDatabase &assets,
                             const std::filesystem::path &folder) {
	options_ = {};
	settings_ = {};
	importSettings_ = false;
	accepted_ = false;
	options_.assetRoot = assets.GetRootDirectory();
	options_.folder = folder;
	folders_ = {{}};

	for (const auto &directory : assets.GetDirectories()) {
		folders_.push_back(directory.lexically_relative(options_.assetRoot));
	}

	created_.clear();
	name_ = "Imported Scene";
	error_.clear();
	opening_ = true;
	selecting_ = false;
}

SceneImportResult SceneImportDialog::Draw(const ImGuiSystem &ui, Scene &scene) {
	constexpr const char *title = "Import Game Scene###SceneImportDialog";

	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}

	ImGui::SetNextWindowSize({660, 0}, ImGuiCond_Appearing);

	if (!ui.BeginPopupModal(title)) {
		return SceneImportResult::NONE;
	}

	ui.BeginDisabled(selecting_);

	if (ui.Button("Choose Game Configuration")) {
		picker_.Show(FileDialogMode::OPEN, options_.source,
		             FileDialogFilter::GAME_CONFIG);
		selecting_ = true;
	}

	ui.Text(PathUtils::ToUtf8(options_.source));
	const auto preview = options_.folder.empty()
	                         ? std::string("Root")
	                         : PathUtils::ToUtf8(options_.folder);
	if (ImGui::BeginCombo("Import Folder", preview.c_str())) {
		for (const auto &folder : folders_) {
			const auto label = folder.empty() ? std::string("Root")
			                                  : PathUtils::ToUtf8(folder);
			if (ui.Selectable(label.c_str(), folder == options_.folder)) {
				options_.folder = folder;
			}
		}

		ImGui::EndCombo();
	}

	ui.InputText("Scene Folder Name", name_);
	ImGui::Checkbox("Import Game Settings", &importSettings_);
	ui.EndDisabled();

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	SceneImportResult result = SceneImportResult::NONE;
	ui.BeginDisabled(selecting_ || options_.source.empty());

	if (ui.Button("Import")) {
		try {
			options_.folderName = PathUtils::FromUtf8(name_);

			if (SceneImporter::Import(options_, scene, created_, error_,
			                          importSettings_ ? &settings_ : nullptr)) {
				accepted_ = true;
				result = SceneImportResult::ACCEPTED;
			}
		} catch (const std::exception &exception) {
			error_ = exception.what();
		}
	}

	ui.EndDisabled();

	ui.SameLine();
	ui.BeginDisabled(selecting_);

	if (result == SceneImportResult::NONE &&
	    (ui.Button("Cancel") ||
	     (!selecting_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))) {
		result = SceneImportResult::CANCELLED;
	}

	ui.EndDisabled();

	if (selecting_) {
		const auto selected = picker_.Draw(ui);

		if (selected == FileDialogResult::ACCEPTED) {
			options_.source = picker_.GetSelectedPath();
			error_.clear();
		}

		if (selected != FileDialogResult::NONE) {
			selecting_ = false;
		}
	}

	if (result != SceneImportResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}

const std::filesystem::path &SceneImportDialog::GetCreatedDirectory() const {
	return created_;
}

const GameConfig *SceneImportDialog::GetImportedSettings() const {
	return accepted_ && importSettings_ ? &settings_ : nullptr;
}
