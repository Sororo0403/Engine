#include "assets/ModelUsage.h"

#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "selection/EditorSelection.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

std::vector<SceneObjectId> ModelUsage::FindObjects(
    const Scene &scene, const std::filesystem::path &modelPath) {
	if (modelPath.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument("ModelUsage: 検索先のパスが無効です。");
	}
	const auto target = PathUtils::ResolveFromExecutable(modelPath);
	std::vector<SceneObjectId> result;
	std::unordered_map<std::filesystem::path, bool> matches;
	for (const auto &[id, object] : scene.GetObjects()) {
		if (object.modelPath.empty() || object.modelPath.native().find(L'\0') != std::wstring::npos) {
			continue;
		}
		// 同じ参照先を使う物体では、パスの正規化とファイルの照合結果を再利用する
		auto [found, inserted] = matches.try_emplace(object.modelPath, false);
		if (inserted) {
			try {
				found->second = PathUtils::IsSameFile(target, object.modelPath);
			} catch (const std::invalid_argument &) {
				// ほかの物体に不正な参照先があっても、有効な参照の検索を続ける
				found->second = false;
			}
		}
		if (found->second) {
			result.push_back(id);
		}
	}
	return result;
}

std::size_t ModelUsage::SelectObjects(const Scene &scene,
                                     const std::filesystem::path &modelPath,
                                     EditorSelection &selection) {
	auto ids = FindObjects(scene, modelPath);
	const auto active = std::find(ids.begin(), ids.end(), selection.GetActiveId());
	if (active != ids.end()) {
		std::rotate(active, active + 1, ids.end());
	}
	const auto count = ids.size();
	if (!selection.Set(std::move(ids))) {
		throw std::logic_error("ModelUsage: 選択する物体の一覧が無効です。");
	}
	return count;
}
