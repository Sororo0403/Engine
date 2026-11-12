#include "dialogs/FileDialog.h"

#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"

#include <algorithm>
#include <exception>
#include <format>
#include <imgui.h>
#include <stdexcept>
#include <system_error>
#include <utility>

void FileDialog::Show(FileDialogMode mode,
                      const std::filesystem::path &initialPath,
                      FileDialogFilter filter) {
	if (((filter == FileDialogFilter::IMAGE ||
	      filter == FileDialogFilter::MODEL ||
	      filter == FileDialogFilter::AUDIO) &&
	     mode != FileDialogMode::OPEN) ||
	    (filter == FileDialogFilter::GAME_CONFIG &&
	     mode == FileDialogMode::SELECT_DIRECTORY)) {
		throw std::invalid_argument(
		    "This file type does not support the requested operation.");
	}

	filter_ = filter;
	mode_ = mode;
	selectedPath_.clear();
	entries_.clear();
	error_.clear();
	fileName_.clear();
	folderName_.clear();
	directoryText_.clear();
	confirmOverwrite_ = false;
	openRequested_ = true;

	try {
		directory_ = PathUtils::GetExecutableDirectory();

		if (!initialPath.empty()) {
			const auto resolved = PathUtils::ResolveFromExecutable(initialPath);

			std::error_code error;

			if (mode_ == FileDialogMode::SELECT_DIRECTORY ||
			    std::filesystem::is_directory(resolved, error)) {
				directory_ = resolved;
			} else {
				directory_ = resolved.parent_path();
				fileName_ = PathUtils::ToUtf8(resolved.filename());
			}
		}

		if (mode_ != FileDialogMode::OPEN &&
		    mode_ != FileDialogMode::SELECT_DIRECTORY && fileName_.empty()) {
			fileName_ = filter_ == FileDialogFilter::GAME_CONFIG
			                ? "Game.settings.json"
			                : "Untitled.scene.json";
		}

		directoryText_ = PathUtils::ToUtf8(directory_);
		Refresh();
	} catch (const std::exception &error) {
		error_ = error.what();
	}
}

void FileDialog::RetryOpen(std::string error) {
	const auto parent = selectedPath_.parent_path();
	if (!parent.empty() && parent != directory_) {
		ChangeDirectory(parent);
		fileName_ = PathUtils::ToUtf8(selectedPath_.filename());
	}
	selectedPath_.clear();
	error_ = std::move(error);
	openRequested_ = true;
}

FileDialogResult FileDialog::Draw(const ImGuiSystem &ui) {
	const char *title =
	    mode_ == FileDialogMode::SELECT_DIRECTORY ? "Select Folder###FileDialog"
	    : filter_ == FileDialogFilter::GAME_CONFIG
	        ? (mode_ == FileDialogMode::OPEN
	               ? "Open Game Configuration###FileDialog"
	               : "Save Game Configuration###FileDialog")
	    : filter_ == FileDialogFilter::MODEL ? "Open Model###FileDialog"
	    : filter_ == FileDialogFilter::IMAGE ? "Open Image###FileDialog"
	    : filter_ == FileDialogFilter::AUDIO ? "Open Audio###FileDialog"
	    : mode_ == FileDialogMode::OPEN      ? "Open Scene###FileDialog"
	    : mode_ == FileDialogMode::SAVE_COPY ? "Save Copy###FileDialog"
	                                         : "Save Scene###FileDialog";

	if (openRequested_) {
		ui.OpenPopup(title);
		openRequested_ = false;
	}

	if (!ui.BeginPopupModal(title)) {
		return FileDialogResult::NONE;
	}

	FileDialogResult result = FileDialogResult::NONE;

	if (confirmOverwrite_) {
		ui.Text("This file already exists:");
		ui.Text(PathUtils::ToUtf8(selectedPath_));
		ui.Text("Overwrite it?");
		ui.Separator();

		if (ui.Button("Overwrite")) {
			result = FileDialogResult::ACCEPTED;
		}

		ui.SameLine();

		if (ui.Button("Back")) {
			confirmOverwrite_ = false;
		}
	} else {
		ui.InputText("Folder", directoryText_);

		if (ui.Button("Go")) {
			try {
				ChangeDirectory(PathUtils::FromUtf8(directoryText_));
			} catch (const std::exception &error) {
				error_ = error.what();
			}
		}

		ui.SameLine();

		if (ui.Button("Up")) {
			const auto parent = directory_.parent_path();

			if (!parent.empty() && parent != directory_) {
				ChangeDirectory(parent);
			}
		}

		ui.SameLine();

		if (ui.Button("Refresh")) {
			Refresh();
		}

		ui.Text(PathUtils::ToUtf8(directory_));

		std::filesystem::path nextDirectory;

		const bool drawEntries = ui.BeginChild("Files", 600.0f, 240.0f);

		if (drawEntries) {
			for (std::size_t i = 0; i < entries_.size(); ++i) {
				const Entry &entry = entries_[i];

				const std::string name =
				    PathUtils::ToUtf8(entry.path.filename());

				const std::string label =
				    entry.directory ? std::format("[Folder] {}", name) : name;

				ui.PushID(static_cast<int>(i));

				const bool selected = !entry.directory && fileName_ == name;

				if (ui.Selectable(label.c_str(), selected)) {
					if (entry.directory) {
						nextDirectory = entry.path;
					} else {
						if (!selected)
							error_.clear();
						fileName_ = name;
					}
				}

				ui.PopID();
			}
		}

		ui.EndChild();

		// 一覧を走査し終えてから表示フォルダを変更する
		if (!nextDirectory.empty()) {
			ChangeDirectory(nextDirectory);
		}

		if (mode_ == FileDialogMode::SELECT_DIRECTORY) {
			ui.InputText("New Folder Name", folderName_);

			if (ui.Button("Create Folder")) {
				CreateFolder();
			}
		} else if (ui.InputText("File name", fileName_)) {
			error_.clear();
		}

		if (!error_.empty()) {
			ImGui::TextWrapped("%s", error_.c_str());
		}

		ui.Separator();

		const char *button = mode_ == FileDialogMode::SELECT_DIRECTORY
		                         ? "Select Folder"
		                     : mode_ == FileDialogMode::OPEN      ? "Open"
		                     : mode_ == FileDialogMode::SAVE_COPY ? "Save Copy"
		                                                          : "Save";

		if (ui.Button(button) && AcceptSelection()) {
			result = FileDialogResult::ACCEPTED;
		}
	}

	ui.SameLine();

	if (ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		result = FileDialogResult::CANCELLED;
	}

	if (result != FileDialogResult::NONE) {
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return result;
}

const std::filesystem::path &FileDialog::GetSelectedPath() const {
	return selectedPath_;
}

void FileDialog::Refresh() {
	entries_.clear();
	error_.clear();

	try {
		std::error_code error;

		std::filesystem::directory_iterator iterator(directory_, error);
		const std::filesystem::directory_iterator end;

		while (!error && iterator != end) {
			const auto &entry = *iterator;

			std::error_code typeError;
			const bool directory = entry.is_directory(typeError);

			if (!typeError) {
				if (directory) {
					entries_.push_back({entry.path(), true});
				} else if (mode_ != FileDialogMode::SELECT_DIRECTORY &&
				           entry.is_regular_file(typeError) && !typeError &&
				           (filter_ == FileDialogFilter::MODEL
				                ? PathUtils::IsModelFile(entry.path())
				            : filter_ == FileDialogFilter::IMAGE
				                ? PathUtils::IsImageFile(entry.path())
				            : filter_ == FileDialogFilter::AUDIO
				                ? PathUtils::HasExtension(entry.path(), ".wav")
				                : PathUtils::HasExtension(entry.path(),
				                                          ".json"))) {
					entries_.push_back({entry.path(), false});
				}
			}

			iterator.increment(error);
		}

		if (error) {
			entries_.clear();
			error_ = error.message();
			return;
		}

		std::sort(entries_.begin(), entries_.end(),
		          [](const Entry &left, const Entry &right) {
			          if (left.directory != right.directory) {
				          return left.directory;
			          }

			          return left.path.filename() < right.path.filename();
		          });
	} catch (const std::exception &error) {
		entries_.clear();
		error_ = error.what();
	}
}

void FileDialog::ChangeDirectory(const std::filesystem::path &path) {
	try {
		if (!path.is_absolute()) {
			error_ = "Enter an absolute folder path.";
			return;
		}

		std::error_code error;

		if (!std::filesystem::is_directory(path, error)) {
			error_ = error ? error.message() : "The folder does not exist.";
			return;
		}

		auto next = path.lexically_normal();
		auto text = PathUtils::ToUtf8(next);

		directory_.swap(next);
		directoryText_.swap(text);

		if (mode_ == FileDialogMode::OPEN) {
			fileName_.clear();
		}

		Refresh();
	} catch (const std::exception &error) {
		error_ = error.what();
	}
}

bool FileDialog::AcceptSelection() {
	try {
		error_.clear();

		if (mode_ == FileDialogMode::SELECT_DIRECTORY) {
			auto selected = ReadDirectory();
			selectedPath_.swap(selected);
			return true;
		}

		auto name = PathUtils::FromUtf8(fileName_);

		if (!PathUtils::IsValidFileName(name)) {
			error_ = "Enter a valid file name.";
			return false;
		}

		if (mode_ != FileDialogMode::OPEN && !name.has_extension()) {
			name += filter_ == FileDialogFilter::GAME_CONFIG ? ".json"
			                                                 : ".scene.json";
		}

		if (!PathUtils::IsValidFileName(name)) {
			error_ = "The complete file name is too long or invalid.";
			return false;
		}

		if (filter_ == FileDialogFilter::MODEL   ? !PathUtils::IsModelFile(name)
		    : filter_ == FileDialogFilter::IMAGE ? !PathUtils::IsImageFile(name)
		    : filter_ == FileDialogFilter::AUDIO
		        ? !PathUtils::HasExtension(name, ".wav")
		        : !PathUtils::HasExtension(name, ".json")) {
			error_ = filter_ == FileDialogFilter::MODEL
			             ? "Choose a GLB, glTF or OBJ model."
			         : filter_ == FileDialogFilter::IMAGE
			             ? "Choose a PNG, JPEG, BMP, TIFF or GIF image."
			         : filter_ == FileDialogFilter::AUDIO
			             ? "Choose a WAV audio file."
			             : "Use a .json file name.";
			return false;
		}

		// Goを押していなくても、表示中のFolder欄と異なる場所へ確定しない。
		auto candidate = ReadDirectory() / name;

		std::error_code error;
		const bool exists = std::filesystem::exists(candidate, error);

		if (error) {
			error_ = error.message();
			return false;
		}

		if (mode_ == FileDialogMode::OPEN && !exists) {
			error_ = "The file does not exist.";
			return false;
		}

		if (exists && !std::filesystem::is_regular_file(candidate, error)) {
			error_ =
			    error ? error.message() : "The selected path is not a file.";
			return false;
		}

		selectedPath_.swap(candidate);

		if (mode_ != FileDialogMode::OPEN && exists) {
			confirmOverwrite_ = true;
			return false;
		}

		return true;
	} catch (const std::exception &error) {
		error_ = error.what();
		return false;
	}
}

std::filesystem::path FileDialog::ReadDirectory() const {
	const auto directory = PathUtils::FromUtf8(directoryText_);

	if (!directory.is_absolute() ||
	    directory.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument("Enter an absolute folder path.");
	}

	const auto resolved = std::filesystem::canonical(directory);

	if (!std::filesystem::is_directory(resolved)) {
		throw std::invalid_argument("Choose an existing folder.");
	}

	return resolved;
}

void FileDialog::CreateFolder() {
	try {
		const auto name = PathUtils::FromUtf8(folderName_);

		if (!PathUtils::IsValidFileName(name)) {
			throw std::invalid_argument("Enter a single valid folder name.");
		}

		const auto created = ReadDirectory() / name;

		if (!std::filesystem::create_directory(created)) {
			throw std::runtime_error(
			    "That folder already exists. Choose another name.");
		}

		folderName_.clear();
		ChangeDirectory(created);
	} catch (const std::exception &error) {
		error_ = error.what();
	}
}
