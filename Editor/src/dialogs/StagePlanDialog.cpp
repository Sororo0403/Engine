#include "dialogs/StagePlanDialog.h"

#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneDocument.h"

#include <algorithm>
#include <imgui.h>
#include <stdexcept>
#include <string_view>
#include <utility>

void StagePlanDialog::Show(const std::filesystem::path &projectRoot) {
	if (projectRoot_ != projectRoot) {
		projectRoot_ = projectRoot;
		brief_ = {
		    {"updraft", "Ride the updraft", StagePlan::WindRequirement::UPDRAFT, true,
		     "Teach the player to rise on the wind from a wide, safe launch platform."},
		    {"crosswind", "Land through the crosswind", StagePlan::WindRequirement::CROSSWIND, true,
		     "Turn the route and use a side current to reach a clearly visible landing."},
		};
		layout_ = {};
		selected_ = 0;
		nextSectionId_ = 3;
		sceneName_ = "Skywind Stage Plan";
		reviseSelected_ = false;
		savedPath_.clear();
	}
	message_.clear();
	error_.clear();
	opening_ = true;
}

StagePlanDialog::Result StagePlanDialog::Draw(
    const ImGuiSystem &ui, SceneDocument &document,
    const std::filesystem::path &scenesDirectory) {
	constexpr auto title = "Stage Plan###StagePlanDialog";
	if (std::exchange(opening_, false)) {
		ui.OpenPopup(title);
	}
	ImGui::SetNextWindowSize({620, 0}, ImGuiCond_Appearing);
	if (!ui.BeginPopupModal(title)) {
		return Result::NONE;
	}

	DrawSections(ui);
	ImGui::Separator();

	const bool hasLayout = HasLayout();
	if (!hasLayout) {
		reviseSelected_ = false;
	}
	ui.BeginDisabled(!hasLayout);
	ui.Checkbox("Revise only selected section", reviseSelected_);
	ui.EndDisabled();
	if (reviseSelected_) {
		ui.Text("Fixed: the other sections and both connections.");
	}

	if (ui.Button("Copy ChatGPT Prompt")) {
		CopyPrompt();
	}
	ui.SameLine();
	if (ui.Button("Import Layout from Clipboard")) {
		ImportClipboard();
	}
	ImGui::Separator();

	ui.InputText("Scene Name", sceneName_);
	ui.BeginDisabled(!HasLayout());
	if (ui.Button("Save New Scene")) {
		Save(document, scenesDirectory);
	}
	ui.EndDisabled();
	ui.SameLine();
	ui.BeginDisabled(savedPath_.empty());
	const bool open = ui.Button("Open Saved Scene");
	ui.EndDisabled();
	ui.SameLine();
	const bool close = ui.Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	} else if (!message_.empty()) {
		ImGui::TextWrapped("%s", message_.c_str());
	}
	if (open || close) {
		ui.CloseCurrentPopup();
	}
	ui.EndPopup();

	if (open) {
		return Result::OPEN_SAVED_SCENE;
	}
	return close ? Result::CLOSED : Result::NONE;
}

/// <summary>
/// 進行順を選択・変更し、選んだ区間の必須条件と設計の希望を編集する
/// </summary>
void StagePlanDialog::DrawSections(const ImGuiSystem &ui) {
	if (ImGui::BeginListBox("Sections", {-1, 118})) {
		for (std::size_t i = 0; i < brief_.size(); ++i) {
			ImGui::PushID(brief_[i].id.c_str());
			const auto label = std::to_string(i + 1) + ". " + brief_[i].name;
			if (ImGui::Selectable(label.c_str(), selected_ == i)) {
				selected_ = i;
			}
			ImGui::PopID();
		}
		ImGui::EndListBox();
	}
	ui.BeginDisabled(selected_ == 0);
	if (ui.Button("Move Up")) {
		std::swap(brief_[selected_], brief_[selected_ - 1]);
		--selected_;
	}
	ui.EndDisabled();
	ui.SameLine();
	ui.BeginDisabled(selected_ + 1 >= brief_.size());
	if (ui.Button("Move Down")) {
		std::swap(brief_[selected_], brief_[selected_ + 1]);
		++selected_;
	}
	ui.EndDisabled();
	ui.SameLine();
	ui.BeginDisabled(brief_.size() >= 8);
	if (ui.Button("Add Section")) {
		const auto number = std::to_string(nextSectionId_++);
		brief_.push_back({"section_" + number, "Section " + number,
		                  StagePlan::WindRequirement::UPDRAFT, true, {}});
		selected_ = brief_.size() - 1;
	}
	ui.EndDisabled();
	ui.SameLine();
	ui.BeginDisabled(brief_.size() <= 1);
	if (ui.Button("Remove Section")) {
		brief_.erase(brief_.begin() + static_cast<std::ptrdiff_t>(selected_));
		selected_ = std::min(selected_, brief_.size() - 1);
	}
	ui.EndDisabled();

	auto &section = brief_[selected_];
	ui.InputText("Section Name", section.name);
	int wind = section.wind == StagePlan::WindRequirement::UPDRAFT ? 0 : 1;
	if (ImGui::Combo("Wind", &wind, "Updraft\0Crosswind\0\0")) {
		section.wind = wind == 0 ? StagePlan::WindRequirement::UPDRAFT
		                        : StagePlan::WindRequirement::CROSSWIND;
	}
	ui.Checkbox("Wind must be needed to pass", section.windIsRequired);
	ui.InputText("Design Intent", section.intent);
}

void StagePlanDialog::CopyPrompt() {
	error_.clear();
	message_.clear();
	try {
		const auto revise = reviseSelected_
		                        ? std::optional<std::string_view>{brief_[selected_].id}
		                        : std::nullopt;
		const auto prompt = StagePlan::MakePrompt(brief_, &layout_, revise);
		if (prompt.empty()) {
			throw std::runtime_error("Import a full layout before revising a section.");
		}
		ImGui::SetClipboardText(prompt.c_str());
		message_ = "Prompt copied. Paste it into ChatGPT, then copy the JSON response.";
	} catch (const std::exception &error) {
		error_ = error.what();
	}
}

void StagePlanDialog::ImportClipboard() {
	error_.clear();
	message_.clear();
	const char *clipboard = ImGui::GetClipboardText();
	if (!clipboard || !*clipboard) {
		error_ = "Copy the layout JSON to the clipboard first.";
		return;
	}

	const auto revise = reviseSelected_
	                        ? std::optional<std::string_view>{brief_[selected_].id}
	                        : std::nullopt;
	const std::string_view text{clipboard};
	// ChatGPTの応答全体をコピーした場合、単独のMarkdownコードフェンスも受け付ける。
	const auto first = text.find_first_not_of(" \t\r\n");
	const auto last = text.find_last_not_of(" \t\r\n");
	auto json = first == std::string_view::npos ? text : text.substr(first, last - first + 1);
	if (json.starts_with("```") && json.ends_with("```")) {
		const auto lineEnd = json.find('\n');
		if (lineEnd != std::string_view::npos && lineEnd + 1 < json.size() - 3) {
			json = json.substr(lineEnd + 1, json.size() - lineEnd - 4);
		}
	}
	if (StagePlan::Import(json, brief_, revise, layout_, error_)) {
		savedPath_.clear();
		message_ = revise ? "Selected section updated." : "Layout imported.";
	}
}

void StagePlanDialog::Save(SceneDocument &document,
                           const std::filesystem::path &scenesDirectory) {
	error_.clear();
	message_.clear();
	try {
		auto name = PathUtils::FromUtf8(sceneName_);
		if (!name.native().ends_with(L".scene.json")) {
			name += L".scene.json";
		}
		if (sceneName_.empty() || !PathUtils::IsValidFileName(name)) {
			throw std::runtime_error("Enter a valid name for the new scene.");
		}
		const auto destination = scenesDirectory / name;
		if (std::filesystem::exists(destination)) {
			throw std::runtime_error("A scene with that name already exists.");
		}
		Scene generated;
		if (!StagePlan::BuildScene(layout_, projectRoot_, generated, error_)) {
			return;
		}
		if (!document.SaveCopy(destination, generated, false)) {
			error_ = document.GetError();
			return;
		}
		savedPath_ = destination;
		message_ = "Saved " + PathUtils::ToUtf8(destination.filename());
	} catch (const std::exception &error) {
		error_ = error.what();
	}
}

bool StagePlanDialog::HasLayout() const {
	return std::ranges::equal(layout_.sections, brief_, {}, &StagePlan::Section::id,
	                          &StagePlan::SectionBrief::id);
}
