#include "panels/MaterialPanel.h"
#include "assets/AssetDatabase.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <algorithm>
#include <format>
#include <imgui.h>
#include <stdexcept>
#include <string_view>

bool MaterialPanel::Open(const std::filesystem::path &path,
                         bool discardChanges) {
	if (HasUnsavedChanges() && !discardChanges) {
		error_ =
		    "Save or revert the current material before selecting another.";
		return false;
	}

	MaterialAsset asset;

	if (!MaterialSerializer::Load(path, asset, error_)) {
		return false;
	}

	auto selected = path;
	auto texture = PathUtils::ToUtf8(asset.baseColorTexture);
	std::vector<MaterialAsset> history{asset};
	path_.swap(selected);
	texture_.swap(texture);
	draft_ = std::move(asset);
	textureValid_ = true;
	states_.swap(history);
	cursor_ = 0;
	return true;
}

bool MaterialPanel::SelectTexture(const std::filesystem::path &path) {
	try {
		if (states_.empty()) {
			throw std::runtime_error(
			    "Open a material before selecting a texture.");
		}

		std::filesystem::path relative;

		if (!path.empty()) {
			if (path.native().find(L'\0') != std::wstring::npos) {
				throw std::invalid_argument("Invalid texture path.");
			}

			const auto absolute = PathUtils::ResolveFromExecutable(path);

			if (!std::filesystem::is_regular_file(absolute)) {
				throw std::runtime_error("The texture file does not exist.");
			}

			relative = absolute.lexically_relative(
			    PathUtils::ResolveFromExecutable(path_).parent_path());

			if (relative.empty()) {
				throw std::runtime_error(
				    "Choose a texture on the material's drive.");
			}
		}

		MaterialAsset candidate;
		candidate.baseColorTexture = relative;

		if (!candidate.IsValid()) {
			throw std::invalid_argument("Invalid relative texture path.");
		}

		auto text = PathUtils::ToUtf8(relative);
		// パスと表示文字列の準備が全て成功してから、編集中の設定を置き換える。
		draft_.baseColorTexture.swap(relative);
		texture_.swap(text);
		textureValid_ = true;
		error_.clear();
		return true;
	} catch (const std::exception &error) {
		error_ = error.what();
		return false;
	}
}

bool MaterialPanel::Save() {
	if (states_.empty() || !textureValid_ || !draft_.IsValid()) {
		error_ = "Invalid material values or texture path.";
		return false;
	}

	MaterialAsset disk;

	if (!MaterialSerializer::Load(path_, disk, error_)) {
		return false;
	}

	if (disk != states_[cursor_]) {
		error_ =
		    "The material changed outside this editor. Reload before saving.";
		return false;
	}

	if (disk == draft_) {
		return true;
	}

	if (!draft_.baseColorTexture.empty() &&
	    !std::filesystem::is_regular_file(
	        PathUtils::ResolveFromExecutable(path_).parent_path() /
	        draft_.baseColorTexture)) {
		error_ = "The texture file does not exist.";
		return false;
	}

	// ファイルの保存後に履歴のメモリ確保が失敗しないよう、先に準備する。
	auto next = draft_;
	states_.reserve(cursor_ + 2);

	if (!MaterialSerializer::Save(path_, next, error_)) {
		return false;
	}

	states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1),
	              states_.end());
	states_.push_back(std::move(next));
	++cursor_;
	return true;
}

bool MaterialPanel::Restore(std::size_t index) {
	if (index >= states_.size() || HasUnsavedChanges()) {
		return false;
	}

	MaterialAsset disk;

	if (!MaterialSerializer::Load(path_, disk, error_) ||
	    disk != states_[cursor_]) {
		error_ = "The material changed outside this editor. Reload before "
		         "restoring history.";
		return false;
	}

	auto restored = states_[index];
	auto texture = PathUtils::ToUtf8(restored.baseColorTexture);

	if (!MaterialSerializer::Save(path_, restored, error_)) {
		return false;
	}

	draft_ = std::move(restored);
	texture_.swap(texture);
	cursor_ = index;
	return true;
}

namespace {
/// <summary>
/// フォルダ指定・制御文字・予約されたデバイス名を含まない材質名か確認する
/// </summary>
/// <param name="name">拡張子を付ける前の新しい材質名</param>
/// <returns>true:使用できる名前 false:空、無効な文字、または予約名</returns>
bool IsValidMaterialName(std::string_view name) {
	if (name.empty() || name == "." || name == ".." || name.back() == '.' ||
	    name.back() == ' ' ||
	    name.find_first_of("<>:\"/\\|?*") != std::string_view::npos ||
	    std::any_of(name.begin(), name.end(),
	                [](unsigned char c) { return c < 32; })) {
		return false;
	}

	std::string stem(name.substr(0, name.find('.')));

	while (!stem.empty() && stem.back() == ' ') {
		stem.pop_back();
	}

	for (auto &c : stem) {
		if (c >= 'a' && c <= 'z') {
			c = static_cast<char>(c - 'a' + 'A');
		}
	}

	if (stem.starts_with("COM") || stem.starts_with("LPT")) {
		const auto suffix = std::string_view(stem).substr(3);

		if ((suffix.size() == 1 && suffix.front() >= '1' &&
		     suffix.front() <= '9') ||
		    suffix == "¹" || suffix == "²" || suffix == "³") {
			return false;
		}
	}

	return stem != "CON" && stem != "PRN" && stem != "AUX" && stem != "NUL";
}

/// <summary>
/// 材質の編集UIで、例外発生時も無効化状態を元へ戻す
/// </summary>
class DisabledScope {
  public:
	DisabledScope(const ImGuiSystem &ui, bool disabled) : ui_(ui) {
		ui_.BeginDisabled(disabled);
	}

	~DisabledScope() {
		ui_.EndDisabled();
	}

  private:
	const ImGuiSystem &ui_;
};
} // namespace

bool MaterialPanel::CreateMaterial(AssetDatabase &assets, bool duplicate) {
	try {
		if (HasUnsavedChanges()) {
			throw std::runtime_error(
			    "Save or revert the current material before creating another.");
		}

		if (!IsValidMaterialName(newName_)) {
			throw std::invalid_argument("Enter a valid file name without a "
			                            "folder path or reserved device name.");
		}

		if (assets.GetRootDirectory().empty() ||
		    assets.GetRootDirectory().has_root_path()) {
			throw std::invalid_argument(
			    "Use an executable-relative asset root.");
		}

		MaterialAsset next;

		if (duplicate) {
			if (states_.empty()) {
				throw std::runtime_error(
				    "Open a material before duplicating it.");
			}

			if (!MaterialSerializer::Load(path_, next, error_)) {
				return false;
			}

			if (next != states_[cursor_]) {
				throw std::runtime_error("The material changed outside this "
				                         "editor. Reload before duplicating.");
			}
		}

		auto path = assets.GetRootDirectory() / L"materials" /
		            PathUtils::FromUtf8(newName_ + ".material.json");
		const auto destination = PathUtils::ResolveFromExecutable(path);

		if (duplicate && !next.baseColorTexture.empty()) {
			const auto texture =
			    (PathUtils::ResolveFromExecutable(path_).parent_path() /
			     next.baseColorTexture)
			        .lexically_normal();

			if (!std::filesystem::is_regular_file(texture)) {
				throw std::runtime_error(
				    "The source material's texture file does not exist.");
			}

			next.baseColorTexture =
			    texture.lexically_relative(destination.parent_path());

			if (next.baseColorTexture.empty() || !next.IsValid()) {
				throw std::runtime_error(
				    "Could not make a relative texture path for the copy.");
			}
		}

		// 保存後の文書切り替えに必要なデータを、ファイルの作成前に全て用意する。
		auto texture = PathUtils::ToUtf8(next.baseColorTexture);
		std::vector<MaterialAsset> history{next};
		std::filesystem::create_directories(destination.parent_path());

		if (!MaterialSerializer::Save(path, next, error_, false)) {
			return false;
		}

		path_.swap(path);
		texture_.swap(texture);
		draft_ = std::move(next);
		states_.swap(history);
		cursor_ = 0;
		textureValid_ = true;
		error_.clear();
		RequestFocus();

		if (!assets.Refresh()) {
			error_ = assets.GetError();
		}

		return true;
	} catch (const std::exception &error) {
		error_ = error.what();
		return false;
	}
}

void MaterialPanel::Draw(const ImGuiSystem &ui, AssetDatabase &assets,
                         bool canAssignMaterial) {
	assignmentRequest_.reset();
	usageSelectionPath_.clear();
	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	if (!ui.BeginWindow("Materials")) {
		ui.EndWindow();
		return;
	}

	try {
		const bool dirty = HasUnsavedChanges();
		{
			const DisabledScope disabled(ui, dirty);
			ui.InputText("New Material Name", newName_);

			if (ui.Button("Create Material")) {
				CreateMaterial(assets, false);
			}

			{
				const DisabledScope noMaterial(ui, states_.empty());

				if (ui.Button("Duplicate Material")) {
					CreateMaterial(assets, true);
				}
			}

			if (ui.Button("Refresh Materials")) {
				assets.Refresh();
			}

			for (const auto &path : assets.GetMaterials()) {
				const auto label = PathUtils::ToUtf8(path);

				if (ui.Selectable(label.c_str(), path == path_)) {
					Open(path);
				}
			}
		}

		if (!states_.empty()) {
			ImGui::Separator();
			ui.Text(PathUtils::ToUtf8(path_) + (dirty ? " *" : ""));
			if (ui.Button("Select Objects Using Material")) {
				usageSelectionPath_ = path_;
			}

			ImGui::InputInt("Material Slot", &assignmentSlot_);
			{
				const DisabledScope noSelection(ui, !canAssignMaterial ||
				                                        assignmentSlot_ < 0);
				{
					const DisabledScope unsaved(ui, dirty);

					if (ui.Button("Assign Slot to Selection")) {
						RequestAssignment(false);
					}

					if (ui.Button("Duplicate and Assign Slot to Selection")) {
						if (CreateMaterial(assets, true)) {
							RequestAssignment(false);
						}
					}
				}

				if (ui.Button("Reset Slot to Model Default")) {
					RequestAssignment(true);
				}
			}

			const char *shadingLabel =
			    draft_.data.shadingMode == MaterialData::ShadingMode::UNLIT
			        ? "Unlit"
			        : "Lit";

			if (ImGui::BeginCombo("Shading", shadingLabel)) {
				if (ui.Selectable("Lit", draft_.data.shadingMode ==
				                             MaterialData::ShadingMode::LIT)) {
					draft_.data.shadingMode = MaterialData::ShadingMode::LIT;
				}

				if (ui.Selectable("Unlit",
				                  draft_.data.shadingMode ==
				                      MaterialData::ShadingMode::UNLIT)) {
					draft_.data.shadingMode = MaterialData::ShadingMode::UNLIT;
				}

				ImGui::EndCombo();
			}

			ui.ColorEdit4("Base Color", draft_.data.baseColor);
			ImGui::DragFloat2("UV Scale", &draft_.data.uvScale.x, 0.01f);
			ImGui::DragFloat2("UV Offset", &draft_.data.uvOffset.x, 0.01f);

			if (ui.InputText("Texture", texture_)) {
				textureValid_ = false;
				draft_.baseColorTexture = PathUtils::FromUtf8(texture_);
				textureValid_ = true;
			}

			if (ui.Button("Refresh Textures")) {
				assets.Refresh();
			}

			if (ImGui::BeginCombo("Texture Asset", texture_.empty()
			                                           ? "None"
			                                           : texture_.c_str())) {
				if (ui.Selectable("None",
				                  textureValid_ &&
				                      draft_.baseColorTexture.empty())) {
					SelectTexture({});
				}

				for (const auto &path : assets.GetTextures()) {
					const auto label = PathUtils::ToUtf8(path);
					const bool selected =
					    textureValid_ && draft_.IsValid() &&
					    !draft_.baseColorTexture.empty() &&
					    PathUtils::IsSameFile(
					        PathUtils::ResolveFromExecutable(path_)
					                .parent_path() /
					            draft_.baseColorTexture,
					        path);

					if (ui.Selectable(label.c_str(), selected)) {
						SelectTexture(path);
					}
				}

				ImGui::EndCombo();
			}

			{
				const DisabledScope disabled(ui, !textureValid_ ||
				                                     !draft_.IsValid());

				if (ui.Button("Save Material")) {
					Save();
				}
			}
			ui.SameLine();

			if (ui.Button("Revert Draft")) {
				auto restored = states_[cursor_];
				auto texture = PathUtils::ToUtf8(restored.baseColorTexture);
				draft_ = std::move(restored);
				textureValid_ = true;
				texture_.swap(texture);
				error_.clear();
			}

			const bool edited = HasUnsavedChanges();
			{
				const DisabledScope disabled(ui, edited || cursor_ == 0);

				if (ui.Button("Undo Material")) {
					Restore(cursor_ - 1);
				}
			}
			ui.SameLine();
			{
				const DisabledScope disabled(ui, edited || cursor_ + 1 >=
				                                               states_.size());

				if (ui.Button("Redo Material")) {
					Restore(cursor_ + 1);
				}
			}
			{
				const DisabledScope disabled(ui, edited);

				if (ui.Button("Reload Material")) {
					Open(path_);
				}
			}
		}

		if (!error_.empty()) {
			ui.Text(error_);
		}
	} catch (const std::exception &e) {
		error_ = e.what();
	}

	ui.EndWindow();
}

bool MaterialPanel::HasUnsavedChanges() const {
	return !states_.empty() && (!textureValid_ || draft_ != states_[cursor_]);
}

const std::filesystem::path &MaterialPanel::GetFilePath() const {
	return path_;
}

const std::string &MaterialPanel::GetError() const {
	return error_;
}

void MaterialPanel::RequestFocus() {
	focusRequested_ = true;
}

const std::filesystem::path &MaterialPanel::GetUsageSelectionRequest() const {
	return usageSelectionPath_;
}

bool MaterialPanel::RequestAssignment(bool reset) {
	assignmentRequest_.reset();

	try {
		if (states_.empty() || assignmentSlot_ < 0) {
			throw std::runtime_error(
			    "Open a material and choose a non-negative slot.");
		}

		if (!reset) {
			if (HasUnsavedChanges()) {
				throw std::runtime_error(
				    "Save or revert the material before assigning it.");
			}

			MaterialAsset disk;

			if (!MaterialSerializer::Load(path_, disk, error_)) {
				return false;
			}

			if (disk != states_[cursor_]) {
				throw std::runtime_error("The material changed outside this "
				                         "editor. Reload before assigning.");
			}
		}

		assignmentRequest_ =
		    AssignmentRequest{static_cast<std::uint32_t>(assignmentSlot_),
		                      reset ? std::filesystem::path{} : path_};
		error_.clear();
		return true;
	} catch (const std::exception &error) {
		error_ = error.what();
		return false;
	}
}

const std::optional<MaterialPanel::AssignmentRequest> &
MaterialPanel::GetAssignmentRequest() const {
	return assignmentRequest_;
}
