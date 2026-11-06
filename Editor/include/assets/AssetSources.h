#pragma once

#include <filesystem>
#include <vector>

class Scene;

/// <summary>
/// 現在のAssetsフォルダと、シーンが引き続き参照できる外部素材の取得元を保持する
/// </summary>
class AssetSources {
  public:
	/// <summary>
	/// 検索先を変更し、絶対指定の場合は外部の取得元にも登録する。ファイル操作は行わない
	/// </summary>
	/// <param name="directory">絶対パス、または従来のresources配下の相対パス</param>
	/// <remarks>無効な指定は例外。変更前の値は保持する。フォルダの存在は呼び出し側で確認する</remarks>
	void Select(std::filesystem::path directory);

	/// <summary>
	/// 現在の検索先を変えず、外部素材の取得元を重複なく登録する。存在は確認しない
	/// </summary>
	/// <param name="directory">取得元の絶対パス。無効な指定は例外</param>
	void Register(std::filesystem::path directory);

	/// <summary>
	/// 現在の検索先を取得する
	/// </summary>
	/// <returns>既定の実行ファイル基準の相対パス、または選択した絶対パス</returns>
	const std::filesystem::path &GetDirectory() const;

	/// <summary>
	/// これまで登録した外部素材の取得元を取得する
	/// </summary>
	/// <returns>存在しない場所も保持した絶対パスの一覧</returns>
	const std::vector<std::filesystem::path> &GetDirectories() const;

	/// <summary>
	/// 直接参照する取得元と、シーン遷移先の素材を検索できる接続済みの取得元を列挙する
	/// </summary>
	/// <param name="scene">非表示・Meshのみの物体や未使用画像スロットも含む編集シーン</param>
	/// <returns>出力時に検証する取得元。遷移がある場合は間接参照用に存在する登録先も含める</returns>
	std::vector<std::filesystem::path> ForScene(const Scene &scene) const;

	bool operator==(const AssetSources &other) const = default;

  private:
	std::filesystem::path directory_ = L"resources/editor";
	std::vector<std::filesystem::path> directories_;
};
