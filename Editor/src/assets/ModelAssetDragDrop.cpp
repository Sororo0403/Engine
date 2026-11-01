#include "assets/ModelAssetDragDrop.h"

#include "core/Logger.h"
#include "platform/windows/PathUtils.h"

#include <imgui.h>

#include <exception>
#include <format>
#include <string_view>

bool ModelAssetDragDrop::DrawSource(const std::filesystem::path &path) {
	if (path.empty() || !ImGui::BeginDragDropSource()) {
		return false;
	}

	const auto text = PathUtils::ToUtf8(path);
	// ImGuiがバイト列をコピーするため、一覧のインデックスやポインターは渡さない
	ImGui::SetDragDropPayload(kPayloadType, text.c_str(), text.size() + 1,
	                          ImGuiCond_Once);
	ImGui::TextUnformatted(PathUtils::ToUtf8(path.filename()).c_str());
	ImGui::TextUnformatted("Drop into the Scene to place the model.");
	ImGui::EndDragDropSource();
	return true;
}

std::filesystem::path ModelAssetDragDrop::AcceptTarget() {
	if (!ImGui::BeginDragDropTarget()) {
		return {};
	}

	const auto *payload = ImGui::AcceptDragDropPayload(kPayloadType);
	std::filesystem::path result;

	if (payload && payload->IsDelivery() && payload->Data &&
	    payload->DataSize > 1) {
		const auto *text = static_cast<const char *>(payload->Data);
		const auto size = static_cast<std::size_t>(payload->DataSize);

		if (text[size - 1] == '\0') {
			try {
				// 確定したデータはEndDragDropTargetより前にコピーする
				result = PathUtils::FromUtf8(std::string_view{text, size - 1});
			} catch (const std::exception &error) {
				Logger::Error(std::format(
				    "ModelAssetDragDrop::AcceptTarget: {}", error.what()));
			}
		}
	}

	ImGui::EndDragDropTarget();
	return result;
}
