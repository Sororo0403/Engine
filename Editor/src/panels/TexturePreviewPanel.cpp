#include "panels/TexturePreviewPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "graphics/ui/UITexture.h"
#include "platform/windows/PathUtils.h"
#include <algorithm>
#include <format>
#include <imgui.h>

TexturePreviewPanel::TexturePreviewPanel() = default;
TexturePreviewPanel::~TexturePreviewPanel() = default;

void TexturePreviewPanel::Open(const std::filesystem::path &path) {
	if (path.empty()) {
		Clear();
		return;
	}

	auto selected = path;
	auto label = PathUtils::ToUtf8(path);
	path_.swap(selected);
	label_.swap(label);
	error_.clear();
	texture_.reset();
	open_ = focusRequested_ = reloadRequested_ = true;
}

void TexturePreviewPanel::Draw(const ImGuiSystem &ui) {
	if (!open_) {
		return;
	}

	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	ImGui::SetNextWindowSize({360, 360}, ImGuiCond_FirstUseEver);
	const bool visible = ImGui::Begin("Image Preview", &open_);

	if (!open_) {
		Clear();
		ImGui::End();
		return;
	}

	if (!visible) {
		ImGui::End();
		return;
	}

	ui.Text(label_);
	if (ui.Button("Reload Preview")) {
		reloadRequested_ = true;
	}

	ui.SameLine();
	if (ui.Button("Close Preview")) {
		Clear();
		ImGui::End();
		return;
	}

	if (reloadRequested_) {
		reloadRequested_ = false;
		texture_ = ui.LoadImageTexture(path_);
		error_ = texture_
		             ? ""
		             : "Could not preview this image. See Console for details.";
	}

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	if (texture_) {
		ui.Text(std::format("{} x {}", texture_->GetWidth(),
		                    texture_->GetHeight()));
		const auto available = ui.GetContentSize();

		if (available.x > 0 && available.y > 0) {
			const float scale = (std::min)(available.x / texture_->GetWidth(),
			                               available.y / texture_->GetHeight());
			ui.Image(*texture_, texture_->GetWidth() * scale,
			         texture_->GetHeight() * scale);
		}
	}

	ImGui::End();
}

void TexturePreviewPanel::Clear() {
	texture_.reset();
	path_.clear();
	label_.clear();
	error_.clear();
	open_ = focusRequested_ = reloadRequested_ = false;
}
