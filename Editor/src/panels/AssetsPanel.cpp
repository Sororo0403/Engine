#include "panels/AssetsPanel.h"

#include "assets/AssetDatabase.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"

#include <algorithm>

std::filesystem::path AssetsPanel::Draw(const ImGuiSystem &ui,
                                        AssetDatabase &assets) {
	if (!ui.BeginWindow("Assets")) {
		ui.EndWindow();
		return {};
	}

	std::filesystem::path requestedPath;

	if (ui.Button("Refresh")) {
		assets.Refresh();
	}

	ui.SameLine();
	ui.Text(PathUtils::ToUtf8(assets.GetRootDirectory()));

	if (ui.InputText("Search", filter_)) {
		// 検索で隠れたモデルを誤って配置しないよう選択を解除する
		selectedPath_.clear();
	}

	const auto &models = assets.GetModels();

	if (!selectedPath_.empty() && std::find(models.begin(), models.end(),
	                                        selectedPath_) == models.end()) {
		selectedPath_.clear();
	}

	if (!assets.GetError().empty()) {
		ui.Text(assets.GetError());
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
}
