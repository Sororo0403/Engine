#include "gizmo/TransformGizmo.h"

#include "math/MathUtils.h"
#include "math/TransformUtils.h"

#include <imgui.h>

#include <ImGuizmo.h>

#include <cmath>

bool TransformGizmo::SetSnapping(bool enabled) {
	if (using_) {
		return false;
	}

	snapping_ = enabled;
	return true;
}

bool TransformGizmo::SetSpace(Space space) {
	if (using_ || (space != Space::LOCAL && space != Space::WORLD)) {
		return false;
	}

	space_ = space;
	return true;
}

TransformGizmo::Space TransformGizmo::GetSpace() const {
	return space_;
}

bool TransformGizmo::SetPivotMode(SelectionPivot::Mode mode) {
	if (using_ || (mode != SelectionPivot::Mode::CENTER &&
	               mode != SelectionPivot::Mode::ACTIVE)) {
		return false;
	}

	pivotMode_ = mode;
	return true;
}

SelectionPivot::Mode TransformGizmo::GetPivotMode() const {
	return pivotMode_;
}

TransformGizmo::Operation TransformGizmo::GetOperation() const {
	return operation_;
}

bool TransformGizmo::IsSnapping() const {
	return snapping_;
}

TransformGizmo::SnapSettings TransformGizmo::GetSnapSettings() const {
	return snapSettings_;
}

bool TransformGizmo::SetSnapSettings(const SnapSettings &settings) {
	if (using_ || !MathUtils::IsFinite(settings.translation) ||
	    settings.translation.x <= 0.0f || settings.translation.y <= 0.0f ||
	    settings.translation.z <= 0.0f ||
	    !std::isfinite(settings.rotationDegrees) ||
	    settings.rotationDegrees <= 0.0f || settings.rotationDegrees > 360.0f ||
	    !std::isfinite(settings.scaleRatio) || settings.scaleRatio <= 0.0f) {
		return false;
	}

	snapSettings_ = settings;
	return true;
}

bool TransformGizmo::SetOperation(Operation operation) {
	if (using_) {
		return false;
	}

	switch (operation) {
	case Operation::TRANSLATE:
	case Operation::ROTATE:
	case Operation::SCALE:
		operation_ = operation;
		return true;
	}

	return false;
}

void TransformGizmo::BeginFrame() {
	ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
	ImGuizmo::BeginFrame();
}

void TransformGizmo::DrawToolbar(SelectionMode selectionMode) {
	const bool multiple = selectionMode == SelectionMode::MULTIPLE;

	// ドラッグ中に操作種別を切り替えない
	ImGui::BeginDisabled(using_);

	if (ImGui::RadioButton("Move", operation_ == Operation::TRANSLATE)) {
		operation_ = Operation::TRANSLATE;
	}

	ImGui::SetItemTooltip("Move (W)");

	ImGui::SameLine();

	if (ImGui::RadioButton("Rotate", operation_ == Operation::ROTATE)) {
		operation_ = Operation::ROTATE;
	}

	ImGui::SetItemTooltip("Rotate (E)");

	ImGui::SameLine();

	if (ImGui::RadioButton("Scale", operation_ == Operation::SCALE)) {
		operation_ = Operation::SCALE;
	}

	ImGui::SetItemTooltip("Scale (R)");
	ImGui::SameLine();
	const bool fixedSpace = multiple || operation_ == Operation::SCALE;
	const bool world = multiple || (!fixedSpace && space_ == Space::WORLD);
	ImGui::BeginDisabled(fixedSpace);

	if (ImGui::Button(world ? "World###GizmoSpace" : "Local###GizmoSpace")) {
		SetSpace(space_ == Space::LOCAL ? Space::WORLD : Space::LOCAL);
	}

	ImGui::EndDisabled();
	ImGui::SetItemTooltip(multiple ? "Multiple selection uses world axes."
	                      : operation_ == Operation::SCALE
	                          ? "Single-object scale uses local axes."
	                          : "Toggle Local / World (X)");
	ImGui::SameLine();
	ImGui::Checkbox("Snap", &snapping_);
	ImGui::SetItemTooltip("Snap gizmo edits and model drops. Drops use the "
	                      "Move step in world coordinates.");
	ImGui::SameLine();
	ImGui::BeginDisabled(!multiple);
	const bool activePivot =
	    !multiple || pivotMode_ == SelectionPivot::Mode::ACTIVE;

	if (ImGui::Button(activePivot ? "Active###GizmoPivot"
	                              : "Center###GizmoPivot")) {
		SetPivotMode(pivotMode_ == SelectionPivot::Mode::CENTER
		                 ? SelectionPivot::Mode::ACTIVE
		                 : SelectionPivot::Mode::CENTER);
	}

	ImGui::EndDisabled();
	ImGui::SetItemTooltip(
	    multiple
	        ? "Center: mean of object origins. Active: active object's origin."
	        : "A single object uses its own origin.");
	ImGui::SameLine();

	if (ImGui::Button("Snap Settings")) {
		ImGui::OpenPopup("GizmoSnapSettings");
	}

	ImGui::EndDisabled();

	if (ImGui::BeginPopup("GizmoSnapSettings")) {
		ImGui::BeginDisabled(using_);
		SnapSettings edited = snapSettings_;
		bool changed = ImGui::DragFloat3("Move step", &edited.translation.x,
		                                 0.1f, 0.001f, 10000.0f, "%.3f");
		changed |= ImGui::DragFloat("Rotation step", &edited.rotationDegrees,
		                            0.5f, 0.1f, 360.0f, "%.1f deg");
		changed |= ImGui::DragFloat("Scale step (ratio)", &edited.scaleRatio,
		                            0.01f, 0.001f, 10.0f, "%.3f");

		if (changed) {
			SetSnapSettings(edited);
		}

		ImGui::EndDisabled();
		ImGui::EndPopup();
	}

	if (multiple && operation_ == Operation::SCALE) {
		ImGui::SameLine();
		ImGui::TextUnformatted("Drag the center to scale uniformly.");
	}
}

TransformGizmo::Result TransformGizmo::Draw(
    const DirectX::XMFLOAT4X4 &view, const DirectX::XMFLOAT4X4 &projection,
    const DirectX::XMFLOAT2 &imageMinimum, const DirectX::XMFLOAT2 &imageSize,
    SceneObjectId objectId, Transform *transform, bool enabled,
    SelectionMode selectionMode) {
	Result result;

	if (!enabled || objectId == 0 || !transform ||
	    !TransformUtils::IsValid(*transform) ||
	    !MathUtils::IsFinite(imageMinimum) || !MathUtils::IsFinite(imageSize) ||
	    imageSize.x <= 0.0f || imageSize.y <= 0.0f) {
		Cancel();
		return result;
	}

	// 複数選択の共有ピボットには、引き続き正の拡大率を使用する。
	if (selectionMode == SelectionMode::MULTIPLE &&
	    (transform->scale.x <= 0.0f || transform->scale.y <= 0.0f ||
	     transform->scale.z <= 0.0f)) {
		Cancel();
		return result;
	}

	if (objectId_ != objectId || selectionMode_ != selectionMode) {
		Cancel();
		objectId_ = objectId;
		selectionMode_ = selectionMode;
	}

	using namespace DirectX;

	// 軸の向きは回転から求め、反転の符号は物体側で保持する。
	Transform gizmoTransform = *transform;
	gizmoTransform.scale = {std::abs(transform->scale.x),
	                        std::abs(transform->scale.y),
	                        std::abs(transform->scale.z)};
	XMFLOAT4X4 matrix;
	XMStoreFloat4x4(&matrix, TransformUtils::MakeWorldMatrix(gizmoTransform));

	// 原点が近クリップ面より後ろにある場合は表示しない
	const XMVECTOR origin =
	    XMVectorSet(matrix._41, matrix._42, matrix._43, 1.0f);

	const XMVECTOR clipPosition = XMVector4Transform(
	    origin, XMLoadFloat4x4(&view) * XMLoadFloat4x4(&projection));

	if (XMVectorGetZ(clipPosition) < 0.001f) {
		Cancel();
		return result;
	}

	const Operation effectiveOperation = operation_;
	const bool uniformScale = selectionMode == SelectionMode::MULTIPLE &&
	                          effectiveOperation == Operation::SCALE;
	ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;

	switch (effectiveOperation) {
	case Operation::TRANSLATE:
		operation = ImGuizmo::TRANSLATE;
		break;

	case Operation::ROTATE:
		operation = ImGuizmo::ROTATE;
		break;

	case Operation::SCALE:
		operation = ImGuizmo::SCALE;
		break;
	}

	const bool wasUsing = using_;

	ImGuizmo::Enable(true);
	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

	// ウィンドウ全体ではなく、Scene画像の範囲を指定する
	ImGuizmo::SetRect(imageMinimum.x, imageMinimum.y, imageSize.x, imageSize.y);

	const bool world =
	    selectionMode == SelectionMode::MULTIPLE ||
	    (effectiveOperation != Operation::SCALE && space_ == Space::WORLD);
	const ImGuizmo::MODE mode = world ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
	// 複数選択の拡縮は軸を隠し、均等拡縮用の中央ハンドルだけを使用する
	ImGuizmo::SetAxisMask(uniformScale, uniformScale, uniformScale);
	const float *snap = nullptr;

	if (snapping_) {
		switch (effectiveOperation) {
		case Operation::TRANSLATE:
			snap = &snapSettings_.translation.x;
			break;
		case Operation::ROTATE:
			snap = &snapSettings_.rotationDegrees;
			break;
		case Operation::SCALE:
			snap = &snapSettings_.scaleRatio;
			break;
		}
	}

	const bool manipulated =
	    ImGuizmo::Manipulate(&view._11, &projection._11, operation, mode,
	                         &matrix._11, nullptr, snap);

	using_ = ImGuizmo::IsUsing();

	// 引数ありのIsOverで、現在の操作に対する当たり判定を取得する
	result.blocksSelection = wasUsing || using_ || ImGuizmo::IsOver(operation);
	ImGuizmo::SetAxisMask(false, false, false);

	if (!manipulated) {
		return result;
	}

	Transform decomposed;

	if (!TransformUtils::TryFromWorldMatrix(matrix, decomposed) ||
	    decomposed.scale.x <= 0.0f || decomposed.scale.y <= 0.0f ||
	    decomposed.scale.z <= 0.0f) {
		return result;
	}

	Transform edited = *transform;

	// 操作対象以外の成分へ、分解による誤差を蓄積させない
	switch (effectiveOperation) {
	case Operation::TRANSLATE:
		edited.translation = decomposed.translation;
		break;

	case Operation::ROTATE:
		edited.rotation = decomposed.rotation;
		break;

	case Operation::SCALE:
		// 行列分解の誤差で、複数選択の各軸に異なる倍率を蓄積させない
		edited.scale =
		    uniformScale
		        ? XMFLOAT3{decomposed.scale.x, decomposed.scale.x,
		                   decomposed.scale.x}
		        : XMFLOAT3{
		              std::copysign(decomposed.scale.x, transform->scale.x),
		              std::copysign(decomposed.scale.y, transform->scale.y),
		              std::copysign(decomposed.scale.z, transform->scale.z)};
		break;
	}

	if (!TransformUtils::IsValid(edited)) {
		return result;
	}

	*transform = edited;
	result.changed = true;

	return result;
}

bool TransformGizmo::IsUsing() const {
	return using_;
}

void TransformGizmo::Cancel() {
	ImGuizmo::Enable(false);

	objectId_ = 0;
	using_ = false;
}
