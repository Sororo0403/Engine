#include "dialogs/AudioImportDialog.h"
#include "assets/AssetDatabase.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <exception>
#include <imgui.h>

void AudioImportDialog::Show(const AssetDatabase &assets,
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

AudioImportResult AudioImportDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *title = "Import Audio###AudioImportDialog";

	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}

	ImGui::SetNextWindowSize({600, 0}, ImGuiCond_Appearing);

	if (!ui.BeginPopupModal(title)) {
		return AudioImportResult::NONE;
	}

	ui.BeginDisabled(selecting_);

	if (ui.Button("Choose WAV")) {
		picker_.ShowImportSource(options_.source, FileDialogFilter::AUDIO);
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

	ui.InputText("Audio Name", name_);
	ui.EndDisabled();

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	AudioImportResult result = AudioImportResult::NONE;
	ui.BeginDisabled(selecting_ || options_.source.empty());

	if (ui.Button("Import")) {
		try {
			options_.fileName = PathUtils::FromUtf8(name_);

			if (AudioImporter::Import(options_, imported_, error_)) {
				result = AudioImportResult::ACCEPTED;
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
		result = AudioImportResult::CANCELLED;
	}

	ui.EndDisabled();

	if (selecting_) {
		const auto selected = picker_.Draw(ui);

		if (selected == FileDialogResult::ACCEPTED) {
			options_.source = picker_.GetSelectedPath();
			name_ = PathUtils::ToUtf8(options_.source.filename());
			error_.clear();
		}

		if (selected != FileDialogResult::NONE) {
			selecting_ = false;
		}
	}

	if (result != AudioImportResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}

const std::filesystem::path &AudioImportDialog::GetImportedPath() const {
	return imported_;
}
