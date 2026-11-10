#include "ui/UIInspector.h"
#include "assets/AssetDragDrop.h"
#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneObject.h"
#include <format>
#include <imgui.h>
#include <imgui_stdlib.h>

namespace {
/// <summary>
/// 1つのUI入力欄の編集開始・変更・終了を、既存の編集履歴へ接続する
/// </summary>
/// <typeparam name="T">UIコンポーネントの型</typeparam>
/// <param name="history">編集履歴</param>
/// <param name="id">編集する物体</param>
/// <param name="current">現在のコンポーネント</param>
/// <param name="value">入力欄が変更した作業値</param>
/// <param name="edited">入力値の変化があったか</param>
/// <param name="begin">対応する連続編集の開始関数</param>
/// <returns>true:有効な値を反映 false:変更なしまたは無効</returns>
template <class T>
bool Apply(EditorHistory &history, SceneObjectId id, std::optional<T> &current,
           const T &value, bool edited,
           bool (EditorHistory::*begin)(SceneObjectId)) {
	const bool ready = !ImGui::IsItemActivated() || (history.*begin)(id);
	bool changed = false;

	if (ready && edited && current && value.IsValid() && value != *current &&
	    (history.IsEditing() || (history.*begin)(id))) {
		current = value;
		changed = true;
	}

	if (ImGui::IsItemDeactivated() && !ImGui::IsAnyItemActive())
		history.CommitEdit();
	return changed;
}
} // namespace

bool UIInspector::Draw(const ImGuiSystem &ui, SceneObjectId id,
                       SceneObject &object, EditorHistory &history,
                       std::span<const std::filesystem::path> images) const {
	const auto scope = std::to_string(id);
	ImGui::PushID(scope.c_str());
	ImGui::PushID("UIInspector");
	bool changed = false;

	if (object.canvas && ui.CollapsingHeader("Canvas")) {
		auto c = *object.canvas;
		if (ui.Checkbox("Canvas Enabled", c.enabled))
			changed |= history.SetCanvas(id, c);
		if (ui.Checkbox("Scale With Screen", c.scaleWithScreen))
			changed |= history.SetCanvas(id, c);
		changed |= Apply(
		    history, id, object.canvas, c,
		    ImGui::DragFloat2("Reference Resolution", c.referenceSize.data(), 1,
		                      1, 16384, "%.0f", ImGuiSliderFlags_AlwaysClamp),
		    &EditorHistory::BeginCanvas);
		changed |= Apply(history, id, object.canvas, c,
		                 ImGui::SliderFloat("Match Width / Height",
		                                    &c.matchWidthOrHeight, 0, 1),
		                 &EditorHistory::BeginCanvas);
		changed |=
		    Apply(history, id, object.canvas, c,
		          ImGui::DragInt("Canvas Sort Order", &c.sortOrder, 1, -10000,
		                         10000, "%d", ImGuiSliderFlags_AlwaysClamp),
		          &EditorHistory::BeginCanvas);
		if (ui.Button("Remove Canvas"))
			changed |= history.SetCanvas(id, std::nullopt);
	}

	if (object.rectTransform && ui.CollapsingHeader("Rect Transform")) {
		auto r = *object.rectTransform;

		if (ImGui::BeginCombo("Anchor Preset", "Choose...")) {
			if (ui.Selectable("Top Left", false)) {
				r.anchorMin = r.anchorMax = r.pivot = {0, 0};
				changed |= history.SetRectTransform(id, r);
			}

			if (ui.Selectable("Center", false)) {
				r.anchorMin = r.anchorMax = r.pivot = {.5f, .5f};
				if (r.sizeDelta[0] <= 0 || r.sizeDelta[1] <= 0)
					r.sizeDelta = {240, 60};
				changed |= history.SetRectTransform(id, r);
			}

			if (ui.Selectable("Bottom Right", false)) {
				r.anchorMin = r.anchorMax = r.pivot = {1, 1};
				changed |= history.SetRectTransform(id, r);
			}

			if (ui.Selectable("Stretch", false)) {
				r.anchorMin = {0, 0};
				r.anchorMax = {1, 1};
				r.pivot = {.5f, .5f};
				r.position = {0, 0};
				r.sizeDelta = {0, 0};
				changed |= history.SetRectTransform(id, r);
			}

			ImGui::EndCombo();
		}

		changed |=
		    Apply(history, id, object.rectTransform, r,
		          ImGui::DragFloat2("Anchor Min", r.anchorMin.data(), .01f, 0,
		                            1, "%.2f", ImGuiSliderFlags_AlwaysClamp),
		          &EditorHistory::BeginRectTransform);
		changed |=
		    Apply(history, id, object.rectTransform, r,
		          ImGui::DragFloat2("Anchor Max", r.anchorMax.data(), .01f, 0,
		                            1, "%.2f", ImGuiSliderFlags_AlwaysClamp),
		          &EditorHistory::BeginRectTransform);
		changed |=
		    Apply(history, id, object.rectTransform, r,
		          ImGui::DragFloat2("Pivot", r.pivot.data(), .01f, 0, 1, "%.2f",
		                            ImGuiSliderFlags_AlwaysClamp),
		          &EditorHistory::BeginRectTransform);
		changed |= Apply(history, id, object.rectTransform, r,
		                 ImGui::DragFloat2("UI Position", r.position.data(), 1,
		                                   -100000, 100000, "%.1f",
		                                   ImGuiSliderFlags_AlwaysClamp),
		                 &EditorHistory::BeginRectTransform);
		changed |= Apply(history, id, object.rectTransform, r,
		                 ImGui::DragFloat2("UI Size Delta", r.sizeDelta.data(),
		                                   1, -100000, 100000, "%.1f",
		                                   ImGuiSliderFlags_AlwaysClamp),
		                 &EditorHistory::BeginRectTransform);
		changed |= Apply(history, id, object.rectTransform, r,
		                 ImGui::DragFloat("UI Rotation (deg)", &r.rotationDegrees,
		                                  .5f, -36000, 36000, "%.2f",
		                                  ImGuiSliderFlags_AlwaysClamp),
		                 &EditorHistory::BeginRectTransform);
		if (ui.Button("Remove Rect Transform"))
			changed |= history.SetRectTransform(id, std::nullopt);
	}

	if (object.button && ui.CollapsingHeader("UI Button")) {
		auto b = *object.button;
		if (ui.Checkbox("Button Enabled", b.enabled))
			changed |= history.SetButton(id, b);
		if (ui.Checkbox("Interactable", b.interactable))
			changed |= history.SetButton(id, b);
		if (ui.Checkbox("Button Navigation", b.navigation))
			changed |= history.SetButton(id, b);
		changed |=
		    Apply(history, id, object.button, b,
		          ImGui::ColorEdit4("Normal Color", b.normalColor.data()),
		          &EditorHistory::BeginButton);
		changed |= Apply(
		    history, id, object.button, b,
		    ImGui::ColorEdit4("Highlighted Color", b.highlightedColor.data()),
		    &EditorHistory::BeginButton);
		changed |=
		    Apply(history, id, object.button, b,
		          ImGui::ColorEdit4("Pressed Color", b.pressedColor.data()),
		          &EditorHistory::BeginButton);
		changed |=
		    Apply(history, id, object.button, b,
		          ImGui::ColorEdit4("Disabled Color", b.disabledColor.data()),
		          &EditorHistory::BeginButton);
		if (ui.Button("Remove UI Button"))
			changed |= history.SetButton(id, std::nullopt);
	}

	if (object.text && ui.CollapsingHeader("UI Text")) {
		auto t = *object.text;
		if (ui.Checkbox("Text Enabled", t.enabled))
			changed |= history.SetText(id, t);
		if (ui.Checkbox("Text Raycast Target", t.raycastTarget))
			changed |= history.SetText(id, t);
		changed |= Apply(history, id, object.text, t,
		                 ImGui::InputTextMultiline("Text", &t.text, {0, 85}),
		                 &EditorHistory::BeginText);
		changed |= Apply(history, id, object.text, t,
		                 ui.InputText("Font Family", t.fontFamily),
		                 &EditorHistory::BeginText);
		changed |= Apply(history, id, object.text, t,
		                 ImGui::DragFloat("Font Size", &t.fontSize, .5f, 1, 512,
		                                  "%.1f", ImGuiSliderFlags_AlwaysClamp),
		                 &EditorHistory::BeginText);
		if (ui.Checkbox("Bold", t.bold))
			changed |= history.SetText(id, t);
		const char *horizontal[]{"Left", "Center", "Right"},
		    *vertical[]{"Top", "Middle", "Bottom"};
		if (ImGui::BeginCombo("Text Alignment",
		                      horizontal[static_cast<int>(t.alignment)])) {
			for (int i = 0; i < 3; ++i)
				if (ui.Selectable(horizontal[i],
				                  i == static_cast<int>(t.alignment))) {
					t.alignment = static_cast<TextComponent::Alignment>(i);
					changed |= history.SetText(id, t);
				}
			ImGui::EndCombo();
		}

		if (ImGui::BeginCombo(
		        "Vertical Alignment",
		        vertical[static_cast<int>(t.verticalAlignment)])) {
			for (int i = 0; i < 3; ++i)
				if (ui.Selectable(vertical[i],
				                  i == static_cast<int>(t.verticalAlignment))) {
					t.verticalAlignment =
					    static_cast<TextComponent::VerticalAlignment>(i);
					changed |= history.SetText(id, t);
				}
			ImGui::EndCombo();
		}
		changed |= Apply(history, id, object.text, t,
		                 ImGui::ColorEdit4("Text Color", t.color.data()),
		                 &EditorHistory::BeginText);
		if (ui.Button("Remove UI Text"))
			changed |= history.SetText(id, std::nullopt);
	}

	if (object.image && ui.CollapsingHeader("UI Image")) {
		auto i = *object.image;
		if (ui.Checkbox("Image Enabled", i.enabled))
			changed |= history.SetImage(id, i);
		if (ui.Checkbox("Image Raycast Target", i.raycastTarget))
			changed |= history.SetImage(id, i);
		const auto preview = i.texture.empty()
		                         ? std::string("Solid Color")
		                         : PathUtils::ToUtf8(i.texture.filename());
		if (ImGui::BeginCombo("UI Image Asset", preview.c_str())) {
			if (ui.Selectable("Solid Color", i.texture.empty())) {
				i.texture.clear();
				changed |= history.SetImage(id, i);
			}

			for (const auto &path : images)
				if (ui.Selectable(PathUtils::ToUtf8(path).c_str(),
				                  path == i.texture)) {
					i.texture = path;
					changed |= history.SetImage(id, i);
				}
			ImGui::EndCombo();
		}

		if (auto path =
		        AssetDragDrop::AcceptTarget(AssetDragDrop::Kind::TEXTURE);
		    !path.empty()) {
			i.texture = std::move(path);
			changed |= history.SetImage(id, i);
		}
		if (ui.Checkbox("Preserve Aspect", i.preserveAspect))
			changed |= history.SetImage(id, i);
		changed |= Apply(history, id, object.image, i,
		                 ImGui::ColorEdit4("Image Color", i.color.data()),
		                 &EditorHistory::BeginImage);
		const char *origins[]{"Left", "Right", "Top", "Bottom"};
		auto origin = static_cast<int>(i.fillOrigin);

		if (ImGui::Combo("Fill Origin", &origin, origins, 4)) {
			i.fillOrigin = static_cast<ImageComponent::FillOrigin>(origin);
			changed |= history.SetImage(id, i);
		}

		changed |=
		    Apply(history, id, object.image, i,
		          ImGui::SliderFloat("Fill Amount", &i.fillAmount, 0, 1, "%.3f",
		                             ImGuiSliderFlags_AlwaysClamp),
		          &EditorHistory::BeginImage);
		if (ui.Button("Remove UI Image"))
			changed |= history.SetImage(id, std::nullopt);
	}

	ImGui::PopID();
	ImGui::PopID();
	return changed;
}

void UIInspector::DrawReadOnly(const ImGuiSystem &ui,
                               const SceneObject &object) const {
	if (object.canvas && ui.CollapsingHeader("Canvas")) {
		const auto &c = *object.canvas;
		ui.Text(c.enabled ? "Enabled" : "Disabled");
		ui.Text(std::format("Reference: {:.0f} x {:.0f}", c.referenceSize[0],
		                    c.referenceSize[1]));
		ui.Text(std::format("Sort Order: {}", c.sortOrder));
	}

	if (object.rectTransform && ui.CollapsingHeader("Rect Transform")) {
		const auto &r = *object.rectTransform;
		ui.Text(std::format("Position: {:.1f}, {:.1f}", r.position[0],
		                    r.position[1]));
		ui.Text(std::format("Size Delta: {:.1f}, {:.1f}", r.sizeDelta[0],
		                    r.sizeDelta[1]));
	}

	if (object.button && ui.CollapsingHeader("UI Button")) {
		ui.Text(object.button->enabled ? "Enabled" : "Disabled");
		ui.Text(object.button->interactable ? "Interactable: Yes"
		                                    : "Interactable: No");
		ui.Text(object.button->navigation ? "Navigation: Yes"
		                                  : "Navigation: No");
	}

	if (object.text && ui.CollapsingHeader("UI Text")) {
		ui.Text(object.text->enabled ? "Enabled" : "Disabled");
		ImGui::TextWrapped("%s", object.text->text.c_str());
		ui.Text(std::format("Font: {} / {:.1f}", object.text->fontFamily,
		                    object.text->fontSize));
	}

	if (object.image && ui.CollapsingHeader("UI Image")) {
		ui.Text(object.image->enabled ? "Enabled" : "Disabled");
		ui.Text(object.image->texture.empty()
		            ? "Solid Color"
		            : PathUtils::ToUtf8(object.image->texture));
		ui.Text(std::format("Fill Amount: {:.3f}", object.image->fillAmount));
	}
}
