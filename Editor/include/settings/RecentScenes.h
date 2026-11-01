#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

/// <summary>
/// 最近使用したシーンの絶対パスを、新しい順で管理する
/// </summary>
class RecentScenes {
  public:
	static constexpr std::size_t kMaximumCount = 8;

	/// <summary>
	/// パスを先頭へ登録する。同じパスの重複を除き、上限件数を保つ
	/// </summary>
	/// <param name="path">使用したシーンのパス。相対パスは実行ファイル基準</param>
	/// <returns>true:登録成功 false:パスが無効、またはコピー失敗</returns>
	bool Add(const std::filesystem::path &path);

	/// <summary>
	/// 最近使用した順の一覧を取得する
	/// </summary>
	/// <returns>次のAdd成功またはClearまで有効なパス一覧</returns>
	std::span<const std::filesystem::path> GetPaths() const;

	/// <summary>
	/// 一覧をクリアする。シーンファイルは削除しない
	/// </summary>
	void Clear();

  private:
	std::vector<std::filesystem::path> paths_;
};
