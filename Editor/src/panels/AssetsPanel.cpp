#include "panels/AssetsPanel.h"

#include "assets/AssetDatabase.h"
#include "assets/ModelAssetDragDrop.h"
#include "core/Logger.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"

#include <algorithm>
#include <exception>
#include <format>
#include <imgui.h>
#include <stdexcept>
#include <system_error>
#include <utility>

void AssetsPanel::RequestReveal(const std::filesystem::path &path) {
	revealPath_ = path;
}

void AssetsPanel::SetAssetType(AssetType type) {
	if (type_ == type) {
		return;
	}

	type_ = type;
	selectedPath_.clear();
	instanceSelectionPath_.clear();
	textureUsageSelectionPath_.clear();
	textureSelectionPath_.clear();
	textureClearRequested_ = false;
	textureImportRequested_ = false;
	texturePreviewPath_.clear();
	revealMessage_.clear();
	scrollToSelected_ = false;
	scrollToTop_ = true;
}

void AssetsPanel::OpenDirectory(std::filesystem::path directory) {
	currentDirectory_.swap(directory);
	selectedPath_.clear();
	filter_.clear();
	revealMessage_.clear();
	scrollToSelected_ = false;
	scrollToTop_ = true;
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
		currentDirectory_.swap(folder);
		filter_.clear();
		scrollToSelected_ = true;
		scrollToTop_ = false;
		return true;
	};

	// 全種類の通常パスを先に探し、別名の場合だけファイルの照合を行う。
	for (const bool aliases : {false, true}) {
		if (select(assets.GetModels(), AssetType::MODEL, aliases) ||
		    select(assets.GetTextures(), AssetType::TEXTURE, aliases)) {
			return true;
		}
	}

	return false;
}

std::filesystem::path AssetsPanel::Draw(const ImGuiSystem &ui,
                                        AssetDatabase &assets,
                                        bool canSetTexture,
                                        bool canImportTexture) {
	instanceSelectionPath_.clear();
	textureUsageSelectionPath_.clear();
	textureSelectionPath_.clear();
	textureClearRequested_ = false;
	textureImportRequested_ = false;
	texturePreviewPath_.clear();

	if (!revealPath_.empty()) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
	}

	if (!ui.BeginWindow("Assets")) {
		ui.EndWindow();
		return {};
	}

	std::filesystem::path requestedPath;

	if (ui.Button("Refresh")) {
		assets.Refresh();
		revealMessage_.clear();
	}

	if (!revealPath_.empty()) {
		try {
			if (SelectAsset(revealPath_, assets) ||
			    (assets.Refresh() && SelectAsset(revealPath_, assets))) {
				revealMessage_.clear();
			} else {
				revealMessage_ = "Asset is not in the indexed Assets folder.";
			}
		} catch (const std::exception &error) {
			Logger::Error(
			    std::format("AssetsPanel::RequestReveal: {}", error.what()));
			revealMessage_ =
			    "Could not locate the asset. See the log for details.";
		}

		revealPath_.clear();
	}

	ui.SameLine();
	ui.BeginDisabled(!canImportTexture);
	if (ui.Button("Import Texture")) {
		textureImportRequested_ = true;
	}
	ui.EndDisabled();

	const auto &directories = assets.GetDirectories();

	if (!currentDirectory_.empty() &&
	    std::none_of(
	        directories.begin(), directories.end(), [&](const auto &directory) {
		        return directory.lexically_relative(
		                   assets.GetRootDirectory()) == currentDirectory_;
	        })) {
		// 更新で開いていたフォルダが消えた場合は、検索先自身へ戻る
		OpenDirectory({});
	}

	ui.SameLine();
	ui.BeginDisabled(currentDirectory_.empty() && filter_.empty());

	if (ui.Button("Root")) {
		OpenDirectory({});
	}

	ui.EndDisabled();
	ui.SameLine();
	ui.BeginDisabled(currentDirectory_.empty());

	if (ui.Button("Up")) {
		OpenDirectory(currentDirectory_.parent_path());
	}

	ui.EndDisabled();
	ui.Text(PathUtils::ToUtf8(assets.GetRootDirectory() / currentDirectory_));
	ImGui::SetNextItemWidth(160.0f);

	const auto typeLabel = [](AssetType type) {
		return type == AssetType::MODEL ? "Models" : "Textures";
	};

	if (ImGui::BeginCombo("Asset Type", typeLabel(type_))) {
		for (const auto type : {AssetType::MODEL, AssetType::TEXTURE}) {
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
		scrollToSelected_ = false;
		revealMessage_.clear();
	}

	ImGui::TextDisabled("%s", filter_.empty() ? "Click a folder to open it."
	                                          : "Searching all folders.");

	const auto &entries =
	    type_ == AssetType::MODEL ? assets.GetModels() : assets.GetTextures();

	if (!selectedPath_.empty() && std::find(entries.begin(), entries.end(),
	                                        selectedPath_) == entries.end()) {
		selectedPath_.clear();
		scrollToSelected_ = false;
	}

	if (!assets.GetError().empty()) {
		ui.Text(assets.GetError());
	}

	if (!revealMessage_.empty()) {
		ImGui::TextWrapped("%s", revealMessage_.c_str());
	}

	const auto contentSize = ui.GetContentSize();
	const float footerHeight = ImGui::GetTextLineHeightWithSpacing() +
	                           ImGui::GetFrameHeightWithSpacing() *
	                               (type_ == AssetType::TEXTURE ? 5.0f : 2.0f) +
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

				if (ui.Selectable(label.c_str(), false)) {
					nextDirectory = relative;
					openRequested = true;
				}

				ImGui::PopID();
			}
		}

		for (std::size_t i = 0; i < entries.size(); ++i) {
			const auto &assetPath = entries[i];

			// パネル内では、アセットフォルダからのパスを表示する
			const auto displayPath =
			    assetPath.lexically_relative(assets.GetRootDirectory());

			if (filter_.empty() &&
			    displayPath.parent_path() != currentDirectory_) {
				continue;
			}

			const std::string searchPath = PathUtils::ToUtf8(displayPath);

			if (!filter_.empty() &&
			    searchPath.find(filter_) == std::string::npos) {
				continue;
			}

			anyVisible = true;

			ui.PushID(static_cast<int>(i));
			const auto label = filter_.empty()
			                       ? PathUtils::ToUtf8(displayPath.filename())
			                       : searchPath;

			if (ui.Selectable(label.c_str(), selectedPath_ == assetPath)) {
				selectedPath_ = assetPath;
				scrollToSelected_ = false;
				revealMessage_.clear();

				if (type_ == AssetType::TEXTURE && canSetTexture &&
				    textureSlot_ >= 0 &&
				    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					textureSelectionPath_ = assetPath;
				}
			}

			if (ImGui::BeginPopupContextItem()) {
				selectedPath_ = assetPath;
				scrollToSelected_ = false;
				revealMessage_.clear();

				if (type_ == AssetType::TEXTURE &&
				    ui.MenuItem("Assign Texture to Selection", nullptr,
				                canSetTexture && textureSlot_ >= 0)) {
					textureSelectionPath_ = assetPath;
				}

				if (ui.MenuItem("Select Instances", nullptr, true)) {
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

			if (type_ == AssetType::MODEL &&
			    ModelAssetDragDrop::DrawSource(assetPath)) {
				selectedPath_ = assetPath;
			}

			ui.PopID();
		}

		if (!anyVisible) {
			ui.Text(filter_.empty() ? "No folders or assets."
			                        : "No matching assets.");
		}
	}

	ui.EndChild();

	if (openRequested) {
		OpenDirectory(std::move(nextDirectory));
	}

	if (!selectedPath_.empty()) {
		const auto displayPath =
		    selectedPath_.lexically_relative(assets.GetRootDirectory());

		ui.Text(PathUtils::ToUtf8(displayPath));
	}

	if (type_ == AssetType::MODEL) {
		ui.BeginDisabled(selectedPath_.empty());

		if (ui.Button("Add to Scene")) {
			requestedPath = selectedPath_;
		}

		ui.EndDisabled();
	} else {
		ui.BeginDisabled(selectedPath_.empty());

		if (ui.Button("Preview Image")) {
			texturePreviewPath_ = selectedPath_;
		}

		ui.EndDisabled();
		ImGui::InputInt("Texture Slot", &textureSlot_);
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
	}

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
	type_ = AssetType::MODEL;
	textureUsageSelectionPath_.clear();
	textureSelectionPath_.clear();
	textureClearRequested_ = false;
	textureImportRequested_ = false;
	texturePreviewPath_.clear();
	selectedPath_.clear();
	instanceSelectionPath_.clear();
	currentDirectory_.clear();
	filter_.clear();
	revealPath_.clear();
	revealMessage_.clear();
	scrollToSelected_ = false;
	scrollToTop_ = true;
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

const std::filesystem::path &AssetsPanel::GetCurrentDirectory() const {
	return currentDirectory_;
}
