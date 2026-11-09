#pragma once

#include "scene/SceneObjectId.h"
#include <algorithm>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

/// <summary>
/// Prefab由来の物体の所属と元ID。根は基準シーンと対応表を保持し、削除した物体の対応も残す
/// </summary>
struct PrefabLinkComponent {
	SceneObjectId root = 0;
	SceneObjectId source = 0;
	// 次の3項目は根だけが保持する。子は空にし、rootから取得する。
	std::filesystem::path asset;
	std::string baseline;
	std::vector<std::pair<SceneObjectId, SceneObjectId>> objects;

	/// <summary>
	/// 根または子の保存用データとして、ID・参照先・対応表が有効か確認する
	/// </summary>
	/// <returns>true:有効 false:未設定、重複、または根の対応が不一致。基準シーンの内容と所有関係はシーン保存時に検証する</returns>
	bool IsValid() const {
		if (!root || !source) {
			return false;
		}

		if (asset.empty()) {
			return baseline.empty() && objects.empty();
		}

		if (asset.native().find(L'\0') != std::wstring::npos ||
		    baseline.empty() || !ContainsSource(source) ||
		    GetInstance(source) != root) {
			return false;
		}
		std::set<SceneObjectId> used, originals;

		for (const auto &[original, current] : objects) {
			if (!original || !originals.insert(original).second ||
			    original == (std::numeric_limits<SceneObjectId>::max)() ||
			    current == (std::numeric_limits<SceneObjectId>::max)() ||
			    (current && !used.insert(current).second)) {
				return false;
			}
		}

		return true;
	}

	/// <summary>
	/// 元IDが対応表に含まれるか確認する
	/// </summary>
	/// <param name="id">アセット内の元ID</param>
	/// <returns>true:対応あり false:未登録</returns>
	bool ContainsSource(SceneObjectId id) const {
		return std::ranges::any_of(
		    objects, [id](const auto &entry) { return entry.first == id; });
	}

	/// <summary>
	/// 元IDに対応する配置先IDを取得する
	/// </summary>
	/// <param name="id">アセット内の元ID</param>
	/// <returns>配置先ID。未登録・配置から削除済みの場合は0</returns>
	SceneObjectId GetInstance(SceneObjectId id) const {
		const auto found = std::ranges::find_if(
		    objects, [id](const auto &entry) { return entry.first == id; });
		return found == objects.end() ? 0 : found->second;
	}

	/// <summary>
	/// 所属・参照先・基準値が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const PrefabLinkComponent &other) const = default;
};
