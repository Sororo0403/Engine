#include "panels/AssetsPanel.h"

#include "assets/AssetDatabase.h"
#include "assets/AssetDragDrop.h"
#include "assets/AssetFolders.h"
#include "core/Logger.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include "platform/windows/StringConversion.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <exception>
#include <format>
#include <imgui.h>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

void AssetsPanel::RequestReveal(const std::filesystem::path &path) {
	revealPath_ = path;
}

bool AssetsPanel::RevealIndexedAsset(const std::filesystem::path &path,
                                     const AssetDatabase &assets) {
	if (!SelectAsset(path, assets)) {
		return false;
	}
	focusRequested_ = true;
	return true;
}

void AssetsPanel::RelocateClipboard(
    std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves) {
	for (auto &path : clipboardItems_) {
		path = PathUtils::AfterMove(path, moves);
	}
}

bool AssetsPanel::ValidateClipboard() {
	const bool missing = std::ranges::any_of(clipboardItems_, [](const auto &path) {
		std::error_code error;
		const auto status = std::filesystem::symlink_status(path, error);
		return status.type() == std::filesystem::file_type::not_found;
	});
	if (!missing) {
		return true;
	}

	clipboardItems_.clear();
	clipboardRoot_.clear();
	clipboardCut_ = false;
	revealMessage_ = "A copied or cut item is missing. Select the items again.";
	return false;
}

void AssetsPanel::SetAssetType(AssetType type) {
	if (type_ == type) {
		return;
	}

	type_ = type;
	selectedPath_.clear();
	selectedItems_.clear();
	selectionAnchor_.clear();
	sceneOpenPath_.clear();
	gameSettingsPath_.clear();
	sceneAppendPath_.clear();
	prefabAppendPath_.clear();
	instanceSelectionPath_.clear();
	modelReloadPath_.clear();
	recyclePaths_.clear();
	referencePaths_.clear();
	renamePath_.clear();
	duplicatePaths_.clear();
	moveRequest_ = {};
	meshReplacementPath_.clear();
	textureReloadPath_.clear();
	textureUsageSelectionPath_.clear();
	textureSelectionPath_.clear();
	textureClearRequested_ = false;
	textureImportRequested_ = false;
	audioImportRequested_ = false;
	modelImportRequested_ = false;
	folderCreateRequested_ = false;
	prefabCreateRequested_ = false;
	scriptCreateRequested_ = false;
	scriptEditPath_.clear();
	clipEditPath_.clear();
	clipCreateRequested_ = false;
	texturePreviewPath_.clear();
	audioPreviewPath_.clear();
	revealMessage_.clear();
	scrollToSelected_ = false;
	scrollToTop_ = true;
}

void AssetsPanel::OpenDirectory(std::filesystem::path directory) {
	currentDirectory_.swap(directory);
	selectedPath_.clear();
	selectedItems_.clear();
	selectionAnchor_.clear();
	filter_.clear();
	revealMessage_.clear();
	scrollToSelected_ = false;
	scrollToTop_ = true;
}

void AssetsPanel::RestoreDirectory(std::filesystem::path folder,
                                   const AssetDatabase &assets) {
	const auto &root = assets.GetRootDirectory();
	while (!folder.empty() &&
	       std::ranges::none_of(assets.GetDirectories(), [&](const auto &directory) {
		       return directory.lexically_relative(root) == folder;
	       })) {
		auto parent = folder.parent_path();
		if (parent == folder) {
			folder.clear();
			break;
		}
		folder = std::move(parent);
	}
	if (folder != currentDirectory_) {
		OpenDirectory(std::move(folder));
	}
}

bool AssetsPanel::SelectAsset(const std::filesystem::path &path,
                              const AssetDatabase &assets) {
	if (path.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument("Invalid asset path.");
	}

	const auto target = PathUtils::ResolveFromExecutable(path);
	const auto directory = PathUtils::GetExecutableDirectory();

	if (directory.empty()) {
		throw std::runtime_error("実行ファイルのフォルダを取得できません。");
	}

	std::error_code error;

	if (!std::filesystem::is_regular_file(target, error)) {
		return false;
	}

	const auto select = [&](const auto &paths, AssetType type, bool aliases) {
		const auto found = std::find_if(
		    paths.begin(), paths.end(), [&](const auto &candidate) {
			    if (!aliases) {
				    return (directory / candidate).lexically_normal() == target;
			    }

			    std::error_code equivalentError;
			    return std::filesystem::equivalent(directory / candidate,
			                                       target, equivalentError);
		    });

		if (found == paths.end()) {
			return false;
		}

		auto selected = *found;
		auto folder = selected.lexically_relative(assets.GetRootDirectory())
		                  .parent_path();
		SetAssetType(type);
		selectedPath_.swap(selected);
		selectedItems_ = {selectedPath_};
		selectionAnchor_ = selectedPath_;
		currentDirectory_.swap(folder);
		filter_.clear();
		scrollToSelected_ = true;
		scrollToTop_ = false;
		return true;
	};

	// 全種類の通常パスを先に探し、別名の場合だけファイルの照合を行う。
	for (const bool aliases : {false, true}) {
		if (select(assets.GetAnimationClips(), AssetType::ANIMATION_CLIP, aliases) ||
		    select(assets.GetScripts(), AssetType::SCRIPT, aliases) ||
		    select(assets.GetModels(), AssetType::MODEL, aliases) ||
		    select(assets.GetTextures(), AssetType::TEXTURE, aliases) ||
		    select(assets.GetAudioClips(), AssetType::AUDIO, aliases) ||
		    select(assets.GetScenes(), AssetType::SCENE, aliases) ||
		    select(assets.GetPrefabs(), AssetType::PREFAB, aliases) ||
		    select(assets.GetGameConfigurations(), AssetType::GAME_SETTINGS,
		           aliases)) {
			return true;
		}
	}

	return false;
}

std::filesystem::path
AssetsPanel::Draw(const ImGuiSystem &ui, AssetDatabase &assets,
                  bool canSetTexture, bool canEditAssets, bool canReplaceMesh,
                  std::span<const TextureSlotChoice> textureSlots,
                  bool canCreatePrefab) {
	sceneOpenPath_.clear();
	gameSettingsPath_.clear();
	sceneAppendPath_.clear();
	prefabAppendPath_.clear();
	instanceSelectionPath_.clear();
	modelReloadPath_.clear();
	recyclePaths_.clear();
	renamePath_.clear();
	duplicatePaths_.clear();
	moveRequest_ = {};
	pasteRequest_ = {};
	meshReplacementPath_.clear();
	textureReloadPath_.clear();
	textureUsageSelectionPath_.clear();
	textureSelectionPath_.clear();
	textureClearRequested_ = false;
	textureImportRequested_ = false;
	audioImportRequested_ = false;
	modelImportRequested_ = false;
	folderCreateRequested_ = false;
	prefabCreateRequested_ = false;
	scriptCreateRequested_ = false;
	scriptEditPath_.clear();
	clipEditPath_.clear();
	clipCreateRequested_ = false;
	texturePreviewPath_.clear();
	audioPreviewPath_.clear();

	const bool focusWindow = focusRequested_ || !revealPath_.empty() ||
	                         directoryOpenRequested_;

	if (focusWindow) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
	}

	const bool windowVisible = ui.BeginWindow("Assets");

	if (focusWindow) {
		ui.FocusWindow("Assets");
	}

	if (!windowVisible) {
		ui.EndWindow();
		return {};
	}
	focusRequested_ = false;

	std::filesystem::path requestedPath;

	if (ui.Button("Refresh")) {
		revealMessage_.clear();
		if (assets.Refresh()) {
			RestoreDirectory(currentDirectory_, assets);
			ValidateClipboard();
		}
	}

	if (!revealPath_.empty()) {
		try {
			if (SelectAsset(revealPath_, assets) ||
			    (assets.Refresh() && SelectAsset(revealPath_, assets))) {
				revealMessage_.clear();
			} else if (assets.GetError().empty()) {
				revealMessage_ = "Asset is not in the indexed Assets folder.";
			} else {
				revealMessage_ = "Could not locate the asset because Assets could not be refreshed.";
			}
		} catch (const std::exception &error) {
			Logger::Error(
			    std::format("AssetsPanel::RequestReveal: {}", error.what()));
			revealMessage_ =
			    "Could not locate the asset. See the log for details.";
		}

		revealPath_.clear();
	}

	const auto &directories = assets.GetDirectories();

	if (directoryOpenRequested_) {
		try {
			const auto target = AssetFolders::Resolve(assets.GetRootDirectory(),
			                                          requestedDirectory_);
			std::filesystem::path selected;

			if (!PathUtils::IsSameFile(target, assets.GetRootDirectory())) {
				const auto found = std::find_if(
				    directories.begin(), directories.end(),
				    [&](const auto &directory) {
					    return PathUtils::IsSameFile(target, directory);
				    });

				if (found == directories.end()) {
					throw std::runtime_error(
					    "Folder is not in the current Assets index.");
				}

				selected = found->lexically_relative(assets.GetRootDirectory());
			}

			OpenDirectory(std::move(selected));
		} catch (const std::exception &error) {
			Logger::Error(
			    std::format("AssetsPanel::OpenDirectory: {}", error.what()));
			revealMessage_ =
			    "Could not open the Assets folder. Refresh and try again.";
		}

		directoryOpenRequested_ = false;
		requestedDirectory_.clear();
	}

	RestoreDirectory(currentDirectory_, assets);

	std::filesystem::path draggedPath;
	std::filesystem::path dropDirectory;
	const auto acceptFolder = [&](const std::filesystem::path &directory) {
		if (!canEditAssets || !assets.GetRootDirectory().is_absolute()) {
			return;
		}

		auto source = AssetDragDrop::AcceptAssetTarget();

		if (!source.empty()) {
			draggedPath = std::move(source);
			dropDirectory = directory;
		}
	};

	ui.SameLine();
	ui.BeginDisabled(currentDirectory_.empty() && filter_.empty());

	if (ui.Button("Root")) {
		OpenDirectory({});
	}

	acceptFolder(assets.GetRootDirectory());
	ui.EndDisabled();
	ui.SameLine();
	ui.BeginDisabled(currentDirectory_.empty());

	if (ui.Button("Up")) {
		OpenDirectory(currentDirectory_.parent_path());
	} else if (!currentDirectory_.empty()) {
		acceptFolder(assets.GetRootDirectory() / currentDirectory_.parent_path());
	}

	ui.EndDisabled();
	ui.SameLine();
	ui.BeginDisabled(!canEditAssets);

	if (ui.Button("New Folder")) {
		folderCreateRequested_ = true;
	}

	ui.EndDisabled();
	ui.BeginDisabled(!canEditAssets);

	ui.BeginDisabled(!canCreatePrefab);

	if (ui.Button("Create Prefab from Selection")) {
		prefabCreateRequested_ = true;
	}

	ui.EndDisabled();

	if (ui.Button("Create Script"))
		scriptCreateRequested_ = true;
	if (ui.Button("Create Animation Clip"))
		clipCreateRequested_ = true;
	if (ui.Button("Import Texture")) {
		textureImportRequested_ = true;
	}

	ui.SameLine();

	if (ui.Button("Import Model")) {
		modelImportRequested_ = true;
	}

	ui.SameLine();

	if (ui.Button("Import Audio")) {
		audioImportRequested_ = true;
	}

	ui.EndDisabled();
	ui.Text(PathUtils::ToUtf8(assets.GetRootDirectory() / currentDirectory_));
	ImGui::SetNextItemWidth(160.0f);

	const auto typeLabel = [](AssetType type) {
		switch (type) {
		case AssetType::ANIMATION_CLIP:
			return "Animation Clips";
		case AssetType::SCRIPT:
			return "Scripts";
		case AssetType::MODEL:
			return "Models";
		case AssetType::TEXTURE:
			return "Textures";
		case AssetType::AUDIO:
			return "Audio";
		case AssetType::SCENE:
			return "Scenes";
		case AssetType::PREFAB:
			return "Prefabs";
		case AssetType::GAME_SETTINGS:
			return "Game Settings";
		}

		return "";
	};

	if (ImGui::BeginCombo("Asset Type", typeLabel(type_))) {
		for (const auto type :
		     {AssetType::MODEL, AssetType::TEXTURE, AssetType::AUDIO,
		      AssetType::SCENE, AssetType::PREFAB, AssetType::GAME_SETTINGS,
		      AssetType::SCRIPT, AssetType::ANIMATION_CLIP}) {
			if (ui.Selectable(typeLabel(type), type_ == type)) {
				SetAssetType(type);
			}
		}

		ImGui::EndCombo();
	}

	if (ui.InputText("Search", filter_)) {
		scrollToTop_ = true;
		// 検索で隠れたアセットを誤って操作しないよう選択を解除する
		selectedPath_.clear();
		selectedItems_.clear();
		selectionAnchor_.clear();
		scrollToSelected_ = false;
		revealMessage_.clear();
	}

	if (type_ != AssetType::AUDIO) {
		ImGui::TextDisabled("%s", filter_.empty() ? "Double-click a folder to open it."
		                                          : "Searching all folders.");
	}

	const auto &entries = type_ == AssetType::ANIMATION_CLIP ? assets.GetAnimationClips()
	                      : type_ == AssetType::SCRIPT ? assets.GetScripts()
	                      : type_ == AssetType::MODEL   ? assets.GetModels()
	                      : type_ == AssetType::TEXTURE ? assets.GetTextures()
	                      : type_ == AssetType::AUDIO   ? assets.GetAudioClips()
	                      : type_ == AssetType::SCENE   ? assets.GetScenes()
	                      : type_ == AssetType::PREFAB
	                          ? assets.GetPrefabs()
	                          : assets.GetGameConfigurations();

	const auto search = StringConversion::Utf8ToWide(filter_);
	const auto visibleFile = [&](const std::filesystem::path &relative) {
		if (filter_.empty()) {
			return relative.parent_path() == currentDirectory_;
		}
		const auto path = relative.generic_wstring();
		return !search.empty() &&
		       FindStringOrdinal(FIND_FROMSTART, path.c_str(),
		                         static_cast<int>(path.size()), search.c_str(),
		                         static_cast<int>(search.size()), TRUE) >= 0;
	};
	std::vector<std::filesystem::path> visible;

	if (filter_.empty()) {
		for (const auto &directory : directories) {
			if (directory.lexically_relative(assets.GetRootDirectory()).parent_path() ==
			    currentDirectory_) {
				visible.push_back(directory);
			}
		}
	}

	for (const auto &path : entries) {
		if (visibleFile(path.lexically_relative(assets.GetRootDirectory()))) {
			visible.push_back(path);
		}
	}

	const auto isSelected = [&](const std::filesystem::path &path) {
		return std::ranges::find(selectedItems_, path) != selectedItems_.end();
	};
	const auto syncSingle = [&] {
		selectedPath_ = selectedItems_.size() == 1 &&
		                        std::ranges::find(entries, selectedItems_.front()) != entries.end()
		                    ? selectedItems_.front()
		                    : std::filesystem::path{};
	};
	std::erase_if(selectedItems_, [&](const auto &path) {
		return std::ranges::find(visible, path) == visible.end();
	});
	if (std::ranges::find(visible, selectionAnchor_) == visible.end()) {
		selectionAnchor_ = selectedItems_.empty() ? std::filesystem::path{}
		                                         : selectedItems_.back();
	}
	syncSingle();

	const auto selectRow = [&](const std::filesystem::path &path, bool modifiers) {
		const auto &io = ImGui::GetIO();
		auto anchor = std::ranges::find(visible, selectionAnchor_);

		auto last = std::ranges::find(visible, path);

		if (modifiers && io.KeyShift && anchor != visible.end() &&
		    last != visible.end()) {
			if (last < anchor) {
				std::swap(anchor, last);
			}
			selectedItems_.assign(anchor, std::next(last));
		} else {
			if (modifiers && io.KeyCtrl) {
				if (!std::erase(selectedItems_, path)) {
					selectedItems_.push_back(path);
				}
			} else {
				selectedItems_ = {path};
			}
			selectionAnchor_ = path;
		}

		syncSingle();
		scrollToSelected_ = false;
		revealMessage_.clear();
	};
	const auto selectContext = [&](const std::filesystem::path &path) {
		if (!isSelected(path)) {
			selectRow(path, false);
		}
	};
	const auto &root = assets.GetRootDirectory();
	const bool clipboardEnabled = canEditAssets && root.is_absolute();

	if (!clipboardItems_.empty() && clipboardRoot_ != root) {
		clipboardItems_.clear();
		clipboardCut_ = false;
	}

	const auto currentFolder = [&] {
		return currentDirectory_.empty() ? root : root / currentDirectory_;
	};
	const auto storeClipboard = [&](bool cut) {
		clipboardItems_ = selectedItems_;
		clipboardRoot_ = root;
		clipboardCut_ = cut;
	};
	const auto requestPaste = [&](const std::filesystem::path &folder) {
		if (ValidateClipboard()) {
			pasteRequest_ = {clipboardItems_, folder};
		}
	};
	const auto drawPaste = [&](const char *label, const std::filesystem::path &folder) {
		if (ui.MenuItem(label, "Ctrl+V", clipboardEnabled && !clipboardItems_.empty())) {
			requestPaste(folder);
		}
	};
	const auto drawClipboardMenu = [&] {
		const bool enabled = clipboardEnabled && !selectedItems_.empty();

		if (ui.MenuItem("Copy", "Ctrl+C", enabled)) {
			storeClipboard(false);
		}

		if (ui.MenuItem("Cut", "Ctrl+X", enabled)) {
			storeClipboard(true);
		}
	};
	const auto drawBackgroundMenu = [&] {
		if (clipboardEnabled &&
		    ImGui::BeginPopupContextWindow("AssetsBackground",
		                                   ImGuiPopupFlags_MouseButtonRight |
		                                       ImGuiPopupFlags_NoOpenOverItems)) {
			drawPaste("Paste", currentFolder());
			ImGui::EndPopup();
		}
	};

	const auto drawLocationMenu = [&](const std::filesystem::path &path) {
		if (ui.MenuItem("Show in Explorer")) {
			ShellUtils::RevealInExplorer(path, revealMessage_);
		}
		if (ui.MenuItem("Copy Full Path")) {
			try {
				const auto absolute = PathUtils::ResolveFromExecutable(path);
				ImGui::SetClipboardText(PathUtils::ToUtf8(absolute).c_str());
				revealMessage_.clear();
			} catch (const std::exception &error) {
				revealMessage_ = error.what();
			}
		}
	};

	if (!assets.GetError().empty()) {
		ui.Text("Assets could not be updated. Showing the last successful listing.");
		ImGui::TextWrapped("%s", assets.GetError().c_str());
	}

	if (!revealMessage_.empty()) {
		ImGui::TextWrapped("%s", revealMessage_.c_str());
	}

	const auto contentSize = ui.GetContentSize();
	const float footerHeight =
	    (type_ == AssetType::AUDIO
	         ? ImGui::GetFrameHeightWithSpacing()
	         : ImGui::GetTextLineHeightWithSpacing() +
	               ImGui::GetFrameHeightWithSpacing() *
	                   (type_ == AssetType::TEXTURE ? 6.0f : 4.0f)) +
	    ImGui::GetStyle().ItemSpacing.y;
	const float listHeight = std::max(1.0f, contentSize.y - footerHeight);

	const bool drawEntries = ui.BeginChild("AssetEntries", 0.0f, listHeight);
	std::filesystem::path nextDirectory;
	bool openRequested = false;

	if (drawEntries) {
		if (scrollToTop_) {
			ImGui::SetScrollY(0.0f);
			scrollToTop_ = false;
		}

		bool anyVisible = false;

		if (filter_.empty()) {
			for (const auto &directory : directories) {
				const auto relative =
				    directory.lexically_relative(assets.GetRootDirectory());

				if (relative.parent_path() != currentDirectory_) {
					continue;
				}

				anyVisible = true;
				const auto label = std::format(
				    "[Folder] {}", PathUtils::ToUtf8(relative.filename()));
				ImGui::PushID(PathUtils::ToUtf8(directory).c_str());

				if (ui.Selectable(label.c_str(), isSelected(directory))) {
					selectRow(directory, true);
				}
				if (ImGui::IsItemHovered() &&
				    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
				    !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) {
					nextDirectory = relative;
					openRequested = true;
				}

				acceptFolder(directory);

				const bool canMove = canEditAssets &&
				                     assets.GetRootDirectory().is_absolute();
				if (canMove) {
					if (AssetDragDrop::DrawSource(directory, AssetDragDrop::Kind::FOLDER)) {
						selectContext(directory);
					}
				}

				if (ImGui::BeginPopupContextItem()) {
					selectContext(directory);
					drawLocationMenu(directory);
					if (canMove) {
						ImGui::Separator();
						drawClipboardMenu();
						drawPaste("Paste into Folder", directory);
						ImGui::Separator();

						if (ui.MenuItem("Rename...", "F2", selectedItems_.size() == 1)) {
							renamePath_ = directory;
						}

						if (ui.MenuItem("Duplicate", "Ctrl+D")) {
							duplicatePaths_ = selectedItems_;
						}

						if (ui.MenuItem("Find References...")) {
							referencePaths_ = selectedItems_;
						}

						if (ui.MenuItem("Move to Recycle Bin...", "Delete")) {
							recyclePaths_ = selectedItems_;
						}

					}
					ImGui::EndPopup();
				}

				ImGui::PopID();
			}
		}

		for (std::size_t i = 0; i < entries.size(); ++i) {
			const auto &assetPath = entries[i];

			// パネル内では、アセットフォルダからのパスを表示する
			const auto displayPath =
			    assetPath.lexically_relative(assets.GetRootDirectory());

			if (!visibleFile(displayPath)) {
				continue;
			}

			anyVisible = true;

			ui.PushID(static_cast<int>(i));
			const auto label = filter_.empty()
			                       ? PathUtils::ToUtf8(displayPath.filename())
			                       : PathUtils::ToUtf8(displayPath);

			const bool selected =
			    ui.Selectable(label.c_str(), isSelected(assetPath));
			// 通常の選択確定はボタンを離した時。ダブルクリックの押下は別に受け取る。
			const bool doubleClicked =
			    ImGui::IsItemHovered() &&
			    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
			    !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift;
			if (selected || doubleClicked) {
				selectRow(assetPath, !doubleClicked);

				if (type_ == AssetType::SCRIPT && canEditAssets &&
				    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					scriptEditPath_ = assetPath;
				if (type_ == AssetType::ANIMATION_CLIP && canEditAssets && doubleClicked)
					clipEditPath_ = assetPath;
				if (type_ == AssetType::SCENE && canEditAssets &&
				    doubleClicked) {
					sceneOpenPath_ = assetPath;
				}

				if (type_ == AssetType::GAME_SETTINGS && canEditAssets &&
				    doubleClicked) {
					gameSettingsPath_ = assetPath;
				}

				if (type_ == AssetType::TEXTURE && canSetTexture &&
				    textureSlot_ >= 0 && doubleClicked) {
					textureSelectionPath_ = assetPath;
				}
			}

			if (ImGui::BeginPopupContextItem()) {
				selectContext(assetPath);
				if (!filter_.empty() && ui.MenuItem("Show in Containing Folder")) {
					RequestReveal(assetPath);
				}
				drawLocationMenu(assetPath);
				ImGui::Separator();
				drawClipboardMenu();
				drawPaste("Paste", currentFolder());
				ImGui::Separator();

				if (ui.MenuItem("Rename...", "F2",
				                selectedItems_.size() == 1 && canEditAssets &&
				                    assets.GetRootDirectory().is_absolute())) {
					renamePath_ = assetPath;
				}

				if (ui.MenuItem("Duplicate", "Ctrl+D",
				                canEditAssets &&
				                    assets.GetRootDirectory().is_absolute())) {
					duplicatePaths_ = selectedItems_;
				}

				if (ui.MenuItem("Find References...", nullptr,
				                canEditAssets &&
				                    assets.GetRootDirectory().is_absolute())) {
					referencePaths_ = selectedItems_;
				}

				if (ui.MenuItem("Move to Recycle Bin...", "Delete",
				                canEditAssets &&
				                    assets.GetRootDirectory().is_absolute())) {
					recyclePaths_ = selectedItems_;
				}

				if (type_ == AssetType::PREFAB &&
				    ui.MenuItem("Add Prefab", nullptr, canEditAssets)) {
					prefabAppendPath_ = assetPath;
				}

				if (type_ == AssetType::SCENE &&
				    ui.MenuItem("Add Objects", nullptr, canEditAssets)) {
					sceneAppendPath_ = assetPath;
				}

				if (type_ == AssetType::SCENE &&
				    ui.MenuItem("Open Scene", nullptr, canEditAssets)) {
					sceneOpenPath_ = assetPath;
				}

				if (type_ == AssetType::TEXTURE &&
				    ui.MenuItem("Assign Texture to Selection", nullptr,
				                canSetTexture && textureSlot_ >= 0)) {
					textureSelectionPath_ = assetPath;
				}

				if (type_ == AssetType::TEXTURE &&
				    ui.MenuItem("Reload Texture", nullptr, canEditAssets)) {
					textureReloadPath_ = assetPath;
				}

				if (type_ == AssetType::MODEL &&
				    ui.MenuItem("Replace Mesh of Selection", nullptr,
				                canEditAssets && canReplaceMesh)) {
					meshReplacementPath_ = assetPath;
				}

				if (type_ == AssetType::MODEL &&
				    ui.MenuItem("Reload Model", nullptr, canEditAssets)) {
					modelReloadPath_ = assetPath;
				}

				if (type_ == AssetType::GAME_SETTINGS &&
				    ui.MenuItem("Load Game Settings", nullptr, canEditAssets)) {
					gameSettingsPath_ = assetPath;
				}

				if ((type_ == AssetType::MODEL ||
				     type_ == AssetType::TEXTURE) &&
				    ui.MenuItem("Select Instances", nullptr, true)) {
					if (type_ == AssetType::MODEL) {
						instanceSelectionPath_ = assetPath;
					} else {
						textureUsageSelectionPath_ = assetPath;
					}
				}
				ImGui::EndPopup();
			}

			if (scrollToSelected_ && selectedPath_ == assetPath) {
				ImGui::SetScrollHereY(0.5f);
				scrollToSelected_ = false;
			}

			if ((type_ != AssetType::GAME_SETTINGS || canEditAssets) &&
			    AssetDragDrop::DrawSource(
			        assetPath,
			        type_ == AssetType::TEXTURE  ? AssetDragDrop::Kind::TEXTURE
			        : type_ == AssetType::SCRIPT ? AssetDragDrop::Kind::SCRIPT
			        : type_ == AssetType::MODEL  ? AssetDragDrop::Kind::MODEL
			        : type_ == AssetType::AUDIO  ? AssetDragDrop::Kind::AUDIO
			        : type_ == AssetType::PREFAB ? AssetDragDrop::Kind::PREFAB
			        : type_ == AssetType::SCENE  ? AssetDragDrop::Kind::SCENE
			        : type_ == AssetType::ANIMATION_CLIP ? AssetDragDrop::Kind::ANIMATION_CLIP
			                                    : AssetDragDrop::Kind::GAME_SETTINGS)) {
				selectContext(assetPath);
			}

			ui.PopID();
		}

		if (!anyVisible) {
			ui.Text(filter_.empty() ? "No folders or assets."
			                        : "No matching assets.");
		}

		drawBackgroundMenu();
	}

	ui.EndChild();

	if (openRequested) {
		OpenDirectory(std::move(nextDirectory));
	}

	const auto &io = ImGui::GetIO();

	if (!openRequested &&
	    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
	                           ImGuiFocusedFlags_NoPopupHierarchy) &&
	    !io.AppFocusLost && !ui.IsTextInputActive() && !ImGui::IsAnyItemActive() &&
	    !ImGui::GetDragDropPayload() &&
	    !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
	                                     ImGuiPopupFlags_AnyPopupLevel) &&
	    !io.KeyShift && !io.KeyAlt && !io.KeySuper) {
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
			selectedItems_ = visible;
			selectionAnchor_ = visible.empty() ? std::filesystem::path{} : visible.front();
			syncSingle();
		} else if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			selectedItems_.clear();
			selectionAnchor_.clear();
			scrollToSelected_ = false;
			syncSingle();
		} else if (clipboardEnabled && io.KeyCtrl) {
			const bool copy = ImGui::IsKeyPressed(ImGuiKey_C, false);
			const bool cut = ImGui::IsKeyPressed(ImGuiKey_X, false);

			if (!selectedItems_.empty() && (copy || cut)) {
				storeClipboard(cut);
			}

			if (!clipboardItems_.empty() && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
				requestPaste(currentFolder());
			} else if (!selectedItems_.empty() &&
			           ImGui::IsKeyPressed(ImGuiKey_D, false)) {
				duplicatePaths_ = selectedItems_;
			}
		} else if (clipboardEnabled && !io.KeyCtrl && selectedItems_.size() == 1 &&
		           ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
			renamePath_ = selectedItems_.front();
		} else if (clipboardEnabled && !io.KeyCtrl && !selectedItems_.empty() &&
		           ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
			recyclePaths_ = selectedItems_;
		}
	}

	if (selectedItems_.size() > 1) {
		ui.Text(std::format("{} items selected", selectedItems_.size()));
	}

	if (!selectedPath_.empty() && type_ != AssetType::AUDIO) {
		const auto displayPath =
		    selectedPath_.lexically_relative(assets.GetRootDirectory());

		ui.Text(PathUtils::ToUtf8(displayPath));
	}

	if (type_ == AssetType::MODEL) {
		ui.BeginDisabled(selectedPath_.empty());

		if (ui.Button("Add to Scene")) {
			requestedPath = selectedPath_;
		}

		ui.BeginDisabled(!canEditAssets || !canReplaceMesh);

		if (ui.Button("Replace Mesh of Selection")) {
			meshReplacementPath_ = selectedPath_;
		}

		ui.EndDisabled();

		ui.BeginDisabled(!canEditAssets);

		if (ui.Button("Reload Model")) {
			modelReloadPath_ = selectedPath_;
		}

		ui.EndDisabled();
		ui.EndDisabled();
	} else if (type_ == AssetType::TEXTURE) {
		ui.BeginDisabled(selectedPath_.empty());

		if (ui.Button("Preview Image")) {
			texturePreviewPath_ = selectedPath_;
		}

		ui.BeginDisabled(!canEditAssets);

		if (ui.Button("Reload Texture")) {
			textureReloadPath_ = selectedPath_;
		}

		ui.EndDisabled();

		ui.EndDisabled();
		ImGui::InputInt("Texture Slot", &textureSlot_);

		if (!textureSlots.empty() &&
		    ImGui::BeginCombo("Model Slots",
		                      std::to_string(textureSlot_).c_str())) {
			if (ImGui::BeginTable("TextureSlotChoices", 2,
			                      ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn(
				    "Slot", ImGuiTableColumnFlags_WidthFixed, 40.0f);
				ImGui::TableSetupColumn("Material / Objects");
				ImGui::TableHeadersRow();

				for (const auto &choice : textureSlots) {
					if (choice.index < 0) {
						continue;
					}

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::PushID(choice.index);
					const auto label = std::to_string(choice.index);

					if (ImGui::Selectable(
					        label.c_str(), textureSlot_ == choice.index,
					        ImGuiSelectableFlags_SpanAllColumns)) {
						textureSlot_ = choice.index;
					}

					ImGui::PopID();
					ImGui::TableNextColumn();
					ui.Text(std::format("{} | Used: {}/{}{}", choice.name,
					                    choice.usedBy, choice.selectedCount,
					                    choice.usedBy == 0 ? " [Unused]" : ""));
				}

				ImGui::EndTable();
			}

			ImGui::EndCombo();
		}

		ui.BeginDisabled(!canSetTexture || textureSlot_ < 0);
		ui.BeginDisabled(selectedPath_.empty());

		if (ui.Button("Assign Texture to Selection")) {
			textureSelectionPath_ = selectedPath_;
		}

		ui.EndDisabled();

		if (ui.Button("Clear Texture Slot")) {
			textureSelectionPath_.clear();
			textureClearRequested_ = true;
		}

		ui.EndDisabled();
	} else if (type_ == AssetType::AUDIO) {
		ui.BeginDisabled(selectedPath_.empty() || !canEditAssets);

		if (ui.Button("Preview Audio")) {
			audioPreviewPath_ = selectedPath_;
		}

		ui.EndDisabled();

		if (!selectedPath_.empty()) {
			ui.SameLine();
			ui.Text(PathUtils::ToUtf8(
			    selectedPath_.lexically_relative(assets.GetRootDirectory())));
		}
	}

	if (type_ == AssetType::ANIMATION_CLIP) {
		ui.BeginDisabled(selectedPath_.empty() || !canEditAssets);
		if (ui.Button("Edit Animation Clip"))
			clipEditPath_ = selectedPath_;
		ui.EndDisabled();
	} else if (type_ == AssetType::SCRIPT) {
		ui.BeginDisabled(selectedPath_.empty() || !canEditAssets);
		if (ui.Button("Edit Script"))
			scriptEditPath_ = selectedPath_;
		ui.EndDisabled();
	} else if (type_ == AssetType::SCENE) {
		ui.BeginDisabled(selectedPath_.empty() || !canEditAssets);

		if (ui.Button("Open Scene")) {
			sceneOpenPath_ = selectedPath_;
		}

		if (ui.Button("Add Objects")) {
			sceneAppendPath_ = selectedPath_;
		}

		ui.EndDisabled();
	} else if (type_ == AssetType::PREFAB) {
		ui.BeginDisabled(selectedPath_.empty() || !canEditAssets);

		if (ui.Button("Add Prefab")) {
			prefabAppendPath_ = selectedPath_;
		}

		ui.EndDisabled();
	} else if (type_ == AssetType::GAME_SETTINGS) {
		ui.BeginDisabled(selectedPath_.empty() || !canEditAssets);

		if (ui.Button("Load Game Settings")) {
			gameSettingsPath_ = selectedPath_;
		}

		ui.EndDisabled();
	} else if (type_ != AssetType::AUDIO) {
		ui.BeginDisabled(selectedPath_.empty());

		if (ui.Button("Select Instances")) {
			if (type_ == AssetType::MODEL) {
				instanceSelectionPath_ = selectedPath_;
			} else {
				textureUsageSelectionPath_ = selectedPath_;
			}
		}

		ImGui::SetItemTooltip(
		    "Select all objects using this asset, including hidden objects.");
		ui.EndDisabled();
	}

	drawBackgroundMenu();
	// 上部の移動先も、現在の一覧と選択を確定してから処理する。
	if (!draggedPath.empty() && std::ranges::find(visible, draggedPath) != visible.end()) {
		moveRequest_.sources = isSelected(draggedPath)
		                           ? selectedItems_
		                           : std::vector<std::filesystem::path>{std::move(draggedPath)};
		moveRequest_.directory = std::move(dropDirectory);
	}
	ui.EndWindow();
	return requestedPath;
}

const std::filesystem::path &AssetsPanel::GetInstanceSelectionRequest() const {
	return instanceSelectionPath_;
}

const std::filesystem::path &
AssetsPanel::GetTextureUsageSelectionRequest() const {
	return textureUsageSelectionPath_;
}

const std::filesystem::path &AssetsPanel::GetTextureSelectionRequest() const {
	return textureSelectionPath_;
}

const std::filesystem::path &AssetsPanel::GetTexturePreviewRequest() const {
	return texturePreviewPath_;
}

void AssetsPanel::Clear() {
	recyclePaths_.clear();
	referencePaths_.clear();
	renamePath_.clear();
	duplicatePaths_.clear();
	moveRequest_ = {};
	pasteRequest_ = {};
	clipboardItems_.clear();
	clipboardRoot_.clear();
	clipboardCut_ = false;
	directoryOpenRequested_ = false;
	requestedDirectory_.clear();
	type_ = AssetType::MODEL;
	textureUsageSelectionPath_.clear();
	textureSelectionPath_.clear();
	textureClearRequested_ = false;
	textureImportRequested_ = false;
	audioImportRequested_ = false;
	modelImportRequested_ = false;
	folderCreateRequested_ = false;
	prefabCreateRequested_ = false;
	scriptCreateRequested_ = false;
	scriptEditPath_.clear();
	clipEditPath_.clear();
	clipCreateRequested_ = false;
	texturePreviewPath_.clear();
	audioPreviewPath_.clear();
	selectedPath_.clear();
	selectedItems_.clear();
	selectionAnchor_.clear();
	sceneOpenPath_.clear();
	gameSettingsPath_.clear();
	sceneAppendPath_.clear();
	prefabAppendPath_.clear();
	instanceSelectionPath_.clear();
	modelReloadPath_.clear();
	meshReplacementPath_.clear();
	textureReloadPath_.clear();
	currentDirectory_.clear();
	filter_.clear();
	revealPath_.clear();
	focusRequested_ = false;
	revealMessage_.clear();
	scrollToSelected_ = false;
	scrollToTop_ = true;
}

const std::vector<std::filesystem::path> &AssetsPanel::GetRecycleRequest() const {
	return recyclePaths_;
}

const std::vector<std::filesystem::path> &AssetsPanel::GetReferenceRequest() const {
	return referencePaths_;
}

const std::filesystem::path &AssetsPanel::GetRenameRequest() const {
	return renamePath_;
}

const std::vector<std::filesystem::path> &AssetsPanel::GetDuplicateRequest() const {
	return duplicatePaths_;
}

const AssetsPanel::MoveRequest &AssetsPanel::GetMoveRequest() const {
	return moveRequest_;
}

const AssetsPanel::MoveRequest &AssetsPanel::GetPasteRequest() const {
	return pasteRequest_;
}

bool AssetsPanel::IsCutPaste() const {
	return clipboardCut_;
}

void AssetsPanel::FinishPaste() {
	if (clipboardCut_) {
		clipboardItems_.clear();
		clipboardCut_ = false;
	}
}

std::uint32_t AssetsPanel::GetTextureSlot() const {
	return textureSlot_ < 0 ? 0 : static_cast<std::uint32_t>(textureSlot_);
}

bool AssetsPanel::IsTextureClearRequested() const {
	return textureClearRequested_;
}

bool AssetsPanel::IsTextureImportRequested() const {
	return textureImportRequested_;
}

const std::filesystem::path &AssetsPanel::GetOpenDirectory() const {
	return currentDirectory_;
}

bool AssetsPanel::IsFolderCreateRequested() const {
	return folderCreateRequested_;
}

void AssetsPanel::RequestOpenDirectory(const std::filesystem::path &folder) {
	requestedDirectory_ = folder;
	directoryOpenRequested_ = true;
}

bool AssetsPanel::IsModelImportRequested() const {
	return modelImportRequested_;
}

const std::filesystem::path &AssetsPanel::GetModelReloadRequest() const {
	return modelReloadPath_;
}

const std::filesystem::path &AssetsPanel::GetMeshReplacementRequest() const {
	return meshReplacementPath_;
}

const std::filesystem::path &AssetsPanel::GetTextureReloadRequest() const {
	return textureReloadPath_;
}

const std::filesystem::path &AssetsPanel::GetSceneOpenRequest() const {
	return sceneOpenPath_;
}

const std::filesystem::path &AssetsPanel::GetPrefabAppendRequest() const {
	return prefabAppendPath_;
}

bool AssetsPanel::IsPrefabCreateRequested() const {
	return prefabCreateRequested_;
}

const std::filesystem::path &AssetsPanel::GetSceneAppendRequest() const {
	return sceneAppendPath_;
}

const std::filesystem::path &AssetsPanel::GetGameSettingsRequest() const {
	return gameSettingsPath_;
}

bool AssetsPanel::IsAudioImportRequested() const {
	return audioImportRequested_;
}

const std::filesystem::path &AssetsPanel::GetAudioPreviewRequest() const {
	return audioPreviewPath_;
}
