#include "dialogs/ScriptEditorDialog.h"
#include "assets/AssetFolders.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <exception>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <stdexcept>

bool ScriptEditorDialog::ShowCreate(const std::filesystem::path &assets,
                                    const std::filesystem::path &folder,
                                    std::string &error) {
	try {
		AssetFolders::Resolve(assets, folder);
		assets_ = assets;
		folder_ = folder;
		path_.clear();
		saved_.clear();
		name_ = "New Script.lua";
		source_ = "return {\n    start = function(self, state)\n        -- "
		          "Initialize per-object values in state.\n    end,\n\n    "
		          "update = function(self, state, seconds, input)\n        -- "
		          "Example: local x, y, z = self:get_position()\n    end,\n}\n";
		original_.clear();
		error_.clear();
		creating_ = true;
		opening_ = true;
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool ScriptEditorDialog::ShowEdit(const std::filesystem::path &path,
                                  std::string &error) {
	std::string source;
	if (!LuaScript::Read(path, source, error))
		return false;
	path_ = path;
	source_ = source;
	original_ = std::move(source);
	saved_.clear();
	error_.clear();
	creating_ = false;
	opening_ = true;
	error.clear();
	return true;
}

ScriptEditorDialog::Result ScriptEditorDialog::Draw(const ImGuiSystem &ui) {
	constexpr const char *title = "Lua Script###ScriptEditorDialog";
	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}
	ImGui::SetNextWindowSize({800, 600}, ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(title, nullptr,
	                            ImGuiWindowFlags_NoSavedSettings))
		return Result::NONE;
	if (creating_) {
		ui.Text(PathUtils::ToUtf8(assets_ / folder_));
		ui.InputText("Script Name", name_);
	} else
		ui.Text(PathUtils::ToUtf8(path_));
	const float height =
	    (std::max)(160.0f, ImGui::GetContentRegionAvail().y - 105.0f);
	ImGui::InputTextMultiline("Code", &source_, {-1, height},
	                          ImGuiInputTextFlags_AllowTabInput);
	if (ui.Button("Check Syntax")) {
		if (LuaScript::Validate(source_, error_))
			error_ = "Syntax OK";
	}
	ui.SameLine();
	Result result = Result::NONE;
	if (ui.Button("Save Script")) {
		try {
			if (!LuaScript::Validate(source_, error_))
				throw std::runtime_error(error_);
			auto destination = path_;
			auto reference = path_;
			if (creating_) {
				auto name = PathUtils::FromUtf8(name_);
				if (!PathUtils::HasExtension(name, ".lua"))
					name += L".lua";
				if (!PathUtils::IsValidFileName(name))
					throw std::runtime_error(
					    "Enter one valid Script file name.");
				destination = AssetFolders::Resolve(assets_, folder_) / name;
				reference = AssetFolders::MakeReference(assets_, destination);
			} else {
				std::string current;
				if (!LuaScript::Read(path_, current, error_))
					throw std::runtime_error(error_);
				if (current != original_)
					throw std::runtime_error(
					    "The Script changed outside this dialog. Cancel and "
					    "reopen it before saving.");
			}
			if (!FileUtils::WriteTextAtomically(destination, source_, error_,
			                                    !creating_))
				throw std::runtime_error(error_);
			saved_.swap(reference);
			result = Result::SAVED;
		} catch (const std::exception &exception) {
			error_ = exception.what();
		}
	}
	ui.SameLine();
	if (ui.Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		result = Result::CANCELLED;
	if (!error_.empty())
		ImGui::TextWrapped("%s", error_.c_str());
	if (result != Result::NONE)
		ui.CloseCurrentPopup();
	ui.EndPopup();
	return result;
}
