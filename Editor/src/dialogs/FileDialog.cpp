#include "dialogs/FileDialog.h"

#include "assets/AssetFolders.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include "platform/windows/StringConversion.h"

#include <Windows.h>
#include <algorithm>
#include <exception>
#include <format>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <memory>
#include <shobjidl.h>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <wrl/client.h>
#include <wrl/implements.h>

namespace {
constexpr DWORD editorPickerControl = 101;
constexpr HRESULT openEditorPicker = HRESULT_FROM_WIN32(ERROR_RETRY);

class EditorPickerEvents final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          IFileDialogEvents, IFileDialogControlEvents> {
  public:
	explicit EditorPickerEvents(IFileDialog &dialog) : dialog_(dialog) {}

	HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog *) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog *, IShellItem *) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog *) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog *) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog *, IShellItem *,
	                                           FDE_SHAREVIOLATION_RESPONSE *response) override {
		*response = FDESVR_DEFAULT;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog *) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog *, IShellItem *,
	                                      FDE_OVERWRITE_RESPONSE *response) override {
		*response = FDEOR_DEFAULT;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnItemSelected(IFileDialogCustomize *, DWORD, DWORD) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnButtonClicked(IFileDialogCustomize *, DWORD control) override {
		return control == editorPickerControl ? dialog_.Close(openEditorPicker) : S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnCheckButtonToggled(IFileDialogCustomize *, DWORD, BOOL) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnControlActivating(IFileDialogCustomize *, DWORD) override { return S_OK; }

  private:
	IFileDialog &dialog_;
};

void CheckDialogResult(HRESULT result) {
	if (FAILED(result)) {
		throw std::runtime_error(std::format(
		    "Windows file dialog failed (HRESULT 0x{:08X}).",
		    static_cast<unsigned long>(result)));
	}
}

/// <summary>
/// 保存先と同じフォルダで、ファイル種別を保った空き名を探す
/// </summary>
std::filesystem::path SuggestSaveName(const std::filesystem::path &path) {
	auto stem = path.stem();
	auto suffix = path.extension().native();
	if (PathUtils::HasExtension(stem, ".scene") ||
	    PathUtils::HasExtension(stem, ".settings")) {
		suffix = stem.extension().native() + suffix;
		stem = stem.stem();
	}

	return AssetFolders::SuggestAvailableName(path.parent_path(), stem.native(), suffix);
}
} // namespace

void FileDialog::Show(FileDialogMode mode,
                      const std::filesystem::path &initialPath,
                      FileDialogFilter filter, std::string title) {
	if (((filter == FileDialogFilter::IMAGE ||
	      filter == FileDialogFilter::MODEL ||
	      filter == FileDialogFilter::AUDIO ||
	      filter == FileDialogFilter::FONT) &&
	     mode != FileDialogMode::OPEN) ||
	    (filter == FileDialogFilter::GAME_CONFIG &&
	     mode == FileDialogMode::SELECT_DIRECTORY) ||
	    ((filter == FileDialogFilter::PROJECT || filter == FileDialogFilter::GAME_EXPORT) &&
	     mode != FileDialogMode::SELECT_DIRECTORY)) {
		throw std::invalid_argument(
		    "This file type does not support the requested operation.");
	}

	filter_ = filter;
	title_ = std::move(title);
	mode_ = mode;
	selectedPath_.clear();
	failedProject_.clear();
	offerPreviousProjectSession_ = false;
	restorePreviousProjectSession_ = false;
	failedProjectLocationError_.clear();
	entries_.clear();
	projectScenes_.clear();
	projectRoot_.clear();
	searchProjectScenes_ = false;
	projectSceneRefreshRequested_ = false;
	projectSceneRefreshError_.clear();
	projectSearchChange_.reset();
	error_.clear();
	fileName_.clear();
	folderName_.clear();
	directoryText_.clear();
	search_.clear();
	destinationExists_ = false;
	requireNewFile_ = false;
	overwriteConfirmed_ = false;
	openRequested_ = true;
	windowsBrowseRequested_ = true;

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
	} catch (const std::exception &error) {
		error_ = error.what();
	}
}

void FileDialog::ShowImportSource(const std::filesystem::path &source,
                                 FileDialogFilter filter) {
	if (!source.empty()) {
		Show(FileDialogMode::OPEN, source, filter);
		return;
	}

	auto folder = selectedPath_.empty() ? directory_ : selectedPath_.parent_path();
	std::error_code status;
	if (!folder.empty() && !std::filesystem::is_directory(folder, status)) {
		folder.clear();
	}
	Show(FileDialogMode::OPEN, folder, filter);
}

void FileDialog::RetrySelection() {
	RetrySelection({}, failedProject_);
}

void FileDialog::RetrySelection(std::string error,
                                std::filesystem::path failedLocation) {
	const bool selectingFolder = mode_ == FileDialogMode::SELECT_DIRECTORY;
	const auto folder = selectingFolder ? selectedPath_ : selectedPath_.parent_path();
	if (!searchProjectScenes_ && !folder.empty() && folder != directory_) {
		ChangeDirectory(folder);
	}
	if (!selectingFolder && !selectedPath_.empty()) {
		fileName_ = PathUtils::ToUtf8(searchProjectScenes_ ? selectedPath_
		                                                 : selectedPath_.filename());
	}
	selectedPath_.clear();
	Refresh();
	if (!error.empty()) {
		error_ = std::move(error);
	}
	failedProject_ = std::move(failedLocation);
	failedProjectLocationError_.clear();
	destinationExists_ = false;
	overwriteConfirmed_ = false;
	openRequested_ = true;
}

FileDialogResult FileDialog::BrowseWindows(const char *title) {
	using Microsoft::WRL::ComPtr;
	const bool pickFolder = mode_ == FileDialogMode::SELECT_DIRECTORY;
	const bool save = mode_ == FileDialogMode::SAVE || mode_ == FileDialogMode::SAVE_COPY;
	constexpr DWORD restoreSessionControl = 100;

	try {
		ComPtr<IFileDialog> dialog;
		CheckDialogResult(CoCreateInstance(save ? __uuidof(FileSaveDialog) : __uuidof(FileOpenDialog),
		                                   nullptr, CLSCTX_INPROC_SERVER,
		                                   IID_PPV_ARGS(dialog.GetAddressOf())));
		FILEOPENDIALOGOPTIONS options{};
		CheckDialogResult(dialog->GetOptions(&options));
		options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
		if (pickFolder) {
			options |= FOS_PICKFOLDERS;
		} else if (save) {
			// 保存名の補完後に、Editor 側で既存ファイルと上書き可否を確認する。
			options &= ~FOS_OVERWRITEPROMPT;
		} else {
			options |= FOS_FILEMUSTEXIST;
		}
		CheckDialogResult(dialog->SetOptions(options));
		const std::string_view caption(title);
		CheckDialogResult(dialog->SetTitle(
		    PathUtils::FromUtf8(std::string(caption.substr(0, caption.find("###")))).c_str()));

		if (!pickFolder) {
			const COMDLG_FILTERSPEC type =
			    filter_ == FileDialogFilter::MODEL ? COMDLG_FILTERSPEC{L"3D models", L"*.glb;*.gltf;*.obj"}
			    : filter_ == FileDialogFilter::IMAGE ? COMDLG_FILTERSPEC{L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif"}
			    : filter_ == FileDialogFilter::AUDIO ? COMDLG_FILTERSPEC{L"WAV audio", L"*.wav"}
			    : filter_ == FileDialogFilter::FONT ? COMDLG_FILTERSPEC{L"Fonts", L"*.ttf;*.otf"}
			                                         : COMDLG_FILTERSPEC{L"JSON files", L"*.json"};
			CheckDialogResult(dialog->SetFileTypes(1, &type));
			if (!fileName_.empty()) {
				const auto name = PathUtils::FromPathInput(fileName_).filename();
				CheckDialogResult(dialog->SetFileName(name.c_str()));
			}
		}

		ComPtr<IShellItem> folder;
		if (!directory_.empty() && SUCCEEDED(SHCreateItemFromParsingName(
		        directory_.c_str(), nullptr, IID_PPV_ARGS(folder.GetAddressOf())))) {
			CheckDialogResult(dialog->SetFolder(folder.Get()));
		}

		ComPtr<IFileDialogCustomize> controls;
		const bool hasEditorOptions = !projectRoot_.empty() || filter_ == FileDialogFilter::PROJECT ||
		                              filter_ == FileDialogFilter::GAME_EXPORT;
		if (hasEditorOptions || offerPreviousProjectSession_) {
			CheckDialogResult(dialog.As(&controls));
		}
		if (offerPreviousProjectSession_) {
			CheckDialogResult(controls->AddCheckButton(
			    restoreSessionControl, L"Restore the previous scene and Assets folder",
			    restorePreviousProjectSession_));
		}

		DWORD eventCookie = 0;
		if (hasEditorOptions) {
			CheckDialogResult(controls->AddPushButton(
			    editorPickerControl, projectRoot_.empty() ? L"Editor options..." : L"Search project scenes..."));
			const auto events = Microsoft::WRL::Make<EditorPickerEvents>(*dialog.Get());
			if (!events) {
				throw std::bad_alloc();
			}
			CheckDialogResult(dialog->Advise(events.Get(), &eventCookie));
		}
		const auto shown = dialog->Show(GetActiveWindow());
		if (hasEditorOptions) {
			CheckDialogResult(dialog->Unadvise(eventCookie));
		}
		if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
			return FileDialogResult::CANCELLED;
		}
		if (shown != openEditorPicker) {
			CheckDialogResult(shown);
		}
		if (offerPreviousProjectSession_) {
			BOOL restore = FALSE;
			CheckDialogResult(controls->GetCheckButtonState(restoreSessionControl, &restore));
			restorePreviousProjectSession_ = restore != FALSE;
		}
		if (shown == openEditorPicker) {
			if (!projectRoot_.empty()) {
				searchProjectScenes_ = true;
				fileName_.clear();
			}
			return FileDialogResult::NONE;
		}

		ComPtr<IShellItem> item;
		CheckDialogResult(dialog->GetResult(item.GetAddressOf()));
		PWSTR rawPath = nullptr;
		CheckDialogResult(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath));
		const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path(rawPath, CoTaskMemFree);
		const std::filesystem::path selected(path.get());
		directory_ = pickFolder ? selected : selected.parent_path();
		directoryText_ = PathUtils::ToUtf8(directory_);
		if (!pickFolder) {
			fileName_ = PathUtils::ToUtf8(selected.filename());
		}
		return AcceptSelection() ? FileDialogResult::ACCEPTED : FileDialogResult::NONE;
	} catch (const std::exception &error) {
		error_ = error.what();
		return FileDialogResult::NONE;
	}
}

FileDialogResult FileDialog::Draw(const ImGuiSystem &ui) {
	const char *title =
	    !title_.empty() ? title_.c_str()
	    : mode_ == FileDialogMode::SELECT_DIRECTORY ? "Select Folder###FileDialog"
	    : filter_ == FileDialogFilter::GAME_CONFIG
	        ? (mode_ == FileDialogMode::OPEN
	               ? "Open Game Configuration###FileDialog"
	               : "Save Game Configuration###FileDialog")
	    : filter_ == FileDialogFilter::MODEL ? "Open Model###FileDialog"
	    : filter_ == FileDialogFilter::IMAGE ? "Open Image###FileDialog"
	    : filter_ == FileDialogFilter::AUDIO ? "Open Audio###FileDialog"
	    : filter_ == FileDialogFilter::FONT ? "Open Font###FileDialog"
	    : mode_ == FileDialogMode::OPEN      ? "Open Scene###FileDialog"
	    : mode_ == FileDialogMode::SAVE_COPY ? "Save Copy###FileDialog"
	                                         : "Save Scene###FileDialog";

	if (openRequested_) {
		if (std::exchange(windowsBrowseRequested_, false)) {
			const auto result = BrowseWindows(title);
			if (result != FileDialogResult::NONE) {
				openRequested_ = false;
				return result;
			}
			Refresh(false);
		}
		ui.OpenPopup(title);
		openRequested_ = false;
	}

	if (!ui.BeginPopupModal(title)) {
		return FileDialogResult::NONE;
	}

	FileDialogResult result = FileDialogResult::NONE;

	if (destinationExists_) {
		ui.Text("This file already exists:");
		ui.Text(PathUtils::ToUtf8(selectedPath_));
		ui.Text(requireNewFile_ ? "Choose a new filename." : "Overwrite it?");
		ui.Separator();

		if (!requireNewFile_) {
			if (ui.Button("Overwrite")) {
				overwriteConfirmed_ = true;
				result = FileDialogResult::ACCEPTED;
			}
			ui.SameLine();
		}

		if (ui.Button("Back")) {
			destinationExists_ = false;
		}
		ui.SameLine();
		if (ui.Button("Suggest Name")) {
			try {
				fileName_ = PathUtils::ToUtf8(SuggestSaveName(selectedPath_));
				error_.clear();
				destinationExists_ = false;
			} catch (const std::exception &error) {
				error_ = error.what();
			}
		}
		if (!error_.empty()) {
			ImGui::TextWrapped("%s", error_.c_str());
		}
	} else {
		const bool folderSubmitted = ImGui::InputText(
		    "Folder", &directoryText_, ImGuiInputTextFlags_EnterReturnsTrue);
		if (filter_ == FileDialogFilter::PROJECT) {
			ImGui::SetItemTooltip("Paste the project folder or the full path to project.json.");
		} else if (filter_ == FileDialogFilter::GAME_EXPORT) {
			ImGui::SetItemTooltip("Paste the export folder or the full path to Game.exe or game.json.");
		}

		const bool goClicked = ui.Button("Go");
		if (goClicked || (folderSubmitted && mode_ != FileDialogMode::SELECT_DIRECTORY)) {
			try {
				ChangeDirectory(ReadFolderInput());
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

		if (ui.Button("Browse in Windows...")) {
			result = BrowseWindows(title);
			if (result != FileDialogResult::ACCEPTED) {
				result = FileDialogResult::NONE;
				Refresh(false);
			}
		}
		ui.SameLine();

		if (ui.Button("Refresh")) {
			Refresh();
			projectSceneRefreshRequested_ = !projectRoot_.empty();
		}

		ui.Text(PathUtils::ToUtf8(directory_));

		if (!projectRoot_.empty() &&
		    ImGui::Checkbox("Search Project Scenes", &searchProjectScenes_)) {
			projectSearchChange_ = searchProjectScenes_;
			fileName_.clear();
			error_.clear();
			ImGui::SetNextWindowScroll({-1.0f, 0.0f});
		}
		const bool choosingProject = filter_ == FileDialogFilter::PROJECT;
		if (!projectSceneRefreshError_.empty()) {
			ImGui::TextWrapped("%s\n%s", projectScenes_.empty()
			                                  ? "Could not load the project scene list."
			                                  : "Could not refresh project scenes. Showing the previous list.",
			                   projectSceneRefreshError_.c_str());
		}
		const bool searchFolders = choosingProject || filter_ == FileDialogFilter::GAME_EXPORT;
		bool folderSearchSubmitted = false;
		if (searchFolders) {
			folderSearchSubmitted = ImGui::InputText(
			    "Search Folders", &search_, ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SetItemTooltip("Press Enter when the search matches one project or game export.");
		} else if (mode_ != FileDialogMode::SELECT_DIRECTORY) {
			ui.InputText("Search Files", search_);
		}
		if ((searchFolders || mode_ != FileDialogMode::SELECT_DIRECTORY) &&
		    ImGui::IsItemEdited()) {
			ImGui::SetNextWindowScroll({-1.0f, 0.0f});
		}
		const auto search = StringConversion::Utf8ToWide(search_);
		std::filesystem::path nextDirectory;
		std::filesystem::path openFile;
		std::filesystem::path openFolder;

		const bool drawEntries = ui.BeginChild("Files", 600.0f, 240.0f);

		if (drawEntries) {
			bool matchingEntry = false;
			const Entry *searchMatch = nullptr;
			bool multipleMatches = false;
			const auto &entries = searchProjectScenes_ ? projectScenes_ : entries_;
			for (std::size_t i = 0; i < entries.size(); ++i) {
				const Entry &entry = entries[i];
				auto displayPath = entry.path.filename();
				if (searchProjectScenes_) {
					const auto relative = entry.path.lexically_relative(projectRoot_);
					displayPath = relative.empty() ? entry.path : relative;
				}
				if (searchFolders || !entry.directory) {
					const auto native = displayPath.generic_wstring();
					if (!search_.empty() &&
					    (search.empty() ||
					     FindStringOrdinal(FIND_FROMSTART, native.c_str(),
					                       static_cast<int>(native.size()), search.c_str(),
					                       static_cast<int>(search.size()), TRUE) < 0)) {
						continue;
					}
					matchingEntry = true;
				}
				if (folderSearchSubmitted && entry.openableFolder) {
					multipleMatches |= searchMatch != nullptr;
					searchMatch = &entry;
				}

				const std::string name = PathUtils::ToUtf8(displayPath);

				const char *folderLabel = entry.openableFolder
				                              ? (choosingProject ? "[Project]" : "[Game Export]")
				                              : "[Folder]";
				const std::string label =
				    entry.directory ? std::format("{} {}", folderLabel, name) : name;

				ui.PushID(static_cast<int>(i));

				const auto fileInput =
				    searchProjectScenes_ ? PathUtils::ToUtf8(entry.path) : name;
				const bool selected = entry.openableFolder
				    ? directoryText_ == PathUtils::ToUtf8(entry.path)
				    : !entry.directory && fileName_ == fileInput;

				const bool clicked = ui.Selectable(label.c_str(), selected);
				if (entry.openableFolder) {
					ImGui::SetItemTooltip(choosingProject
					                          ? "Select this project. Double-click to open it."
					                          : "Select this export folder. Double-click to open it.");
					if (ImGui::IsItemHovered() &&
					    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						openFolder = entry.path;
					}
				}
				if (!entry.directory && mode_ == FileDialogMode::OPEN &&
				    ImGui::IsItemHovered() &&
				    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					openFile = entry.path;
				}
				if (clicked) {
					if (entry.openableFolder) {
						directoryText_ = PathUtils::ToUtf8(entry.path);
						error_.clear();
					} else if (entry.directory) {
						nextDirectory = entry.path;
					} else {
						if (!selected)
							error_.clear();
						fileName_ = fileInput;
					}
				}

				ui.PopID();
			}
			if (!search_.empty() && !matchingEntry) {
				ui.Text(searchFolders ? "No matching folders." : "No matching files.");
			}
			if (folderSearchSubmitted && !search_.empty()) {
				if (searchMatch && !multipleMatches) {
					openFolder = searchMatch->path;
				} else {
					error_ = multipleMatches ? "Narrow the search to one result or select a folder."
					                         : "No project or game export matches the search.";
				}
			}
		}

		ui.EndChild();

		// 一覧を走査し終えてから表示フォルダを変更する
		if (!openFolder.empty()) {
			directoryText_ = PathUtils::ToUtf8(openFolder);
		} else if (!nextDirectory.empty()) {
			ChangeDirectory(nextDirectory);
		}

		bool fileSubmitted = false;
		if (mode_ != FileDialogMode::OPEN) {
			ui.InputText("New Folder Name", folderName_);
			ui.SameLine();
			if (ui.Button("Create Folder")) {
				CreateFolder();
			}
			ImGui::SetItemTooltip("Create a folder under the path in Folder and open it.");
		}

		if (mode_ != FileDialogMode::SELECT_DIRECTORY) {
			const bool enterSubmits = mode_ == FileDialogMode::OPEN || requireNewFile_;
			const bool submittedByEnter = ImGui::InputText(
			    "File name", &fileName_, enterSubmits ? ImGuiInputTextFlags_EnterReturnsTrue
			                                         : ImGuiInputTextFlags_None);
			fileSubmitted = enterSubmits && submittedByEnter;
			if (ImGui::IsItemEdited()) {
				error_.clear();
			}
			ImGui::SetItemTooltip(enterSubmits
			                          ? "Enter a file name or paste the full file path, then press Enter."
			                          : "Enter a file name or paste the full save path.");
			if (mode_ != FileDialogMode::OPEN && !fileName_.empty()) {
				try {
					const auto path = PathUtils::ToUtf8(ReadFilePath());
					ImGui::TextWrapped("Save to: %s", path.c_str());
				} catch (const std::invalid_argument &) {
					// 入力途中のエラーは保存を確定したときに表示する。
				}
			}
		}

		if (!error_.empty()) {
			ImGui::TextWrapped("%s", error_.c_str());
		}

		std::optional<std::string> folderBeforeRetry;
		if (!failedProject_.empty()) {
			const char *label = filter_ == FileDialogFilter::GAME_EXPORT
			                        ? "Show Previous Export Location"
			                    : filter_ == FileDialogFilter::SCENE || filter_ == FileDialogFilter::GAME_CONFIG
			                        ? (mode_ != FileDialogMode::OPEN ? "Show Failed Save Location"
			                           : filter_ == FileDialogFilter::SCENE ? "Show Failed Scene Location"
			                                                               : "Show Failed Configuration Location")
			                        : "Show Failed Project Location";
			if (ui.Button(label)) {
				ShellUtils::RevealLocationInExplorer(failedProject_,
				                                     failedProjectLocationError_);
			}
			ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(failedProject_).c_str());
			std::error_code saveStatus;
			if ((filter_ == FileDialogFilter::SCENE || filter_ == FileDialogFilter::GAME_CONFIG) &&
			    mode_ != FileDialogMode::OPEN &&
			    mode_ != FileDialogMode::SELECT_DIRECTORY &&
			    std::filesystem::exists(std::filesystem::symlink_status(failedProject_, saveStatus))) {
				ui.SameLine();
				if (ui.Button("Suggest Name")) {
					try {
						const auto path = failedProject_.parent_path() / SuggestSaveName(failedProject_);
						fileName_ = PathUtils::ToUtf8(path);
						failedProject_.clear();
						failedProjectLocationError_.clear();
						error_.clear();
					} catch (const std::exception &error) {
						failedProjectLocationError_ = error.what();
					}
				}
			}
			if (choosingProject) {
				ui.SameLine();
				if (ui.Button("Retry Failed Project")) {
					folderBeforeRetry = directoryText_;
					openFolder = failedProject_;
					directoryText_ = PathUtils::ToUtf8(openFolder);
				}
			}
			if (!failedProjectLocationError_.empty()) {
				ImGui::TextWrapped("%s", failedProjectLocationError_.c_str());
			}
		}

		if (choosingProject && offerPreviousProjectSession_) {
			ui.Checkbox("Restore the previous scene and Assets folder",
			            restorePreviousProjectSession_);
		}

		ui.Separator();

		const char *button = filter_ == FileDialogFilter::PROJECT ? "Open Project"
		                     : filter_ == FileDialogFilter::GAME_EXPORT ? "Open Game Export"
		                     : mode_ == FileDialogMode::SELECT_DIRECTORY
		                         ? "Select Folder"
		                     : mode_ == FileDialogMode::OPEN      ? "Open"
		                     : mode_ == FileDialogMode::SAVE_COPY ? "Save Copy"
		                                                          : "Save";

		const bool submitted = ui.Button(button);
		if (!openFile.empty()) {
			fileName_ = PathUtils::ToUtf8(openFile);
		}
		if ((submitted || fileSubmitted || !openFile.empty() || !openFolder.empty() ||
		     (folderSubmitted && mode_ == FileDialogMode::SELECT_DIRECTORY)) &&
		    AcceptSelection()) {
			result = FileDialogResult::ACCEPTED;
		} else if (folderBeforeRetry) {
			directoryText_.swap(*folderBeforeRetry);
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

std::string_view FileDialog::GameExportOpenProblem(const std::filesystem::path &directory) {
	if (directory.empty())
		return "Choose a game export folder.";

	std::error_code error;
	const auto folder = std::filesystem::status(directory, error);
	if (folder.type() == std::filesystem::file_type::not_found)
		return "The export folder was not found.";
	if (error)
		return "Could not access the export folder.";
	if (!std::filesystem::is_directory(folder))
		return "The export path is not a folder.";

	const bool hasGame = std::filesystem::is_regular_file(directory / L"Game.exe", error);
	const bool hasConfig = std::filesystem::is_regular_file(directory / L"game.json", error);
	if (!hasGame && !hasConfig)
		return "Game.exe and game.json are missing or are not regular files.";
	if (!hasGame)
		return "Game.exe is missing or is not a regular file.";
	if (!hasConfig)
		return "game.json is missing or is not a regular file.";
	return {};
}

void FileDialog::Refresh(bool clearError) {
	entries_.clear();
	if (clearError) {
		error_.clear();
	}

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
					const bool openableFolder =
					    (filter_ == FileDialogFilter::PROJECT &&
					     std::filesystem::is_regular_file(entry.path() / L"project.json", typeError)) ||
					    (filter_ == FileDialogFilter::GAME_EXPORT &&
					     GameExportOpenProblem(entry.path()).empty());
					entries_.push_back({entry.path(), true, openableFolder});
				} else if (mode_ != FileDialogMode::SELECT_DIRECTORY &&
				           entry.is_regular_file(typeError) && !typeError &&
				           (filter_ == FileDialogFilter::MODEL
				                ? PathUtils::IsModelFile(entry.path())
				            : filter_ == FileDialogFilter::IMAGE
				                ? PathUtils::IsImageFile(entry.path())
				            : filter_ == FileDialogFilter::AUDIO
				                ? PathUtils::HasExtension(entry.path(), ".wav")
				            : filter_ == FileDialogFilter::FONT
				                ? PathUtils::IsFontFile(entry.path())
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

void FileDialog::SetProjectScenes(
    const std::filesystem::path &root,
    std::span<const std::filesystem::path> paths, bool searchProject) {
	projectRoot_ = root;
	UpdateProjectScenes(paths);
	searchProjectScenes_ = searchProject && !projectRoot_.empty();
	if (searchProjectScenes_ && !fileName_.empty()) {
		fileName_ = PathUtils::ToUtf8(directory_ / PathUtils::FromUtf8(fileName_));
	}
}

void FileDialog::UpdateProjectScenes(std::span<const std::filesystem::path> paths) {
	projectSceneRefreshError_.clear();
	projectScenes_.clear();
	projectScenes_.reserve(paths.size());
	for (const auto &path : paths) {
		projectScenes_.push_back({PathUtils::ResolveFromExecutable(path), false});
	}
}

void FileDialog::SetProjectSceneRefreshError(std::string error) {
	projectSceneRefreshError_ = std::move(error);
}

bool FileDialog::TakeProjectSceneRefreshRequest() {
	return std::exchange(projectSceneRefreshRequested_, false);
}

std::optional<bool> FileDialog::TakeProjectSearchChange() {
	return std::exchange(projectSearchChange_, std::nullopt);
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
		searchProjectScenes_ = false;

		if (mode_ == FileDialogMode::OPEN) {
			fileName_.clear();
		}

		Refresh();
	} catch (const std::exception &error) {
		error_ = error.what();
	}
}

std::filesystem::path FileDialog::ReadFilePath() const {
	const auto input = PathUtils::FromPathInput(fileName_);
	const bool absoluteInput = input.is_absolute();
	auto name = absoluteInput ? input.filename() : input;

	if (!PathUtils::IsValidFileName(name)) {
		throw std::invalid_argument("Enter a valid file name.");
	}

	if (mode_ != FileDialogMode::OPEN && !name.has_extension()) {
		name += filter_ == FileDialogFilter::GAME_CONFIG ? ".json" : ".scene.json";
	}

	if (!PathUtils::IsValidFileName(name)) {
		throw std::invalid_argument("The complete file name is too long or invalid.");
	}

	if (filter_ == FileDialogFilter::MODEL   ? !PathUtils::IsModelFile(name)
	    : filter_ == FileDialogFilter::IMAGE ? !PathUtils::IsImageFile(name)
	    : filter_ == FileDialogFilter::AUDIO ? !PathUtils::HasExtension(name, ".wav")
	    : filter_ == FileDialogFilter::FONT ? !PathUtils::IsFontFile(name)
	                                         : !PathUtils::HasExtension(name, ".json")) {
		throw std::invalid_argument(
		    filter_ == FileDialogFilter::MODEL   ? "Choose a GLB, glTF or OBJ model."
		    : filter_ == FileDialogFilter::IMAGE ? "Choose a PNG, JPEG, BMP, TIFF or GIF image."
		    : filter_ == FileDialogFilter::AUDIO ? "Choose a WAV audio file."
		    : filter_ == FileDialogFilter::FONT ? "Choose a TTF or OTF font file."
		                                         : "Use a .json file name.");
	}

	// ファイル名だけなら、Goを押す前でもFolder欄の場所を使う。
	const auto folder = absoluteInput ? input.parent_path() : ReadFolderInput();
	if (!folder.is_absolute() || folder.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument("Enter an absolute folder path.");
	}
	return folder / name;
}

bool FileDialog::AcceptSelection() {
	overwriteConfirmed_ = false;
	const bool choosingJson = mode_ != FileDialogMode::SELECT_DIRECTORY &&
	                          (filter_ == FileDialogFilter::SCENE || filter_ == FileDialogFilter::GAME_CONFIG);
	if (choosingJson) {
		failedProject_.clear();
		failedProjectLocationError_.clear();
	}
	try {
		error_.clear();

		if (mode_ == FileDialogMode::SELECT_DIRECTORY) {
			auto selected = ReadDirectory();
			selectedPath_.swap(selected);
			return true;
		}

		const auto path = ReadFilePath();
		if (choosingJson) {
			// 親フォルダが消えていても、入力パスから最寄りの場所を案内できるようにする。
			failedProject_ = path;
		}
		const auto folder = std::filesystem::canonical(path.parent_path());
		if (!std::filesystem::is_directory(folder)) {
			throw std::invalid_argument("Choose an existing folder.");
		}
		auto candidate = folder / path.filename();

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
		if (choosingJson) {
			failedProject_.clear();
		}

		if (mode_ != FileDialogMode::OPEN && exists) {
			if (PathUtils::FromPathInput(fileName_).is_absolute()) {
				// 別名を提案するときも、貼り付けたパスの親フォルダを使う。
				ChangeDirectory(selectedPath_.parent_path());
				fileName_ = PathUtils::ToUtf8(selectedPath_.filename());
			}
			destinationExists_ = true;
			return false;
		}

		return true;
	} catch (const std::exception &error) {
		error_ = error.what();
		return false;
	}
}

std::filesystem::path FileDialog::ReadDirectory() const {
	const auto directory = ReadFolderInput();

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

std::filesystem::path FileDialog::ReadFolderInput() const {
	auto path = PathUtils::FromPathInput(directoryText_);
	const auto filename = path.filename();
	const auto named = [&](const wchar_t *expected) {
		return CompareStringOrdinal(filename.c_str(), -1, expected, -1, TRUE) ==
		       CSTR_EQUAL;
	};
	const bool isManifest = filter_ == FileDialogFilter::PROJECT && named(L"project.json");
	const bool isExportFile = filter_ == FileDialogFilter::GAME_EXPORT &&
	                          (named(L"Game.exe") || named(L"game.json"));
	if (path.is_absolute() && (isManifest || isExportFile) &&
	    std::filesystem::is_regular_file(path)) {
		path = path.parent_path();
	}
	return path;
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
