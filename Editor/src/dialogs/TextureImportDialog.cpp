#include "dialogs/TextureImportDialog.h"
#include "assets/AssetDatabase.h"
#include "assets/AssetFolders.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include <exception>
#include <imgui.h>

void TextureImportDialog::Show(const AssetDatabase &assets,
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

TextureImportResult TextureImportDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *title = "Import Texture###TextureImportDialog";

	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}

	ImGui::SetNextWindowSize({600, 0}, ImGuiCond_Appearing);

	if (!ui.BeginPopupModal(title)) {
		return TextureImportResult::NONE;
	}

	ui.BeginDisabled(selecting_);

	if (ui.Button("Choose Image")) {
		picker_.ShowImportSource(options_.source, FileDialogFilter::IMAGE);
		selecting_ = true;
	}

	ui.SameLine();
	ui.BeginDisabled(options_.source.empty());
	if (ui.Button("Show Source in Explorer")) {
		ShellUtils::RevealLocationInExplorer(options_.source, error_);
	}
	ui.EndDisabled();

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

	ui.InputText("Image Name", name_);
	ui.SameLine();
	ui.BeginDisabled(name_.empty());
	if (ui.Button("Suggest Name")) {
		try {
			name_ = AssetFolders::SuggestImportName(options_.assetRoot, options_.folder,
			                                        name_, true);
			error_.clear();
		} catch (const std::exception &exception) {
			error_ = exception.what();
		}
	}
	ui.EndDisabled();
	ui.EndDisabled();

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	TextureImportResult result = TextureImportResult::NONE;
	ui.BeginDisabled(selecting_ || options_.source.empty());

	if (ui.Button("Import")) {
		try {
			options_.fileName = PathUtils::FromUtf8(name_);

			if (TextureImporter::Import(options_, imported_, error_)) {
				result = TextureImportResult::ACCEPTED;
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
		result = TextureImportResult::CANCELLED;
	}

	ui.EndDisabled();

	if (selecting_) {
		const auto selected = picker_.Draw(ui);

		if (selected == FileDialogResult::ACCEPTED) {
			const auto &source = picker_.GetSelectedPath();
			if (options_.source != source) {
				options_.source = source;
				name_ = PathUtils::ToUtf8(source.filename());
			}
			error_.clear();
		}

		if (selected != FileDialogResult::NONE) {
			selecting_ = false;
		}
	}

	if (result != TextureImportResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}

const std::filesystem::path &TextureImportDialog::GetImportedPath() const {
	return imported_;
}
