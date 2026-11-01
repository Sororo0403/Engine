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

void AssetsPanel::RequestReveal(const std::filesystem::path &path) {
	revealPath_ = path;
}

bool AssetsPanel::SelectModel(const std::filesystem::path &path, const AssetDatabase &assets) {
	const auto target = PathUtils::ResolveFromExecutable(path);
	const auto directory = PathUtils::GetExecutableDirectory();
	if (directory.empty()) {
		throw std::runtime_error("実行ファイルのフォルダを取得できません。");
	}
	const auto &models = assets.GetModels();
	auto found = std::find_if(models.begin(), models.end(), [&](const auto &model) {
		return (directory / model).lexically_normal() == target;
	});
	if (found == models.end()) {
		// パスの大文字・小文字や別名が異なる場合も、実際のファイルで照合する
		found = std::find_if(models.begin(), models.end(), [&](const auto &model) {
			std::error_code error;
			return std::filesystem::equivalent(directory / model, target, error);
		});
	}
	if (found == models.end()) {
		return false;
	}
	auto selected = *found;
	selectedPath_.swap(selected);
	filter_.clear();
	scrollToSelected_ = true;
	return true;
}

std::filesystem::path AssetsPanel::Draw(const ImGuiSystem &ui,
                                        AssetDatabase &assets) {
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

	ui.SameLine();
	ui.Text(PathUtils::ToUtf8(assets.GetRootDirectory()));
	if (!revealPath_.empty()) {
		try {
			if (SelectModel(revealPath_, assets) || (assets.Refresh() && SelectModel(revealPath_, assets))) {
				revealMessage_.clear();
			} else {
				revealMessage_ = "Model is not in the indexed Assets folder.";
			}
		} catch (const std::exception &error) {
			Logger::Error(std::format("AssetsPanel::RequestReveal: {}", error.what()));
			revealMessage_ = "Could not locate the model. See the log for details.";
		}
		revealPath_.clear();
	}

	if (ui.InputText("Search", filter_)) {
		// 検索で隠れたモデルを誤って配置しないよう選択を解除する
		selectedPath_.clear();
		scrollToSelected_ = false;
		revealMessage_.clear();
	}

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
	const float listHeight = std::max(1.0f, contentSize.y - 90.0f);

	const bool drawModels = ui.BeginChild("ModelAssets", 0.0f, listHeight);

	if (drawModels) {
		bool anyVisible = false;

		for (std::size_t i = 0; i < models.size(); ++i) {
			const auto &modelPath = models[i];

			// パネル内では、アセットフォルダからのパスを表示する
			const auto displayPath =
			    modelPath.lexically_relative(assets.GetRootDirectory());

			const std::string label = PathUtils::ToUtf8(displayPath);

			if (!filter_.empty() && label.find(filter_) == std::string::npos) {
				continue;
			}

			anyVisible = true;

			ui.PushID(static_cast<int>(i));

			if (ui.Selectable(label.c_str(), selectedPath_ == modelPath)) {
				selectedPath_ = modelPath;
				scrollToSelected_ = false;
				revealMessage_.clear();
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
			ui.Text("No models found.");
		}
	}

	ui.EndChild();

	if (!selectedPath_.empty()) {
		const auto displayPath =
		    selectedPath_.lexically_relative(assets.GetRootDirectory());

		ui.Text(PathUtils::ToUtf8(displayPath));
	}

	ui.BeginDisabled(selectedPath_.empty());

	if (ui.Button("Add to Scene")) {
		requestedPath = selectedPath_;
	}

	ui.EndDisabled();

	ui.EndWindow();
	return requestedPath;
}

void AssetsPanel::Clear() {
	selectedPath_.clear();
	filter_.clear();
	revealPath_.clear();
	revealMessage_.clear();
	scrollToSelected_ = false;
}
