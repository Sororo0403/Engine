#pragma once
#include <filesystem>
#include <memory>
#include <span>
#include <string>
class Scene;

/// <summary>
/// 割り当てたPrefabと入れ子の参照を読み込み、再生用の不変な階層を共有する
/// </summary>
class ScenePrefabs {
  public:
	static constexpr std::size_t kMaximumAssets = 256;
	static constexpr std::size_t kMaximumTemplateObjects = 4096;
	static constexpr std::size_t kMaximumFileBytes = 32 * 1024 * 1024;
	static constexpr std::size_t kMaximumTotalBytes = 64 * 1024 * 1024;

	/// <summary>
	/// 名前付き参照から到達するPrefabを準備する。循環と同じパスの大小文字違いを重複処理しない
	/// </summary>
	/// <param name="scene">参照元。変更しない</param>
	/// <param name="preserve">true:読み込み済みデータを保持 false:ファイルを読み直す</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:準備成功 false:不正なファイル・階層・件数・容量。以前のライブラリを保持する</returns>
	bool Configure(const Scene &scene, bool preserve, std::string &error);

	/// <summary>
	/// 準備済みの階層だけを取得する。ファイルにはアクセスしない
	/// </summary>
	/// <param name="path">割り当てた絶対パスまたは実行ファイル基準の参照</param>
	/// <returns>不変な階層。不在や無効な参照は空</returns>
	std::shared_ptr<const Scene> Find(const std::filesystem::path &path) const;

	/// <summary>
	/// 読み込んだ全Prefabを、素材準備用の一覧として参照する
	/// </summary>
	/// <returns>次のConfigure成功または破棄まで有効な一覧</returns>
	std::span<const std::shared_ptr<const Scene>> GetScenes() const;

  private:
	struct Data;
	std::shared_ptr<const Data> data_;
};
