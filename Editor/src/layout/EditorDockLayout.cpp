#include "layout/EditorDockLayout.h"

#include "graphics/ui/ImGuiSystem.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>

void EditorDockLayout::Draw(const ImGuiSystem &ui, bool resetLayout) const {
	if (!std::isfinite(leftRatio_) || !std::isfinite(rightRatio_) ||
	    !std::isfinite(bottomRatio_) || leftRatio_ <= 0.0f ||
	    rightRatio_ <= 0.0f || leftRatio_ + rightRatio_ >= 1.0f ||
	    bottomRatio_ <= 0.0f || bottomRatio_ >= 1.0f) {
		throw std::invalid_argument("EditorDockLayout: 配置の割合が無効です。");
	}

	const std::uint32_t dockspaceId = ui.GetDockSpaceId(dockspaceName_.c_str());

	// 保存済みの配置がある場合は、ユーザーの変更を維持する
	if (resetLayout || !ui.HasDockNode(dockspaceId)) {
		if (!ui.ResetDockSpace(dockspaceId)) {
			return;
		}

		std::uint32_t upperId = 0;
		std::uint32_t bottomId = 0;
		ui.SplitDockNode(dockspaceId, DockDirection::DOWN, bottomRatio_,
		                 bottomId, upperId);

		std::uint32_t centerId = 0;
		std::uint32_t leftId = 0;
		ui.SplitDockNode(upperId, DockDirection::LEFT, leftRatio_, leftId,
		                 centerId);

		std::uint32_t rightId = 0;
		// 左領域を除いた残りの幅に対する割合へ変換する
		const float rightRatio = rightRatio_ / (1.0f - leftRatio_);
		ui.SplitDockNode(centerId, DockDirection::RIGHT, rightRatio, rightId,
		                 centerId);

		ui.DockWindow(hierarchyWindow_.c_str(), leftId);
		ui.DockWindow(sceneWindow_.c_str(), centerId);
		ui.DockWindow(inspectorWindow_.c_str(), rightId);
		ui.DockWindow(assetsWindow_.c_str(), bottomId);
		ui.FinishDockLayout(dockspaceId);
	}

	ui.DrawDockSpace(dockspaceId);
}
