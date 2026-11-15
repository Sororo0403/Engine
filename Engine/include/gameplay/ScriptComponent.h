#pragma once

#include "scene/SceneObjectId.h"
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

/// <summary>
/// Luaアセット・開始時の状態・名前付き物体参照を保持する。実行中の状態は編集データと分離する
/// </summary>
struct ScriptComponent {
	std::filesystem::path asset;
	bool enabled = true;
	// JSONオブジェクト。個々のインスタンスのstateへ複製し、再生中は変更しない
	std::string initialState = "{}";
	std::vector<std::pair<std::string, SceneObjectId>> references;
	// 生成に使うPrefab。未割り当てのスロットも編集用に保持する
	std::vector<std::pair<std::string, std::filesystem::path>> prefabs;
	// 遷移に使うシーン。Prefab・物体参照とは別の名前空間を使用する
	std::vector<std::pair<std::string, std::filesystem::path>> scenes;
	// 同じ物体のButtonを押すと、ゲーム内でコードを試せるようにする
	bool editInGame = false;
	static constexpr std::size_t kMaximumScenes = 32;
	static constexpr std::size_t kMaximumPrefabs = 32;
	static constexpr std::size_t kMaximumReferences = 32;
	static constexpr std::size_t kMaximumReferenceNameBytes = 64;

	/// <summary>
	/// パス・開始状態・参照名を検証する。未割り当てアセットと参照先0は編集用に許可する
	/// </summary>
	/// <returns>true:有効 false:設定が範囲外、参照名が空・重複、または状態の形式が無効</returns>
	bool IsValid() const;

	/// <summary>
	/// アセット・有効設定・開始状態・参照先が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const ScriptComponent &other) const = default;
};
