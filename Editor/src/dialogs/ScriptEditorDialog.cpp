#include "dialogs/ScriptEditorDialog.h"
#include "assets/AssetFolders.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "scripting/LuaScript.h"
#include <algorithm>
#include <exception>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <stdexcept>

namespace {
constexpr const char *kDefaultScriptName = "New Script.lua";

/// <summary>
/// 元のLuaと同じフォルダに、既存ファイルと重ならない複製名を選ぶ
/// </summary>
std::filesystem::path FindScriptCopy(const std::filesystem::path &source) {
	const auto stem = source.stem().native();
	for (std::size_t index = 1;; ++index) {
		const auto suffix =
		    index == 1 ? std::wstring{} : L"-" + std::to_wstring(index);
		const auto name = stem + L".copy" + suffix + L".lua";
		if (!PathUtils::IsValidFileName(name)) {
			throw std::runtime_error("The script copy name is too long or invalid.");
		}
		const auto candidate = source.parent_path() / name;
		const auto absolute = PathUtils::ResolveFromExecutable(candidate);
		if (!std::filesystem::exists(std::filesystem::symlink_status(absolute))) {
			return candidate;
		}
	}
}

/// <summary>
/// Luaの行番号を入力欄のUTF-8バイト位置に変換し、キャレットを移す
/// </summary>
int JumpToLine(ImGuiInputTextCallbackData *data) {
	auto &line = *static_cast<int *>(data->UserData);
	const std::string_view code(data->Buf, data->BufTextLen);
	std::size_t start = 0;
	for (; line > 1; --line) {
		const auto newline = code.find_first_of("\r\n", start);
		if (newline == std::string_view::npos)
			break;
		start = newline + 1;
		if (start < code.size() && (code[start] == '\r' || code[start] == '\n') &&
		    code[start] != code[newline])
			++start;
	}
	data->CursorPos = data->SelectionStart =
	    data->SelectionEnd = static_cast<int>(start);
	line = 0;
	return 0;
}
} // namespace

bool ScriptEditorDialog::ShowCreate(const std::filesystem::path &assets,
                                    const std::filesystem::path &folder,
                                    std::string &error) {
	try {
		AssetFolders::Resolve(assets, folder);
		assets_ = assets;
		folder_ = folder;
		path_.clear();
		saved_.clear();
		name_ = kDefaultScriptName;
		source_ = "return {\n    start = function(self, state)\n        -- "
		          "Initialize per-object values in state.\n    end,\n\n    "
		          "update = function(self, state, seconds, input)\n        -- "
		          "Example: local x, y, z = self:get_position()\n    end,\n}\n";
		original_ = source_;
		error_.clear();
		creating_ = true;
		saveConflict_ = false;
		confirmation_ = Confirmation::NONE;
		pendingLine_ = 0;
		++inputRevision_;
		opening_ = true;
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool ScriptEditorDialog::ShowEdit(const std::filesystem::path &path,
                                  std::string &error,
                                  std::string_view diagnostic) {
	std::string source;
	if (!LuaScript::Read(path, source, error))
		return false;
	path_ = path;
	source_ = source;
	original_ = std::move(source);
	saved_.clear();
	error_.clear();
	creating_ = false;
	saveConflict_ = false;
	confirmation_ = Confirmation::NONE;
	pendingLine_ = 0;
	if (!diagnostic.empty()) {
		auto absolute = PathUtils::ResolveFromExecutable(path);
		absolute.make_preferred();
		pendingLine_ = LuaScript::FindErrorLine(PathUtils::ToUtf8(absolute),
		                                       diagnostic);
	}
	++inputRevision_;
	opening_ = true;
	error.clear();
	return true;
}

void ScriptEditorDialog::ReloadFromDisk() {
	std::string current;
	if (!LuaScript::Read(path_, current, error_))
		return;

	source_ = current;
	original_ = std::move(current);
	saveConflict_ = false;
	pendingLine_ = 0;
	++inputRevision_;
}

ScriptEditorDialog::Result ScriptEditorDialog::Draw(const ImGuiSystem &ui,
                                                    bool canPlayAfterSave) {
	constexpr const char *title = "Lua Script###ScriptEditorDialog";

	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}

	ImGui::SetNextWindowSize({800, 600}, ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(title, nullptr,
	                            ImGuiWindowFlags_NoSavedSettings))
		return Result::NONE;

	const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
	if (escape) {
		// テキスト欄がEscで編集中の値を戻す前に、ダイアログが処理する。
		ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("Script Editor Escape"),
		                   ImGuiInputFlags_LockThisFrame);
	}

	Result result = Result::NONE;
	const bool confirming = confirmation_ != Confirmation::NONE;
	bool reload = false;
	if (confirming) {
		const bool confirmingReload = confirmation_ == Confirmation::RELOAD;
		ui.Text(confirmingReload
		            ? "Discard unsaved changes and reload the script from disk?"
		            : "Discard unsaved script changes?");
		if (ui.Button("Keep Editing") || escape) {
			confirmation_ = Confirmation::NONE;
		}
		ui.SameLine();
		if (ui.Button(confirmingReload ? "Reload Script" : "Discard Changes") &&
		    confirmation_ != Confirmation::NONE) {
			confirmation_ = Confirmation::NONE;
			if (confirmingReload)
				reload = true;
			else
				result = Result::CANCELLED;
		}
	}

	ui.BeginDisabled(confirming);
	// 開き直しや再読み込みの前の入力状態を引き継がない。
	ImGui::PushID(static_cast<int>(inputRevision_));
	if (creating_) {
		ui.Text(PathUtils::ToUtf8(assets_ / folder_));
		ui.InputText("Script Name", name_);
	} else
		ui.Text(PathUtils::ToUtf8(path_));
	const float height =
	    (std::max)(160.0f, ImGui::GetContentRegionAvail().y - 105.0f);
	ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
	if (pendingLine_ > 0 && !confirming) {
		ImGui::SetKeyboardFocusHere();
		flags |= ImGuiInputTextFlags_CallbackAlways;
	}
	const auto codeId = ImGui::GetID("Code");
	ImGui::InputTextMultiline("Code", &source_, {-1, height}, flags, JumpToLine,
	                          &pendingLine_);
	if ((flags & ImGuiInputTextFlags_CallbackAlways) && pendingLine_ == 0) {
		if (auto *state = ImGui::GetInputTextState(codeId))
			state->CursorFollow = true;
	}
	ImGui::PopID();
	if (ui.Button("Check Syntax")) {
		if (LuaScript::Validate(source_, error_))
			error_ = "Syntax OK";
		else
			pendingLine_ = LuaScript::FindErrorLine("Script", error_);
	}

	ui.SameLine();
	const bool save = ui.Button("Save Script");
	bool saveAndPlay = false;
	if (canPlayAfterSave && !saveConflict_) {
		ui.SameLine();
		saveAndPlay = ui.Button("Save and Play");
	}
	bool saveCopy = false;
	if (saveConflict_) {
		ui.SameLine();
		saveCopy = ui.Button("Save Copy");
		ui.SameLine();
		if (ui.Button("Reload from Disk")) {
			if (source_ != original_)
				confirmation_ = Confirmation::RELOAD;
			else
				reload = true;
		}
	}

	if (save || saveCopy || saveAndPlay) {
		try {
			if (!LuaScript::Validate(source_, error_))
				throw std::runtime_error(error_);
			auto destination = path_;
			auto reference = path_;

			if (saveCopy) {
				destination = reference = FindScriptCopy(path_);
			} else if (creating_) {
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
				if (!LuaScript::Read(path_, current, error_)) {
					saveConflict_ = true;
					throw std::runtime_error(error_);
				}
				if (current != original_) {
					saveConflict_ = true;
					throw std::runtime_error("The script changed on disk. "
					                         "Save a copy or reload it to continue.");
				}
			}

			if (!FileUtils::WriteTextAtomically(destination, source_, error_,
			                                    !creating_ && !saveCopy))
				throw std::runtime_error(error_);
			saved_.swap(reference);
			result = saveAndPlay ? Result::SAVED_AND_PLAY : Result::SAVED;
		} catch (const std::exception &exception) {
			error_ = exception.what();
		}
	}

	ui.SameLine();
	const bool cancel = ui.Button("Cancel");
	ui.EndDisabled();
	if (!confirming && result == Result::NONE && (cancel || escape)) {
		if (source_ != original_ || (creating_ && name_ != kDefaultScriptName)) {
			confirmation_ = Confirmation::DISCARD;
		} else {
			result = Result::CANCELLED;
		}
	}
	if (reload)
		ReloadFromDisk();
	if (!error_.empty())
		ImGui::TextWrapped("%s", error_.c_str());
	if (result != Result::NONE)
		ui.CloseCurrentPopup();
	ui.EndPopup();
	return result;
}
