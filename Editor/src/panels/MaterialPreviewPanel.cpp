#include "panels/MaterialPreviewPanel.h"

#include "core/Logger.h"
#include "graphics/material/MaterialAsset.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <algorithm>
#include <exception>
#include <format>
#include <imgui.h>
#include <stdexcept>

MaterialPreviewPanel::MaterialPreviewPanel(ModelManager &models,
                                           RenderSystem &render)
    : models_(models), render_(render) {
}

MaterialPreviewPanel::~MaterialPreviewPanel() = default;

void MaterialPreviewPanel::Open() {
	open_ = focusRequested_ = reloadRequested_ = true;
}

bool MaterialPreviewPanel::HasFocus() const {
	return focused_;
}

void MaterialPreviewPanel::DrawUI(const ImGuiSystem &ui,
                                  const MaterialAsset *draft,
                                  const std::filesystem::path &path) {
	ready_ = false;
	focused_ = false;

	if (!open_) {
		return;
	}

	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	ImGui::SetNextWindowSize({360, 400}, ImGuiCond_FirstUseEver);
	const bool visible = ImGui::Begin("Material Preview", &open_);
	focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (!open_) {
		Clear();
		ImGui::End();
		return;
	}

	if (!visible) {
		ImGui::End();
		return;
	}

	if (ui.Button("Reload Material Preview")) {
		reloadRequested_ = true;
	}

	if (ui.Button("Close Material Preview")) {
		Clear();
		ImGui::End();
		return;
	}

	try {
		if (!draft || !draft->IsValid() || path.empty()) {
			ui.Text("Open a valid material draft to preview.");
			ImGui::End();
			return;
		}

		ui.Text(PathUtils::ToUtf8(path));
		const bool reload = reloadRequested_;
		reloadRequested_ = false;

		if (!world_ && (!resourceAttempted_ || reload)) {
			resourceAttempted_ = true;
			auto world = render_.CreateWorld();
			auto model = models_.Load(L"resources/editor/models/box.glb");
			auto target =
			    render_.CreateRenderTexture(kPreviewSize, kPreviewSize);

			if (!world || !model || !target) {
				throw std::runtime_error(
				    "Could not create material preview resources.");
			}

			const auto object = world->CreateObject(model);

			if (!world->Contains(object) ||
			    world->GetMaterialCount(object) == 0 ||
			    !world->SetCameraPose({{3, 2.5f, -4}, {}, {0, 1, 0}})) {
				throw std::runtime_error(
				    "Could not prepare the material preview box.");
			}

			world_ = std::move(world);
			target_ = std::move(target);
			object_ = object;
		}

		if (world_) {
			const auto directory =
			    PathUtils::ResolveFromExecutable(path).parent_path();
			auto image =
			    draft->baseColorTexture.empty()
			        ? std::filesystem::path{}
			        : (directory / draft->baseColorTexture).lexically_normal();

			if (!materialAttempted_ || image != attemptedImage_ || reload) {
				attemptedImage_.swap(image);
				materialAttempted_ = true;
				material_ = render_.GetMaterials().Create(*draft, directory);
				error_ =
				    material_
				        ? ""
				        : "Could not load the preview image. Reload to retry.";

				if (material_) {
					for (std::size_t slot = 0;
					     slot < world_->GetMaterialCount(object_); ++slot) {
						if (!world_->SetMaterial(object_, slot, material_)) {
							throw std::runtime_error(
							    "Could not apply the preview material.");
						}
					}
				}
			}

			if (material_) {
				for (std::size_t slot = 0;
				     slot < world_->GetMaterialCount(object_); ++slot) {
					world_->GetMaterial(object_, slot) = draft->data;
				}

				ready_ = render_.Update(*world_, kPreviewSize, kPreviewSize);

				if (!ready_) {
					throw std::runtime_error(
					    "Could not update the material preview.");
				}

				error_.clear();
				const auto available = ui.GetContentSize();
				const float size = (std::min)(available.x, available.y);

				if (size > 0) {
					ui.Image(*target_, size, size);
				}
			}
		}
	} catch (const std::exception &error) {
		error_ = error.what();
		ready_ = false;
		Logger::Error(std::format("MaterialPreviewPanel: {}", error.what()));
	}

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	ImGui::End();
}

void MaterialPreviewPanel::Draw() const {
	if (ready_ && world_ && target_) {
		const float background[4]{.12f, .12f, .12f, 1};
		render_.Draw(*world_, *target_, background);
	}
}

void MaterialPreviewPanel::Clear() {
	world_.reset();
	target_.reset();
	object_ = {};
	material_ = {};
	attemptedImage_.clear();
	error_.clear();
	open_ = focused_ = focusRequested_ = reloadRequested_ = false;
	resourceAttempted_ = materialAttempted_ = ready_ = false;
}
