#include "assets/MaterialUsage.h"

#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "selection/EditorSelection.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

std::vector<SceneObjectId>
MaterialUsage::FindObjects(const Scene &scene,
                           const std::filesystem::path &materialPath) {
	if (materialPath.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument("MaterialUsage: 検索先のパスが無効です。");
	}

	const auto target = PathUtils::ResolveFromExecutable(materialPath);
	std::vector<SceneObjectId> result;
	std::unordered_map<std::filesystem::path, bool> matches;

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.meshRenderer) {
			continue;
		}

		for (const auto &[slot, path] : object.meshRenderer->materials.slots) {
			if (path.empty() ||
			    path.native().find(L'\0') != std::wstring::npos) {
				continue;
			}

			// 同じ材質を割り当てたスロットでは、ファイルの照合結果を再利用する。
			auto [found, inserted] = matches.try_emplace(path, false);

			if (inserted) {
				try {
					found->second = PathUtils::IsSameFile(target, path);
				} catch (const std::invalid_argument &) {
					// 不正な参照先が混在していても、ほかの有効な割り当てを検索する。
					found->second = false;
				}
			}

			if (found->second) {
				result.push_back(id);
				break;
			}
		}
	}

	return result;
}

std::size_t
MaterialUsage::SelectObjects(const Scene &scene,
                             const std::filesystem::path &materialPath,
                             EditorSelection &selection) {
	auto ids = FindObjects(scene, materialPath);
	const auto active =
	    std::find(ids.begin(), ids.end(), selection.GetActiveId());

	if (active != ids.end()) {
		std::rotate(active, active + 1, ids.end());
	}

	const auto count = ids.size();

	if (!selection.Set(std::move(ids))) {
		throw std::logic_error("MaterialUsage: 選択する物体の一覧が無効です。");
	}

	return count;
}
