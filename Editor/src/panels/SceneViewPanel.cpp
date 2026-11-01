#include "panels/SceneViewPanel.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "math/MathUtils.h"

#include <algorithm>
#include <imgui.h>

SceneViewPanel::SceneViewPanel(RenderSystem &render) : render_(render) {}

SceneViewPanel::~SceneViewPanel() = default;

bool SceneViewPanel::DrawUI(const ImGuiSystem &ui, TransformGizmo &gizmo,
                            TransformGizmo::SelectionMode selectionMode) {
	visible_ = false;
	input_ = {};
	imageMinimum_ = {};
	imageSize_ = {};

	ui.SetNextWindowSize(800.0f, 500.0f);

	if (!ui.BeginWindow("Scene")) {
		ui.EndWindow();
		return true;
	}

	gizmo.DrawToolbar(selectionMode);

	const auto size = ui.GetContentSize();

	if (!MathUtils::IsFinite(size) || size.x < 1.0f || size.y < 1.0f) {
		ui.EndWindow();
		return true;
	}

	// 極端なパネルサイズで巨大な描画先を作らないよう制限する
	const int width = static_cast<int>(std::clamp(size.x, 1.0f, 8192.0f));

	const int height = static_cast<int>(std::clamp(size.y, 1.0f, 8192.0f));

	if (!texture_) {
		texture_ = render_.CreateRenderTexture(width, height);

		if (!texture_) {
			ui.EndWindow();
			return false;
		}
	} else if (!texture_->Resize(width, height)) {
		ui.EndWindow();
		return false;
	}

	ui.Image(*texture_, static_cast<float>(width), static_cast<float>(height));

	// 最後に表示した項目が画像である間に取得する
	const ImVec2 minimum = ImGui::GetItemRectMin();
	const ImVec2 maximum = ImGui::GetItemRectMax();

	imageMinimum_ = {minimum.x, minimum.y};
	imageSize_ = {
	    maximum.x - minimum.x,
	    maximum.y - minimum.y,
	};

	input_ = ui.GetViewportInput();
	visible_ = true;

	ui.EndWindow();
	return true;
}

TransformGizmo::Result SceneViewPanel::DrawGizmo(
    const ImGuiSystem &ui, TransformGizmo &gizmo,
    const DirectX::XMFLOAT4X4 &view, const DirectX::XMFLOAT4X4 &projection,
    SceneObjectId objectId, Transform *transform, bool enabled,
    TransformGizmo::SelectionMode selectionMode) const {
	if (!visible_ || !enabled || !transform) {
		gizmo.Cancel();
		return {};
	}

	// 同じフレームのSceneウィンドウへ、ギズモの描画だけを追加する
	if (!ui.BeginWindow("Scene")) {
		ui.EndWindow();
		gizmo.Cancel();
		return {};
	}

	const auto result = gizmo.Draw(view, projection, imageMinimum_, imageSize_,
	                               objectId, transform, enabled, selectionMode);

	ui.EndWindow();
	return result;
}

RenderTexture *SceneViewPanel::GetRenderTexture() const {
	return visible_ ? texture_.get() : nullptr;
}

const ViewportInput &SceneViewPanel::GetInput() const { return input_; }

void SceneViewPanel::Finalize() {
	texture_.reset();
	input_ = {};
	visible_ = false;
}
