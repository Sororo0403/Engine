#include "gameplay/FollowTargetPicker.h"

#include "gameplay/SceneCameraUtils.h"
#include "graphics/ui/ImGuiSystem.h"
#include "scene/Scene.h"
#include <algorithm>
#include <imgui.h>
#include <string_view>

namespace {
/// <summary>
/// ASCII英字の大文字・小文字を区別せず、表示名とIDの部分一致を確認する
/// </summary>
/// <param name="label">検索する表示文字列</param>
/// <param name="query">入力した検索文字列。空の場合は全件に一致する</param>
/// <returns>true:一致 false:不一致</returns>
bool Matches(std::string_view label, std::string_view query) {
	const auto lower = [](unsigned char value) {
		return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
	};

	return query.empty() ||
	       std::search(label.begin(), label.end(), query.begin(), query.end(),
	                   [&](unsigned char a, unsigned char b) {
		                   return lower(a) == lower(b);
	                   }) != label.end();
}
} // namespace

std::optional<SceneObjectId> FollowTargetPicker::Draw(const ImGuiSystem &ui,
                                                      const Scene &scene,
                                                      SceneObjectId owner,
                                                      SceneObjectId current) {
	if (!scene.Contains(owner)) {
		return std::nullopt;
	}

	const auto *target = scene.FindObject(current);
	const auto preview =
	    target ? (target->name.empty() ? std::string{"Object"} : target->name) +
	                 " (ID " + std::to_string(current) + ")"
	    : current == 0 ? std::string{"None"}
	                   : "Missing target (ID " + std::to_string(current) + ")";
	const auto scope = std::to_string(owner);
	ImGui::PushID(scope.c_str());
	std::optional<SceneObjectId> chosen;

	if (ImGui::BeginCombo("Target", preview.c_str(),
	                      ImGuiComboFlags_HeightLarge)) {
		if (ImGui::IsWindowAppearing()) {
			search_.clear();
			ImGui::SetKeyboardFocusHere();
		}

		ui.InputText("Search", search_);

		if (ImGui::Selectable("None###NoFollowTarget", current == 0)) {
			chosen = 0;
		}

		bool found = false;

		for (const auto &[id, object] : scene.GetObjects()) {
			const auto display =
			    (object.name.empty() ? std::string{"Object"} : object.name) +
			    " (ID " + std::to_string(id) + ")";

			if (!Matches(display, search_)) {
				continue;
			}

			found = true;
			const bool allowed =
			    SceneCameraUtils::CanSetFollowTarget(scene, owner, id);
			const auto label = display + "###" + std::to_string(id);
			ImGui::BeginDisabled(!allowed);

			if (ImGui::Selectable(label.c_str(), current == id)) {
				chosen = id;
			}

			ImGui::EndDisabled();
		}

		if (!found) {
			ImGui::TextDisabled("No matching objects.");
		}

		ImGui::EndCombo();
	}

	ImGui::PopID();
	return chosen;
}

void FollowTargetPicker::Clear() noexcept {
	search_.clear();
}
