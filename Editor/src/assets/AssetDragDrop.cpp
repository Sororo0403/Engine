#include "assets/AssetDragDrop.h"
#include "assets/PrefabAsset.h"
#include "core/Logger.h"
#include "platform/windows/PathUtils.h"

#include <exception>
#include <format>
#include <imgui.h>
#include <string_view>

namespace {
/// <summary>
/// アセット種別に対応する受け渡し用の識別名を取得する
/// </summary>
const char *PayloadType(AssetDragDrop::Kind kind) {
	switch (kind) {
	case AssetDragDrop::Kind::TEXTURE:
		return "EDITOR_TEXTURE_PATH";
	case AssetDragDrop::Kind::SCRIPT:
		return "EDITOR_SCRIPT_PATH";
	case AssetDragDrop::Kind::MODEL:
		return "EDITOR_MODEL_PATH";
	case AssetDragDrop::Kind::SCENE:
		return "EDITOR_SCENE_PATH";
	case AssetDragDrop::Kind::AUDIO:
		return "EDITOR_AUDIO_PATH";
	case AssetDragDrop::Kind::PREFAB:
		return "EDITOR_PREFAB_PATH";
	}
	return nullptr;
}

/// <summary>
/// 指定種別の通常のアセットパスか確認する。ファイル内容は受け取り側で検証する
/// </summary>
bool Matches(const std::filesystem::path &path, AssetDragDrop::Kind kind) {
	if (path.empty() || path.native().find(L'\0') != std::wstring::npos ||
	    (path.has_root_path() && !path.is_absolute())) {
		return false;
	}
	switch (kind) {
	case AssetDragDrop::Kind::TEXTURE:
		return PathUtils::IsImageFile(path);
	case AssetDragDrop::Kind::SCRIPT:
		return PathUtils::HasExtension(path, ".lua");
	case AssetDragDrop::Kind::MODEL:
		return PathUtils::IsModelFile(path);
	case AssetDragDrop::Kind::AUDIO:
		return PathUtils::HasExtension(path, ".wav");
	case AssetDragDrop::Kind::PREFAB:
		return PrefabAsset::IsPrefabPath(path);
	case AssetDragDrop::Kind::SCENE:
		return PathUtils::HasExtension(path, ".json") &&
		       PathUtils::HasExtension(path.stem(), ".scene");
	}
	return false;
}
} // namespace

bool AssetDragDrop::DrawSource(const std::filesystem::path &path, Kind kind) {
	const auto *type = PayloadType(kind);
	if (!type || !Matches(path, kind) || !ImGui::BeginDragDropSource()) {
		return false;
	}
	const auto text = PathUtils::ToUtf8(path);
	// ImGuiがバイト列をコピーするため、一覧のインデックスやポインターは渡さない。
	ImGui::SetDragDropPayload(type, text.c_str(), text.size() + 1,
	                          ImGuiCond_Once);
	ImGui::TextUnformatted(PathUtils::ToUtf8(path.filename()).c_str());
	ImGui::EndDragDropSource();
	return true;
}

std::filesystem::path AssetDragDrop::AcceptTarget(Kind kind) {
	const auto *type = PayloadType(kind);
	if (!type || !ImGui::BeginDragDropTarget()) {
		return {};
	}
	const auto *payload = ImGui::AcceptDragDropPayload(type);
	std::filesystem::path result;
	if (payload && payload->IsDelivery() && payload->Data &&
	    payload->DataSize > 1) {
		const auto *text = static_cast<const char *>(payload->Data);
		const auto size = static_cast<std::size_t>(payload->DataSize);
		if (text[size - 1] == '\0') {
			try {
				auto candidate =
				    PathUtils::FromUtf8(std::string_view{text, size - 1});
				if (Matches(candidate, kind)) {
					result.swap(candidate);
				}
			} catch (const std::exception &error) {
				Logger::Error(std::format("AssetDragDrop::AcceptTarget: {}",
				                          error.what()));
			}
		}
	}
	ImGui::EndDragDropTarget();
	return result;
}
