#include "gizmo/TransformGizmo.h"

#include "math/MathUtils.h"
#include "math/TransformUtils.h"

#include <imgui.h>

#include <ImGuizmo.h>

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

	ImGui::SameLine();

	if (ImGui::RadioButton("Rotate", operation_ == Operation::ROTATE)) {
		operation_ = Operation::ROTATE;
	}

	ImGui::SameLine();

	if (ImGui::RadioButton("Scale", operation_ == Operation::SCALE)) {
		operation_ = Operation::SCALE;
	}

	ImGui::EndDisabled();

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

	// 最初の実装では、正の拡大率を持つ物体を対象にする
	if (transform->scale.x <= 0.0f || transform->scale.y <= 0.0f ||
	    transform->scale.z <= 0.0f) {
		Cancel();
		return result;
	}

	if (objectId_ != objectId || selectionMode_ != selectionMode) {
		Cancel();
		objectId_ = objectId;
		selectionMode_ = selectionMode;
	}

	using namespace DirectX;

	XMFLOAT4X4 matrix;
	XMStoreFloat4x4(&matrix, TransformUtils::MakeWorldMatrix(*transform));

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

	const ImGuizmo::MODE mode = selectionMode == SelectionMode::MULTIPLE
	                                ? ImGuizmo::WORLD
	                                : ImGuizmo::LOCAL;
	// 複数選択の拡縮は軸を隠し、均等拡縮用の中央ハンドルだけを使用する
	ImGuizmo::SetAxisMask(uniformScale, uniformScale, uniformScale);
	const bool manipulated = ImGuizmo::Manipulate(
	    &view._11, &projection._11, operation, mode, &matrix._11);

	using_ = ImGuizmo::IsUsing();

	// 引数ありのIsOverで、現在の操作に対する当たり判定を取得する
	result.blocksSelection = wasUsing || using_ || ImGuizmo::IsOver(operation);
	ImGuizmo::SetAxisMask(false, false, false);

	if (!manipulated) {
		return result;
	}

	Transform decomposed;

	if (!TransformUtils::TryFromWorldMatrix(matrix, decomposed)) {
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
		edited.scale = uniformScale
		                   ? XMFLOAT3{decomposed.scale.x, decomposed.scale.x,
		                              decomposed.scale.x}
		                   : decomposed.scale;
		break;
	}

	if (!TransformUtils::IsValid(edited) || edited.scale.x <= 0.0f ||
	    edited.scale.y <= 0.0f || edited.scale.z <= 0.0f) {
		return result;
	}

	*transform = edited;
	result.changed = true;

	return result;
}

bool TransformGizmo::IsUsing() const { return using_; }

void TransformGizmo::Cancel() {
	ImGuizmo::Enable(false);

	objectId_ = 0;
	using_ = false;
}
