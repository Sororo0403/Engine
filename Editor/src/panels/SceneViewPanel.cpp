#include "panels/SceneViewPanel.h"
#include "assets/AssetDragDrop.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "math/MathUtils.h"

#include <algorithm>
#include <imgui.h>
#include <utility>

SceneViewPanel::SceneViewPanel(RenderSystem &render) : render_(render) {
}

SceneViewPanel::~SceneViewPanel() = default;

void SceneViewPanel::Show() {
	focusRequested_ = true;
}

bool SceneViewPanel::DrawUI(const ImGuiSystem &ui, TransformGizmo &gizmo,
                            TransformGizmo::SelectionMode selectionMode,
                            bool editable, bool allowReadOnlySelection) {
	visible_ = false;
	editable_ = editable;
	selectionEnabled_ = editable || allowReadOnlySelection;
	input_ = {};
	modelDrop_ = {};
	objectDrop_ = {};
	imageMinimum_ = {};
	imageSize_ = {};

	ui.SetNextWindowSize(800.0f, 500.0f);

	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
	}

	const bool open = ui.BeginWindow("Scene");

	if (focusRequested_) {
		ui.FocusWindow("Scene");
		focusRequested_ = false;
	}

	if (!open) {
		ui.EndWindow();
		CancelSelection();
		return true;
	}

	if (editable_) {
		gizmo.DrawToolbar(selectionMode);
	} else {
		gizmo.Cancel();

		if (!selectionEnabled_) {
			CancelSelection();
		}
	}

	const auto size = ui.GetContentSize();

	if (!MathUtils::IsFinite(size) || size.x < 1.0f || size.y < 1.0f) {
		ui.EndWindow();
		CancelSelection();
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

	if (editable_) {
		for (const auto kind :
		     {AssetDragDrop::Kind::MODEL, AssetDragDrop::Kind::SCENE,
		      AssetDragDrop::Kind::PREFAB}) {
			auto path = AssetDragDrop::AcceptTarget(kind);
			const auto mouse = ImGui::GetMousePos();
			if (!path.empty() && ImGui::IsMousePosValid() &&
			    mouse.x >= minimum.x && mouse.x < maximum.x &&
			    mouse.y >= minimum.y && mouse.y < maximum.y) {
				auto &drop = kind == AssetDragDrop::Kind::MODEL ? modelDrop_
				                                                : objectDrop_;
				drop.path = std::move(path);
				drop.imagePosition = {(mouse.x - minimum.x) / imageSize_.x,
				                      (mouse.y - minimum.y) / imageSize_.y};
			}
		}
	}

	visible_ = true;

	ui.EndWindow();
	return true;
}

TransformGizmo::Result SceneViewPanel::DrawGizmo(
    const ImGuiSystem &ui, TransformGizmo &gizmo,
    const DirectX::XMFLOAT4X4 &view, const DirectX::XMFLOAT4X4 &projection,
    SceneObjectId objectId, Transform *transform, bool enabled,
    TransformGizmo::SelectionMode selectionMode) const {
	if (!editable_ || !visible_ || !enabled || !transform || selecting_) {
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

const ViewportInput &SceneViewPanel::GetInput() const {
	return input_;
}

const SceneViewPanel::AssetDropRequest &SceneViewPanel::GetModelDrop() const {
	return modelDrop_;
}

SceneViewPanel::SelectionRequest
SceneViewPanel::UpdateSelection(const ImGuiSystem &ui, bool enabled,
                                bool gizmoBlocksSelection) {
	SelectionRequest request;
	const auto &io = ImGui::GetIO();

	if (!selectionEnabled_ || !enabled || !visible_ ||
	    !input_.applicationFocused || !input_.windowFocused ||
	    input_.widgetActive || input_.rightDown || input_.middleDown ||
	    io.KeyAlt || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
	    !ImGui::IsMousePosValid()) {
		CancelSelection();
		return request;
	}

	if (selecting_ && (imageMinimum_.x != selectionImageMinimum_.x ||
	                   imageMinimum_.y != selectionImageMinimum_.y ||
	                   imageSize_.x != selectionImageSize_.x ||
	                   imageSize_.y != selectionImageSize_.y)) {
		// レイアウトが変わった場合は、異なる画像範囲の座標を混在させない
		CancelSelection();
		return request;
	}

	if (!selecting_) {
		if (gizmoBlocksSelection || !input_.leftPressed ||
		    !input_.imageHovered || !input_.imagePositionValid) {
			return request;
		}

		selecting_ = true;
		selectionDragging_ = false;
		selectionAdditive_ = io.KeyCtrl;
		selectionStart_ = {input_.imageX, input_.imageY};
		selectionPressPosition_ = {io.MousePos.x, io.MousePos.y};
		selectionImageMinimum_ = imageMinimum_;
		selectionImageSize_ = imageSize_;
	}

	const float dx = io.MousePos.x - selectionPressPosition_.x;
	const float dy = io.MousePos.y - selectionPressPosition_.y;
	const float threshold = io.MouseDragThreshold;

	if (dx * dx + dy * dy > threshold * threshold) {
		selectionDragging_ = true;
	}

	const DirectX::XMFLOAT2 current{
	    std::clamp((io.MousePos.x - imageMinimum_.x) / imageSize_.x, 0.0f,
	               1.0f),
	    std::clamp((io.MousePos.y - imageMinimum_.y) / imageSize_.y, 0.0f,
	               1.0f),
	};
	const DirectX::XMFLOAT2 minimum{
	    (std::min)(selectionStart_.x, current.x),
	    (std::min)(selectionStart_.y, current.y),
	};
	const DirectX::XMFLOAT2 maximum{
	    (std::max)(selectionStart_.x, current.x),
	    (std::max)(selectionStart_.y, current.y),
	};

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		request.additive = selectionAdditive_;

		if (selectionDragging_) {
			request.action = SelectionRequest::Action::BOX;
			request.minimum = minimum;
			request.maximum = maximum;
		} else if (input_.imageHovered && input_.imagePositionValid) {
			request.action = SelectionRequest::Action::CLICK;
			request.minimum = selectionStart_;
			request.maximum = selectionStart_;
		}

		CancelSelection();
		return request;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		CancelSelection();
		return request;
	}

	if (selectionDragging_) {
		if (ui.BeginWindow("Scene")) {
			auto *drawList = ImGui::GetWindowDrawList();
			const ImVec2 imageMaximum{
			    imageMinimum_.x + imageSize_.x,
			    imageMinimum_.y + imageSize_.y,
			};
			const ImVec2 start{
			    imageMinimum_.x + minimum.x * imageSize_.x,
			    imageMinimum_.y + minimum.y * imageSize_.y,
			};
			const ImVec2 end{
			    imageMinimum_.x + maximum.x * imageSize_.x,
			    imageMinimum_.y + maximum.y * imageSize_.y,
			};
			drawList->PushClipRect({imageMinimum_.x, imageMinimum_.y},
			                       imageMaximum, true);
			drawList->AddRectFilled(start, end, IM_COL32(70, 150, 255, 40));
			drawList->AddRect(start, end, IM_COL32(90, 170, 255, 230));
			drawList->PopClipRect();
		}

		ui.EndWindow();
	}

	return request;
}

bool SceneViewPanel::IsSelecting() const {
	return selecting_;
}

void SceneViewPanel::CancelSelection() {
	selecting_ = false;
	selectionDragging_ = false;
}

void SceneViewPanel::Finalize() {
	focusRequested_ = false;
	CancelSelection();
	texture_.reset();
	input_ = {};
	modelDrop_ = {};
	objectDrop_ = {};
	visible_ = false;
}

const SceneViewPanel::AssetDropRequest &SceneViewPanel::GetObjectDrop() const {
	return objectDrop_;
}
