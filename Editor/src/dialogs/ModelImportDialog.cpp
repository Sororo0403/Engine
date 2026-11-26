#include "dialogs/ModelImportDialog.h"
#include "assets/AssetDatabase.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <exception>
#include <imgui.h>

void ModelImportDialog::Show(const AssetDatabase &assets,
                             const std::filesystem::path &folder) {
	options_ = {};
	options_.assetRoot = assets.GetRootDirectory();
	options_.folder = folder;
	folders_ = {{}};

	for (const auto &directory : assets.GetDirectories()) {
		folders_.push_back(directory.lexically_relative(options_.assetRoot));
	}

	imported_.clear();
	name_.clear();
	error_.clear();
	opening_ = true;
	selecting_ = false;
}

ModelImportResult ModelImportDialog::Draw(const ImGuiSystem &ui,
                                          ModelManager &models) {
	constexpr const char *title = "Import Model###ModelImportDialog";

	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}

	ImGui::SetNextWindowSize({660, 0}, ImGuiCond_Appearing);

	if (!ui.BeginPopupModal(title)) {
		return ModelImportResult::NONE;
	}

	ui.BeginDisabled(selecting_);

	if (ui.Button("Choose Model")) {
		picker_.ShowImportSource(options_.source, FileDialogFilter::MODEL);
		selecting_ = true;
	}

	ui.Text(PathUtils::ToUtf8(options_.source));
	ui.Text("Source Folder");
	ui.Text(PathUtils::ToUtf8(options_.sourceRoot));
	const auto parent = options_.sourceRoot.parent_path();
	ui.BeginDisabled(options_.source.empty() || parent.empty() ||
	                 parent == options_.sourceRoot);
	if (ui.Button("Use Parent Source Folder")) {
		options_.sourceRoot = parent;
		error_.clear();
	}

	ui.EndDisabled();
	ui.SameLine();
	const auto modelFolder = options_.source.parent_path();
	ui.BeginDisabled(options_.source.empty() || options_.sourceRoot == modelFolder);
	if (ui.Button("Reset Source Folder")) {
		options_.sourceRoot = modelFolder;
		error_.clear();
	}
	ui.EndDisabled();

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

	ui.InputText("Model Folder Name", name_);
	ui.EndDisabled();

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	ModelImportResult result = ModelImportResult::NONE;
	ui.BeginDisabled(selecting_ || options_.source.empty());

	if (ui.Button("Import")) {
		try {
			options_.folderName = PathUtils::FromUtf8(name_);

			if (ModelImporter::Import(options_, models, imported_, error_)) {
				result = ModelImportResult::ACCEPTED;
			}
		} catch (const std::exception &exception) {
			error_ = exception.what();
		}
	}

	ui.EndDisabled();

	ui.SameLine();
	ui.BeginDisabled(selecting_);

	if (ui.Button("Cancel") ||
	    (!selecting_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
		result = ModelImportResult::CANCELLED;
	}

	ui.EndDisabled();

	if (selecting_) {
		const auto selected = picker_.Draw(ui);

		if (selected == FileDialogResult::ACCEPTED) {
			const auto &source = picker_.GetSelectedPath();
			if (options_.source != source) {
				options_.source = source;
				options_.sourceRoot = source.parent_path();
				name_ = PathUtils::IsValidFileName(source.stem())
				            ? PathUtils::ToUtf8(source.stem())
				            : "Model";
			}
			error_.clear();
		}

		if (selected != FileDialogResult::NONE) {
			selecting_ = false;
		}
	}

	if (result != ModelImportResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}

const std::filesystem::path &ModelImportDialog::GetImportedPath() const {
	return imported_;
}
