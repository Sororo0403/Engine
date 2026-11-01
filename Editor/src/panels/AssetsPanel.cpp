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

void AssetsPanel::OpenDirectory(std::filesystem::path directory) {
	currentDirectory_.swap(directory);
	selectedPath_.clear();
	filter_.clear();
	revealMessage_.clear();
	scrollToSelected_ = false;
	scrollToTop_ = true;
}

bool AssetsPanel::SelectModel(const std::filesystem::path &path,
                              const AssetDatabase &assets) {
	const auto target = PathUtils::ResolveFromExecutable(path);
	const auto directory = PathUtils::GetExecutableDirectory();

	if (directory.empty()) {
		throw std::runtime_error("実行ファイルのフォルダを取得できません。");
	}

	const auto &models = assets.GetModels();
	auto found =
	    std::find_if(models.begin(), models.end(), [&](const auto &model) {
		    return (directory / model).lexically_normal() == target;
	    });

	if (found == models.end()) {
		// パスの大文字・小文字や別名が異なる場合も、実際のファイルで照合する
		found =
		    std::find_if(models.begin(), models.end(), [&](const auto &model) {
			    std::error_code error;
			    return std::filesystem::equivalent(directory / model, target,
			                                       error);
		    });
	}

	if (found == models.end()) {
		return false;
	}

	auto selected = *found;
	auto folder =
	    selected.lexically_relative(assets.GetRootDirectory()).parent_path();
	selectedPath_.swap(selected);
	currentDirectory_.swap(folder);
	filter_.clear();
	scrollToSelected_ = true;
	scrollToTop_ = false;
	return true;
}

std::filesystem::path AssetsPanel::Draw(const ImGuiSystem &ui,
                                        AssetDatabase &assets) {
	instanceSelectionPath_.clear();

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
			if (SelectModel(revealPath_, assets) ||
			    (assets.Refresh() && SelectModel(revealPath_, assets))) {
				revealMessage_.clear();
			} else {
				revealMessage_ = "Model is not in the indexed Assets folder.";
			}
		} catch (const std::exception &error) {
			Logger::Error(
			    std::format("AssetsPanel::RequestReveal: {}", error.what()));
			revealMessage_ =
			    "Could not locate the model. See the log for details.";
		}

		revealPath_.clear();
	}

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

	if (ui.InputText("Search", filter_)) {
		scrollToTop_ = true;
		// 検索で隠れたモデルを誤って配置しないよう選択を解除する
		selectedPath_.clear();
		scrollToSelected_ = false;
		revealMessage_.clear();
	}

	ImGui::TextDisabled("%s", filter_.empty() ? "Click a folder to open it."
	                                          : "Searching all folders.");

	const auto &models = assets.GetModels();

	if (!selectedPath_.empty() && std::find(models.begin(), models.end(),
	                                        selectedPath_) == models.end()) {
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
	                           ImGui::GetFrameHeightWithSpacing() * 2.0f +
	                           ImGui::GetStyle().ItemSpacing.y;
	const float listHeight = std::max(1.0f, contentSize.y - footerHeight);

	const bool drawModels = ui.BeginChild("ModelAssets", 0.0f, listHeight);
	std::filesystem::path nextDirectory;
	bool openRequested = false;

	if (drawModels) {
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

		for (std::size_t i = 0; i < models.size(); ++i) {
			const auto &modelPath = models[i];

			// パネル内では、アセットフォルダからのパスを表示する
			const auto displayPath =
			    modelPath.lexically_relative(assets.GetRootDirectory());

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

			if (ui.Selectable(label.c_str(), selectedPath_ == modelPath)) {
				selectedPath_ = modelPath;
				scrollToSelected_ = false;
				revealMessage_.clear();
			}

			if (ImGui::BeginPopupContextItem()) {
				selectedPath_ = modelPath;
				scrollToSelected_ = false;
				revealMessage_.clear();

				if (ui.MenuItem("Select Instances", nullptr, true)) {
					instanceSelectionPath_ = modelPath;
				}

				ImGui::SetItemTooltip("Select all objects using this model, "
				                      "including hidden objects.");
				ImGui::EndPopup();
			}

			if (scrollToSelected_ && selectedPath_ == modelPath) {
				ImGui::SetScrollHereY(0.5f);
				scrollToSelected_ = false;
			}

			if (ModelAssetDragDrop::DrawSource(modelPath)) {
				selectedPath_ = modelPath;
			}

			ui.PopID();
		}

		if (!anyVisible) {
			ui.Text(filter_.empty() ? "No folders or models."
			                        : "No matching models.");
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

	ui.BeginDisabled(selectedPath_.empty());

	if (ui.Button("Add to Scene")) {
		requestedPath = selectedPath_;
	}

	if (ui.Button("Select Instances")) {
		instanceSelectionPath_ = selectedPath_;
	}

	ImGui::SetItemTooltip(
	    "Select all objects using this model, including hidden objects.");

	ui.EndDisabled();

	ui.EndWindow();
	return requestedPath;
}

const std::filesystem::path &AssetsPanel::GetInstanceSelectionRequest() const {
	return instanceSelectionPath_;
}

void AssetsPanel::Clear() {
	selectedPath_.clear();
	instanceSelectionPath_.clear();
	currentDirectory_.clear();
	filter_.clear();
	revealPath_.clear();
	revealMessage_.clear();
	scrollToSelected_ = false;
	scrollToTop_ = true;
}
