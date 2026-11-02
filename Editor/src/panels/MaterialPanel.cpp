#include "panels/MaterialPanel.h"
#include "assets/AssetDatabase.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <format>
#include <imgui.h>
#include <stdexcept>

bool MaterialPanel::Select(const std::filesystem::path &path) {
	if (HasUnsavedChanges()) {
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

void MaterialPanel::Draw(const ImGuiSystem &ui, AssetDatabase &assets) {
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
				if (newName_.empty() || newName_ == "." || newName_ == ".." ||
				    newName_.find_first_of("<>:\"/\\|?*") !=
				        std::string::npos ||
				    newName_.back() == '.' || newName_.back() == ' ') {
					error_ = "Enter a valid file name without a folder path.";
				} else {
					const auto path =
					    assets.GetRootDirectory() / L"materials" /
					    PathUtils::FromUtf8(newName_ + ".material.json");
					std::filesystem::create_directories(
					    PathUtils::ResolveFromExecutable(path).parent_path());

					if (MaterialSerializer::Save(path, MaterialAsset{}, error_,
					                             false)) {
						assets.Refresh();
						Select(path);
					}
				}
			}

			if (ui.Button("Refresh Materials")) {
				assets.Refresh();
			}

			for (const auto &path : assets.GetMaterials()) {
				const auto label = PathUtils::ToUtf8(path);

				if (ui.Selectable(label.c_str(), path == path_)) {
					Select(path);
				}
			}
		}

		if (!states_.empty()) {
			ImGui::Separator();
			ui.Text(PathUtils::ToUtf8(path_) + (dirty ? " *" : ""));
			ui.ColorEdit4("Base Color", draft_.data.baseColor);
			ImGui::DragFloat2("UV Scale", &draft_.data.uvScale.x, 0.01f);
			ImGui::DragFloat2("UV Offset", &draft_.data.uvOffset.x, 0.01f);

			if (ui.InputText("Texture", texture_)) {
				textureValid_ = false;
				draft_.baseColorTexture = PathUtils::FromUtf8(texture_);
				textureValid_ = true;
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
					Select(path_);
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
