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
#include <array>
#include <exception>
#include <format>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace {
/// <summary>
/// 貼り付けたパスの引用符と区切りを取り除き、検索に使う文字列へ変換する
/// </summary>
std::wstring NormalizeSearch(std::string_view filter) {
	auto search = StringConversion::Utf8ToWide(filter);
	if (search.size() >= 2 && search.front() == L'"' && search.back() == L'"') {
		search = search.substr(1, search.size() - 2);
	}
	std::ranges::replace(search, L'\\', L'/');
	return search;
}

/// <summary>
/// フルパス検索では末尾の区切りを除いた絶対パスを返す
/// </summary>
std::filesystem::path FullSearchPath(std::wstring_view search) {
	std::filesystem::path path(search);
	if (!path.is_absolute()) {
		return {};
	}
	path = path.lexically_normal();
	return !path.has_filename() && path.has_relative_path() ? path.parent_path()
	                                                       : path;
}

bool IsMissingAsset(const std::filesystem::path &path) {
	if (path.empty()) {
		return false;
	}
	std::error_code error;
	const auto status = std::filesystem::symlink_status(
	    PathUtils::ResolveFromExecutable(path), error);
	return status.type() == std::filesystem::file_type::not_found;
}

/// <summary>
/// フォルダとその中身が同時に選ばれた場合は、操作対象にフォルダだけを残す
/// </summary>
std::vector<std::filesystem::path> SelectedAssetRoots(
    const std::vector<std::filesystem::path> &selection) {
	const auto less = [](const auto &a, const auto &b) {
		return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
	};
	std::set<std::filesystem::path, decltype(less)> selected(less);
	for (const auto &path : selection) {
		selected.insert(path.lexically_normal().make_preferred());
	}

	std::vector<std::filesystem::path> roots;
	roots.reserve(selection.size());
	for (const auto &path : selection) {
		auto parent = path.lexically_normal().make_preferred().parent_path();
		for (; parent.has_relative_path() && !selected.contains(parent);
		     parent = parent.parent_path()) {
		}
		if (!parent.has_relative_path()) {
			roots.push_back(path);
		}
	}
	return roots;
}
} // namespace

void AssetsPanel::ClearNavigationRequests() {
	resultPaths_.clear();
	revealPath_.clear();
	unavailableRevealPath_.clear();
	requestedDirectory_.clear();
	directoryOpenRequested_ = false;
	revealMessage_.clear();
}

void AssetsPanel::RequestReveal(const std::filesystem::path &path, bool keepResults) {
	ClearNavigationRequests();
	previousView_.reset();
	revealPath_ = path;
	keepRevealResults_ = keepResults;
}

void AssetsPanel::RequestSelectResults(std::vector<std::filesystem::path> paths) {
	ClearNavigationRequests();
	resultPaths_ = std::move(paths);
	focusRequested_ = true;
}

bool AssetsPanel::RevealIndexedAsset(const std::filesystem::path &path,
                                     const AssetDatabase &assets) {
	if (!SelectAsset(path, assets)) {
		return false;
	}
	ClearNavigationRequests();
	previousView_.reset();
	focusRequested_ = true;
	return true;
}

void AssetsPanel::RelocatePaths(
    std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves,
    const std::filesystem::path &root) {
	const auto relocateList = [&](auto &paths) {
		for (auto &path : paths) {
			path = PathUtils::AfterMove(path, moves);
		}
	};
	const auto relocateDirectory = [&](const std::filesystem::path &folder) {
		auto relative =
		    PathUtils::AfterMove(root / folder, moves).lexically_relative(root);
		return relative == L"." ? std::filesystem::path{} : relative;
	};
	const auto relocateSearch = [&](std::string &filter) {
		const auto path = FullSearchPath(NormalizeSearch(filter));
		if (!path.empty()) {
			const auto relocated = PathUtils::AfterMove(path, moves);
			if (relocated.native() != path.native()) {
				filter = PathUtils::ToUtf8(relocated);
			}
		}
	};
	relocateList(clipboardItems_);
	relocateList(resultPaths_);
	relocateList(displayedResults_);
	relocateList(selectedItems_);
	selectedPath_ = PathUtils::AfterMove(selectedPath_, moves);
	selectionAnchor_ = PathUtils::AfterMove(selectionAnchor_, moves);
	revealPath_ = PathUtils::AfterMove(revealPath_, moves);
	unavailableRevealPath_ = PathUtils::AfterMove(unavailableRevealPath_, moves);
	currentDirectory_ = relocateDirectory(currentDirectory_);
	relocateSearch(filter_);
	if (previousView_) {
		relocateList(previousView_->results);
		relocateList(previousView_->selection);
		previousView_->anchor = PathUtils::AfterMove(previousView_->anchor, moves);
		previousView_->directory = relocateDirectory(previousView_->directory);
		relocateSearch(previousView_->search);
	}
}

std::optional<AssetsPanel::ResultView> AssetsPanel::CaptureResults(bool includeFolder) const {
	if (!includeFolder && filter_.empty() && displayedResults_.empty()) {
		return std::nullopt;
	}
	return ResultView{filter_, displayedResults_, selectedItems_, currentDirectory_,
	                  selectionAnchor_, type_, searchAllTypes_, searchCurrentFolderOnly_};
}

void AssetsPanel::RemoveMissingPaths() {
	for (auto *paths : {&displayedResults_, &selectedItems_}) {
		std::erase_if(*paths, IsMissingAsset);
	}
	if (IsMissingAsset(selectedPath_)) {
		selectedPath_.clear();
	}
	if (IsMissingAsset(selectionAnchor_)) {
		selectionAnchor_.clear();
	}
	if (previousView_) {
		std::erase_if(previousView_->results, IsMissingAsset);
		std::erase_if(previousView_->selection, IsMissingAsset);
		if (IsMissingAsset(previousView_->anchor)) {
			previousView_->anchor.clear();
		}
	}
	ValidateClipboard();
}

bool AssetsPanel::ValidateClipboard() {
	const bool missing = std::ranges::any_of(clipboardItems_, IsMissingAsset);
	clipboardHasMissingItems_ = missing;
	return !missing;
}

void AssetsPanel::SetAssetType(AssetType type) {
	if (type_ == type) {
		return;
	}

	type_ = type;
	displayedResults_.clear();
	previousView_.reset();
	selectedPath_.clear();
	selectedItems_.clear();
	selectionAnchor_.clear();
	sceneOpenPath_.clear();
	sceneAppendPath_.clear();
	prefabAppendPath_.clear();
	modelReloadPath_.clear();
	recyclePaths_.clear();
	referencePaths_.clear();
	renamePath_.clear();
	moveRequest_ = {};
	meshReplacementPath_.clear();
	textureReloadPath_.clear();
	textureSelectionPath_.clear();
	textureImportRequested_ = false;
	audioImportRequested_ = false;
	modelImportRequested_ = false;
	folderCreateRequested_ = false;
	prefabCreateRequested_ = false;
	scriptCreateRequested_ = false;
	scriptEditPath_.clear();
	clipEditPath_.clear();
	clipCreateRequested_ = false;
	revealMessage_.clear();
	scrollToSelected_ = false;
	scrollToTop_ = true;
}

void AssetsPanel::OpenDirectory(std::filesystem::path directory) {
	displayedResults_.clear();
	currentDirectory_.swap(directory);
	selectedPath_.clear();
	selectedItems_.clear();
	selectionAnchor_.clear();
	filter_.clear();
	previousView_.reset();
	unavailableRevealPath_.clear();
	revealMessage_.clear();
	scrollToSelected_ = false;
	scrollToTop_ = true;
}

void AssetsPanel::RestoreDirectory(std::filesystem::path folder,
                                   const AssetDatabase &assets) {
	// 検索失敗中はフォルダの有無を確定できないため、次の更新まで場所を保つ。
	if (!assets.GetError().empty()) {
		if (folder != currentDirectory_) {
			OpenDirectory(std::move(folder));
		}
		return;
	}

	const auto &root = assets.GetRootDirectory();
	const auto &directories = assets.GetDirectories();
	folder.make_preferred();
	while (!folder.empty()) {
		const auto found = std::ranges::find_if(directories, [&](const auto &directory) {
			const auto relative = directory.lexically_relative(root).make_preferred();
			if (relative == folder) {
				return true;
			}
			if (CompareStringOrdinal(relative.c_str(), -1, folder.c_str(), -1,
			                         TRUE) != CSTR_EQUAL) {
				return false;
			}
			std::error_code error;
			return std::filesystem::equivalent(
			    PathUtils::ResolveFromExecutable(directory),
			    PathUtils::ResolveFromExecutable(root / folder), error);
		});
		if (found != directories.end()) {
			folder = found->lexically_relative(root);
			break;
		}

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
		displayedResults_.clear();
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
		    select(assets.GetMaterials(), AssetType::MATERIAL, aliases) ||
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
                  bool canCreatePrefab, bool canReturnToExport) {
	refreshed_ = false;
	exportCheckRequested_ = false;
	sceneOpenPath_.clear();
	sceneAppendPath_.clear();
	prefabAppendPath_.clear();
	modelReloadPath_.clear();
	recyclePaths_.clear();
	renamePath_.clear();
	moveRequest_ = {};
	pasteRequest_ = {};
	meshReplacementPath_.clear();
	textureReloadPath_.clear();
	textureSelectionPath_.clear();
	textureImportRequested_ = false;
	audioImportRequested_ = false;
	modelImportRequested_ = false;
	folderCreateRequested_ = false;
	prefabCreateRequested_ = false;
	scriptCreateRequested_ = false;
	scriptEditPath_.clear();
	clipEditPath_.clear();
	clipCreateRequested_ = false;

	const bool focusWindow = focusRequested_ || !revealPath_.empty() ||
	                         (directoryOpenRequested_ && assets.GetError().empty());

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

	const auto refresh = [&] {
		if (assets.Refresh()) {
			refreshed_ = true;
			auto retry = std::exchange(unavailableRevealPath_, {});
			revealMessage_.clear();
			RestoreDirectory(currentDirectory_, assets);
			RemoveMissingPaths();
			if (!retry.empty()) {
				revealPath_ = std::move(retry);
			}
			return true;
		}
		return false;
	};
	const auto trackExplorer = [&] {
		refreshAfterExplorer_ = true;
		explorerTookFocus_ = false;
	};
	bool refreshRequested = false;
	if (refreshAfterExplorer_) {
		DWORD foregroundProcess = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
		if (foregroundProcess != 0 && foregroundProcess != GetCurrentProcessId()) {
			explorerTookFocus_ = true;
		} else if (foregroundProcess == GetCurrentProcessId() && explorerTookFocus_) {
			refreshAfterExplorer_ = false;
		}
	}
	const bool returnedFromExplorer =
	    !refreshAfterExplorer_ && std::exchange(explorerTookFocus_, false);
	if (returnedFromExplorer) {
		refresh();
	}

	if (!revealPath_.empty()) {
		auto previous = keepRevealResults_ ? CaptureResults() : std::nullopt;
		try {
			bool selected = SelectAsset(revealPath_, assets);
			if (!selected && !refreshed_ && assets.Refresh()) {
				refreshed_ = true;
				selected = SelectAsset(revealPath_, assets);
			}
			if (selected) {
				revealMessage_.clear();
				unavailableRevealPath_.clear();
				previousView_ = std::move(previous);
				keepRevealResults_ = false;
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

		if (!revealMessage_.empty()) {
			unavailableRevealPath_ = revealPath_;
		}
		revealPath_.clear();
	}

	const auto &directories = assets.GetDirectories();
	if (!resultPaths_.empty() && assets.GetError().empty()) {
		std::erase_if(resultPaths_, IsMissingAsset);
		if (resultPaths_.empty()) {
			revealMessage_ = "The files from the last operation are no longer available.";
			unavailableRevealPath_.clear();
		}
	}
	if (!resultPaths_.empty() && assets.GetError().empty()) {
		const auto parent = resultPaths_.front().parent_path();
		const auto directory = parent == assets.GetRootDirectory()
		                           ? std::filesystem::path{}
		                           : parent.lexically_relative(assets.GetRootDirectory());
		auto previous = CaptureResults(directory != currentDirectory_);
		OpenDirectory(directory);
		// フォルダはどの種類でも表示される。ファイルがあればその種類を開く。
		for (const auto &path : resultPaths_) {
			if (path.parent_path() == parent && SelectAsset(path, assets)) {
				break;
			}
		}
		selectedItems_ = std::exchange(resultPaths_, {});
		if (selectedItems_.size() > 1) {
			displayedResults_ = selectedItems_;
		}
		selectionAnchor_ = selectedItems_.front();
		scrollToSelected_ = true;
		previousView_ = std::move(previous);
	}

	if (directoryOpenRequested_ && assets.GetError().empty()) {
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

	const bool canWriteAssets = canEditAssets && assets.GetError().empty();
	const bool canManageFiles = canWriteAssets && assets.GetRootDirectory().is_absolute();
	std::filesystem::path draggedPath;
	std::filesystem::path dropDirectory;
	const auto acceptFolder = [&](const std::filesystem::path &directory) {
		if (!canManageFiles) {
			return;
		}

		auto source = AssetDragDrop::AcceptAssetTarget();

		if (!source.empty()) {
			draggedPath = std::move(source);
			dropDirectory = directory;
		}
	};

	refreshRequested = ui.Button("Refresh");
	ui.SameLine();
	const auto explorerDirectory = assets.GetRootDirectory() / currentDirectory_;
	const auto directoryLabel = PathUtils::ToUtf8(explorerDirectory);
	ui.BeginDisabled(assets.GetRootDirectory().empty());
	if (ui.Button("Explorer")) {
		unavailableRevealPath_.clear();
		if (ShellUtils::OpenDirectory(explorerDirectory, revealMessage_))
			trackExplorer();
	}
	ui.EndDisabled();
	ImGui::SetItemTooltip("Open in Explorer\n%s", directoryLabel.c_str());

	ui.SameLine();
	ui.BeginDisabled(!canWriteAssets);
	if (ui.Button("Import"))
		ImGui::OpenPopup("AssetImport");
	ui.EndDisabled();
	if (ImGui::BeginPopup("AssetImport")) {
		if (ui.MenuItem("Texture", nullptr, canWriteAssets))
			textureImportRequested_ = true;
		if (ui.MenuItem("Model", nullptr, canWriteAssets))
			modelImportRequested_ = true;
		if (ui.MenuItem("Audio", nullptr, canWriteAssets))
			audioImportRequested_ = true;
		ImGui::EndPopup();
	}

	ui.SameLine();
	if (ui.Button("Actions"))
		ImGui::OpenPopup("AssetActions");

	if (!currentDirectory_.empty() || !filter_.empty()) {
		if (ImGui::GetContentRegionAvail().x >= 340.f)
			ui.SameLine();
		if (ui.Button("Root"))
			OpenDirectory({});
		acceptFolder(assets.GetRootDirectory());
	}
	if (!currentDirectory_.empty()) {
		ui.SameLine();
		if (ui.Button("Up"))
			OpenDirectory(currentDirectory_.parent_path());
		else
			acceptFolder(assets.GetRootDirectory() / currentDirectory_.parent_path());
	}
	if (!currentDirectory_.empty()) {
		ui.SameLine();
		const auto folder = PathUtils::ToUtf8(currentDirectory_.filename());
		ImGui::TextUnformatted(folder.c_str());
		ImGui::SetItemTooltip("%s", directoryLabel.c_str());
	}

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
		case AssetType::MATERIAL:
			return "Materials";
		case AssetType::GAME_SETTINGS:
			return "Game Settings";
		}

		return "";
	};

	const float filterWidth = ImGui::GetContentRegionAvail().x;
	ImGui::SetNextItemWidth(filterWidth * .42f);
	if (ImGui::BeginCombo("##AssetType", typeLabel(type_))) {
		for (const auto type :
		     {AssetType::MODEL, AssetType::TEXTURE, AssetType::AUDIO,
		      AssetType::SCENE, AssetType::PREFAB, AssetType::GAME_SETTINGS,
		      AssetType::SCRIPT, AssetType::ANIMATION_CLIP, AssetType::MATERIAL}) {
			if (ui.Selectable(typeLabel(type), type_ == type)) {
				SetAssetType(type);
			}
		}

		ImGui::EndCombo();
	}

	ui.SameLine();
	ImGui::SetNextItemWidth(-1.f);
	const bool searchChanged = ImGui::InputTextWithHint("##AssetSearch", "Search", &filter_);
	ImGui::SetItemTooltip("Search by name or paste a file or folder path.");
	bool scopeChanged = false;
	if (!filter_.empty()) {
		scopeChanged = ui.Checkbox("All Asset Types", searchAllTypes_);
		if (!currentDirectory_.empty()) {
			ui.SameLine();
			scopeChanged |= ui.Checkbox("Current Folder Only", searchCurrentFolderOnly_);
			ImGui::SetItemTooltip("Search the current folder and its subfolders.");
		}
	}
	if (searchChanged || scopeChanged) {
		displayedResults_.clear();
		previousView_.reset();
		unavailableRevealPath_.clear();
		scrollToTop_ = true;
		// 検索で隠れたアセットを誤って操作しないよう選択を解除する
		selectedPath_.clear();
		selectedItems_.clear();
		selectionAnchor_.clear();
		scrollToSelected_ = false;
		revealMessage_.clear();
	}

	if (previousView_ && unavailableRevealPath_.empty()) {
		const auto *label = !previousView_->results.empty() ? "Back to Results"
		                  : !previousView_->search.empty() ? "Back to Search"
		                                                   : "Back to Folder";
		if (ui.Button(label)) {
			if (assets.Refresh()) {
				refreshed_ = true;
				auto previous = std::exchange(previousView_, std::nullopt);
				SetAssetType(previous->type);
				RestoreDirectory(std::move(previous->directory), assets);
				filter_ = std::move(previous->search);
				searchAllTypes_ = previous->allTypes;
				searchCurrentFolderOnly_ = previous->currentFolderOnly;
				displayedResults_ = std::move(previous->results);
				selectedItems_ = std::move(previous->selection);
				selectionAnchor_ = std::move(previous->anchor);
				RemoveMissingPaths();
				scrollToSelected_ = !selectedItems_.empty();
				scrollToTop_ = !scrollToSelected_;
			}
		}
	}

	if (!displayedResults_.empty()) {
		ui.Text("Operation Results");
		ui.SameLine();
		if (ui.Button("Show Current Folder")) {
			auto previous = CaptureResults();
			auto selection = std::move(selectedItems_);
			auto anchor = std::move(selectionAnchor_);
			OpenDirectory(currentDirectory_);
			// 表示するフォルダに残る選択は、この後の表示項目との照合で保持する。
			selectedItems_ = std::move(selection);
			selectionAnchor_ = std::move(anchor);
			scrollToSelected_ = !selectedItems_.empty();
			previousView_ = std::move(previous);
		}
	}

	auto search = NormalizeSearch(filter_);
	const auto searchPath = FullSearchPath(search);
	const bool absoluteSearch = !searchPath.empty();
	if (absoluteSearch) {
		search = searchPath.generic_wstring();
	}

	const bool mixedTypes = !displayedResults_.empty() || absoluteSearch ||
	                        (!filter_.empty() && searchAllTypes_);
	const std::array filesByType{
	    std::pair{AssetType::MODEL, &assets.GetModels()},
	    std::pair{AssetType::TEXTURE, &assets.GetTextures()},
	    std::pair{AssetType::MATERIAL, &assets.GetMaterials()},
	    std::pair{AssetType::AUDIO, &assets.GetAudioClips()},
	    std::pair{AssetType::SCENE, &assets.GetScenes()},
	    std::pair{AssetType::PREFAB, &assets.GetPrefabs()},
	    std::pair{AssetType::SCRIPT, &assets.GetScripts()},
	    std::pair{AssetType::ANIMATION_CLIP, &assets.GetAnimationClips()},
	    std::pair{AssetType::GAME_SETTINGS, &assets.GetGameConfigurations()}};
	const auto searchPrefix = searchCurrentFolderOnly_ && !currentDirectory_.empty()
	                              ? currentDirectory_.generic_wstring() + L"/"
	                              : std::wstring{};
	const auto inResults = [&](const std::filesystem::path &path) {
		return std::ranges::find(displayedResults_, path) != displayedResults_.end();
	};
	const auto visiblePath = [&](const std::filesystem::path &path) {
		if (!displayedResults_.empty()) {
			return inResults(path);
		}
		const auto relative = path.lexically_relative(assets.GetRootDirectory());
		if (filter_.empty()) {
			return relative.parent_path() == currentDirectory_;
		}
		const auto text = relative.generic_wstring();
		if (!searchPrefix.empty() &&
		    (text.size() <= searchPrefix.size() ||
		     CompareStringOrdinal(text.data(), static_cast<int>(searchPrefix.size()),
		                          searchPrefix.data(), static_cast<int>(searchPrefix.size()),
		                          TRUE) != CSTR_EQUAL)) {
			return false;
		}
		if (absoluteSearch) {
			const auto absolute = PathUtils::ResolveFromExecutable(path).generic_wstring();
			return CompareStringOrdinal(absolute.c_str(), -1, search.c_str(), -1,
			                            TRUE) == CSTR_EQUAL;
		}
		const auto queryPath = std::wstring_view(text).substr(searchPrefix.size());
		return !search.empty() &&
		       FindStringOrdinal(FIND_FROMSTART, queryPath.data(),
		                         static_cast<int>(queryPath.size()), search.c_str(),
		                         static_cast<int>(search.size()), TRUE) >= 0;
	};
	std::vector<std::filesystem::path> visible;

	for (const auto &directory : directories) {
		if (visiblePath(directory)) {
			visible.push_back(directory);
		}
	}

	std::vector<std::pair<AssetType, const std::filesystem::path *>> visibleFiles;
	for (const auto &[type, paths] : filesByType) {
		if (!mixedTypes && type != type_) {
			continue;
		}
		for (const auto &path : *paths) {
			if (visiblePath(path)) {
				visible.push_back(path);
				visibleFiles.emplace_back(type, &path);
			}
		}
	}

	const auto isSelected = [&](const std::filesystem::path &path) {
		return std::ranges::find(selectedItems_, path) != selectedItems_.end();
	};
	auto selectedType = type_;
	const auto syncSingle = [&] {
		selectedPath_.clear();
		selectedType = type_;
		if (selectedItems_.size() == 1) {
			const auto found = std::ranges::find(
			    visibleFiles, selectedItems_.front(),
			    [](const auto &entry) -> const auto & { return *entry.second; });
			if (found != visibleFiles.end()) {
				selectedType = found->first;
				selectedPath_ = *found->second;
			}
		}
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

	if (!clipboardItems_.empty() && clipboardRoot_ != root) {
		clipboardItems_.clear();
		clipboardCut_ = false;
		clipboardHasMissingItems_ = false;
	}

	const auto currentFolder = [&] {
		return currentDirectory_.empty() ? root : root / currentDirectory_;
	};
	const auto storeClipboard = [&](bool cut) {
		clipboardItems_ = SelectedAssetRoots(selectedItems_);
		clipboardRoot_ = root;
		clipboardCut_ = cut;
		clipboardHasMissingItems_ = false;
	};
	const auto requestPaste = [&](const std::filesystem::path &folder) {
		if (ValidateClipboard()) {
			pasteRequest_ = {clipboardItems_, folder};
		}
	};
	const auto drawPaste = [&](const char *label, const std::filesystem::path &folder) {
		if (ui.MenuItem(label, "Ctrl+V", canManageFiles && !clipboardItems_.empty())) {
			requestPaste(folder);
		}
	};
	const auto drawClipboardMenu = [&] {
		const bool enabled = canManageFiles && !selectedItems_.empty();

		if (ui.MenuItem("Copy", "Ctrl+C", enabled)) {
			storeClipboard(false);
		}

		if (ui.MenuItem("Cut", "Ctrl+X", enabled)) {
			storeClipboard(true);
		}
	};
	const auto drawBackgroundMenu = [&] {
		if (canManageFiles &&
		    ImGui::BeginPopupContextWindow("AssetsBackground",
		                                   ImGuiPopupFlags_MouseButtonRight |
		                                       ImGuiPopupFlags_NoOpenOverItems)) {
			drawPaste("Paste", currentFolder());
			ImGui::EndPopup();
		}
	};

	const auto drawLocationMenu = [&](const std::filesystem::path &path) {
		if (ui.MenuItem("Show in Explorer")) {
			unavailableRevealPath_.clear();
			if (ShellUtils::RevealInExplorer(path, revealMessage_)) {
				trackExplorer();
			}
		}
	};

	if (!assets.GetError().empty()) {
		ui.Text("Assets could not be updated. Showing the last successful listing.");
		ui.Text("Refresh successfully to resume file operations.");
		if (!resultPaths_.empty()) {
			ui.Text("Refresh to show the files from the last operation.");
		}
		if (directoryOpenRequested_) {
			ui.Text("Refresh to open the folder from the last operation.");
		}
		ImGui::TextWrapped("%s", assets.GetError().c_str());
	}

	if (clipboardHasMissingItems_) {
		ImGui::TextWrapped("Some copied or cut items are missing. "
		                   "Restore them or remove them from the clipboard.");
		if (ui.Button("Remove Missing Clipboard Items")) {
			std::erase_if(clipboardItems_, IsMissingAsset);
			clipboardHasMissingItems_ = false;
			if (clipboardItems_.empty()) {
				clipboardRoot_.clear();
				clipboardCut_ = false;
			}
		}
	}

	if (!revealMessage_.empty()) {
		ImGui::TextWrapped("%s", revealMessage_.c_str());
		if (!unavailableRevealPath_.empty()) {
			if (ui.Button("Show Location in Explorer")) {
				std::string error;
				if (!ShellUtils::RevealLocationInExplorer(unavailableRevealPath_, error)) {
					revealMessage_ = std::move(error);
				}
			}
		}
	}

	// 対象がある時だけ一覧の下に主操作を出す。
	const auto primaryPath = selectedPath_;
	const auto primaryType = selectedType;
	const bool hasPrimaryAction = !primaryPath.empty() &&
	    (primaryType == AssetType::MODEL ||
	     (canEditAssets && (primaryType == AssetType::SCENE || primaryType == AssetType::PREFAB)));
	const bool drawEntries = ui.BeginChild(
	    "AssetEntries", 0.0f, hasPrimaryAction ? -ImGui::GetFrameHeightWithSpacing() : 0.0f);
	std::filesystem::path nextDirectory;
	bool openRequested = false;

	if (drawEntries) {
		if (scrollToTop_) {
			ImGui::SetScrollY(0.0f);
			scrollToTop_ = false;
		}

		bool anyVisible = false;

		for (const auto &directory : directories) {
			const auto relative =
			    directory.lexically_relative(assets.GetRootDirectory());

			if (!visiblePath(directory)) {
				continue;
			}

			anyVisible = true;
			const auto name = filter_.empty() && displayedResults_.empty()
			                      ? relative.filename() : relative;
			const auto label = std::format(
			    "[Folder] {}", PathUtils::ToUtf8(name));
			ImGui::PushID(PathUtils::ToUtf8(directory).c_str());

			if (ui.Selectable(label.c_str(), isSelected(directory))) {
				selectRow(directory, true);
			}
			if (scrollToSelected_ && selectionAnchor_ == directory) {
				ImGui::SetScrollHereY(0.5f);
				scrollToSelected_ = false;
			}
			if (ImGui::IsItemHovered() &&
			    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
			    !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) {
				selectRow(directory, false);
				nextDirectory = relative;
				openRequested = true;
			}

			acceptFolder(directory);

			if (canManageFiles) {
				if (AssetDragDrop::DrawSource(directory, AssetDragDrop::Kind::FOLDER)) {
					selectContext(directory);
				}
			}

			if (ImGui::BeginPopupContextItem()) {
				selectContext(directory);
				drawLocationMenu(directory);
				if (canManageFiles) {
					ImGui::Separator();
					drawClipboardMenu();
					drawPaste("Paste into Folder", directory);
					ImGui::Separator();

					if (ui.MenuItem("Rename...", "F2", selectedItems_.size() == 1)) {
						renamePath_ = directory;
					}

					if (ui.MenuItem("Move to Recycle Bin...", "Delete")) {
						recyclePaths_ = SelectedAssetRoots(selectedItems_);
					}

				}
				if (ui.MenuItem("Find References...", nullptr,
				                canEditAssets && root.is_absolute())) {
					referencePaths_ = selectedItems_;
				}
				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		for (const auto &[assetType, path] : visibleFiles) {
			const auto &assetPath = *path;

			// パネル内では、アセットフォルダからのパスを表示する
			const auto displayPath =
			    assetPath.lexically_relative(assets.GetRootDirectory());

			anyVisible = true;

			ImGui::PushID(PathUtils::ToUtf8(assetPath).c_str());
			const auto label = filter_.empty() && displayedResults_.empty()
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

				if (assetType == AssetType::SCRIPT && canEditAssets &&
				    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					scriptEditPath_ = assetPath;
				if (assetType == AssetType::ANIMATION_CLIP && canEditAssets && doubleClicked)
					clipEditPath_ = assetPath;
				if (assetType == AssetType::SCENE && canEditAssets &&
				    doubleClicked) {
					sceneOpenPath_ = assetPath;
				}


				if (assetType == AssetType::TEXTURE && canSetTexture &&
				    textureSlot_ >= 0 && doubleClicked) {
					textureSelectionPath_ = assetPath;
				}
			}

			if (ImGui::BeginPopupContextItem()) {
				selectContext(assetPath);
				drawLocationMenu(assetPath);
				ImGui::Separator();
				drawClipboardMenu();
				drawPaste("Paste", currentFolder());
				ImGui::Separator();

				if (ui.MenuItem("Rename...", "F2",
				                selectedItems_.size() == 1 && canManageFiles)) {
					renamePath_ = assetPath;
				}

				if (ui.MenuItem("Find References...", nullptr,
				                canEditAssets &&
				                    assets.GetRootDirectory().is_absolute())) {
					referencePaths_ = selectedItems_;
				}

				if (ui.MenuItem("Move to Recycle Bin...", "Delete",
				                canManageFiles)) {
					recyclePaths_ = SelectedAssetRoots(selectedItems_);
				}

				if (assetType == AssetType::PREFAB &&
				    ui.MenuItem("Add Prefab", nullptr, canEditAssets)) {
					prefabAppendPath_ = assetPath;
				}

				if (assetType == AssetType::SCENE &&
				    ui.MenuItem("Add Objects", nullptr, canEditAssets)) {
					sceneAppendPath_ = assetPath;
				}

				if (assetType == AssetType::SCENE &&
				    ui.MenuItem("Open Scene", nullptr, canEditAssets)) {
					sceneOpenPath_ = assetPath;
				}

				if (assetType == AssetType::TEXTURE &&
				    ui.MenuItem("Assign Texture to Selection", nullptr,
				                canSetTexture && textureSlot_ >= 0)) {
					textureSelectionPath_ = assetPath;
				}

				if (assetType == AssetType::TEXTURE &&
				    selectedItems_.size() == 1 &&
				    ui.MenuItem("Reload Texture", nullptr, canEditAssets)) {
					textureReloadPath_ = assetPath;
				}

				if (assetType == AssetType::MODEL &&
				    ui.MenuItem("Replace Mesh of Selection", nullptr,
				                canEditAssets && canReplaceMesh)) {
					meshReplacementPath_ = assetPath;
				}

				if (assetType == AssetType::MODEL &&
				    selectedItems_.size() == 1 &&
				    ui.MenuItem("Reload Model", nullptr, canEditAssets)) {
					modelReloadPath_ = assetPath;
				}


				ImGui::EndPopup();
			}

			if (scrollToSelected_ && selectionAnchor_ == assetPath) {
				ImGui::SetScrollHereY(0.5f);
				scrollToSelected_ = false;
			}

			if ((assetType != AssetType::GAME_SETTINGS || canEditAssets) &&
			    AssetDragDrop::DrawSource(
			        assetPath,
			        assetType == AssetType::TEXTURE  ? AssetDragDrop::Kind::TEXTURE
			        : assetType == AssetType::SCRIPT ? AssetDragDrop::Kind::SCRIPT
			        : assetType == AssetType::MODEL  ? AssetDragDrop::Kind::MODEL
			        : assetType == AssetType::AUDIO  ? AssetDragDrop::Kind::AUDIO
			        : assetType == AssetType::PREFAB ? AssetDragDrop::Kind::PREFAB
			        : assetType == AssetType::SCENE  ? AssetDragDrop::Kind::SCENE
			        : assetType == AssetType::ANIMATION_CLIP ? AssetDragDrop::Kind::ANIMATION_CLIP
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
		auto previous = CaptureResults();
		OpenDirectory(std::move(nextDirectory));
		previousView_ = std::move(previous);
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
		} else if (canManageFiles && io.KeyCtrl) {
			const bool copy = ImGui::IsKeyPressed(ImGuiKey_C, false);
			const bool cut = ImGui::IsKeyPressed(ImGuiKey_X, false);

			if (!selectedItems_.empty() && (copy || cut)) {
				storeClipboard(cut);
			}

			if (!clipboardItems_.empty() && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
				requestPaste(currentFolder());
			}
		} else if (canManageFiles && !io.KeyCtrl && selectedItems_.size() == 1 &&
		           ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
			renamePath_ = selectedItems_.front();
		} else if (canManageFiles && !io.KeyCtrl && !selectedItems_.empty() &&
		           ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
			recyclePaths_ = SelectedAssetRoots(selectedItems_);
		}
	}

	if (hasPrimaryAction && selectedPath_ == primaryPath) {
		switch (primaryType) {
		case AssetType::MODEL:
			if (ui.Button("Add to Scene"))
				requestedPath = primaryPath;
			break;
		case AssetType::SCENE:
			if (ui.Button("Open Scene"))
				sceneOpenPath_ = primaryPath;
			break;
		case AssetType::PREFAB:
			if (ui.Button("Add Prefab"))
				prefabAppendPath_ = primaryPath;
			break;
		default:
			break;
		}
	}

	if (ImGui::BeginPopup("AssetActions")) {
		if (canReturnToExport && ui.MenuItem("Return to Export Check", nullptr, true))
			exportCheckRequested_ = true;

		if (ImGui::BeginMenu("Create", canWriteAssets)) {
			if (ui.MenuItem("Folder", nullptr, true))
				folderCreateRequested_ = true;
			if (ui.MenuItem("Prefab from Selection", nullptr, canCreatePrefab))
				prefabCreateRequested_ = true;
			if (ui.MenuItem("Script", nullptr, true))
				scriptCreateRequested_ = true;
			if (ui.MenuItem("Animation Clip", nullptr, true))
				clipCreateRequested_ = true;
			ImGui::EndMenu();
		}
		if (!selectedItems_.empty() || selectedType == AssetType::TEXTURE) {
			ImGui::Separator();
			if (selectedItems_.size() > 1) {
				ui.Text(std::format("{} items selected", selectedItems_.size()));
			}
			if (!selectedPath_.empty()) {
				const auto displayPath =
				    selectedPath_.lexically_relative(assets.GetRootDirectory());

				ui.Text(PathUtils::ToUtf8(displayPath));
			}

			if (selectedType == AssetType::TEXTURE) {
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

				if (ui.MenuItem("Assign Texture to Selection", nullptr, true)) {
					textureSelectionPath_ = selectedPath_;
				}

				ui.EndDisabled();

				ui.EndDisabled();
			}

			if (selectedType == AssetType::ANIMATION_CLIP) {
				ui.BeginDisabled(selectedPath_.empty() || !canEditAssets);
				if (ui.MenuItem("Edit Animation Clip", nullptr, true))
					clipEditPath_ = selectedPath_;
				ui.EndDisabled();
			} else if (selectedType == AssetType::SCRIPT) {
				ui.BeginDisabled(selectedPath_.empty() || !canEditAssets);
				if (ui.MenuItem("Edit Script", nullptr, true))
					scriptEditPath_ = selectedPath_;
				ui.EndDisabled();
			}
		}
		ImGui::EndPopup();
	}

	drawBackgroundMenu();
	// 上部の移動先も、現在の一覧と選択を確定してから処理する。
	if (!draggedPath.empty() && std::ranges::find(visible, draggedPath) != visible.end()) {
		moveRequest_.sources = isSelected(draggedPath)
		                           ? SelectedAssetRoots(selectedItems_)
		                           : std::vector<std::filesystem::path>{std::move(draggedPath)};
		moveRequest_.directory = std::move(dropDirectory);
	}
	if (refreshRequested)
		refresh();
	ui.EndWindow();
	return requestedPath;
}

const std::filesystem::path &AssetsPanel::GetTextureSelectionRequest() const {
	return textureSelectionPath_;
}

void AssetsPanel::Clear() {
	exportCheckRequested_ = false;
	recyclePaths_.clear();
	referencePaths_.clear();
	renamePath_.clear();
	moveRequest_ = {};
	pasteRequest_ = {};
	clipboardItems_.clear();
	clipboardRoot_.clear();
	clipboardCut_ = false;
	clipboardHasMissingItems_ = false;
	directoryOpenRequested_ = false;
	requestedDirectory_.clear();
	type_ = AssetType::MODEL;
	textureSelectionPath_.clear();
	textureImportRequested_ = false;
	audioImportRequested_ = false;
	modelImportRequested_ = false;
	folderCreateRequested_ = false;
	prefabCreateRequested_ = false;
	scriptCreateRequested_ = false;
	scriptEditPath_.clear();
	clipEditPath_.clear();
	clipCreateRequested_ = false;
	selectedPath_.clear();
	selectedItems_.clear();
	selectionAnchor_.clear();
	sceneOpenPath_.clear();
	sceneAppendPath_.clear();
	prefabAppendPath_.clear();
	modelReloadPath_.clear();
	meshReplacementPath_.clear();
	textureReloadPath_.clear();
	currentDirectory_.clear();
	filter_.clear();
	previousView_.reset();
	revealPath_.clear();
	resultPaths_.clear();
	displayedResults_.clear();
	unavailableRevealPath_.clear();
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
	ClearNavigationRequests();
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

bool AssetsPanel::IsAudioImportRequested() const {
	return audioImportRequested_;
}
