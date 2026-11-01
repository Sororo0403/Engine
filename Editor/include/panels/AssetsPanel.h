#pragma once

#include <filesystem>
#include <string>

class AssetDatabase;
class ImGuiSystem;

/// <summary>
/// モデルアセットの一覧・検索・選択と、配置要求を管理する
/// </summary>
class AssetsPanel {
  public:
	/// <summary>
	/// モデルをAssets内で選択し、次の描画時に表示する
	/// </summary>
	/// <param name="path">表示するモデルの相対パスまたは絶対パス</param>
	void RequestReveal(const std::filesystem::path &path);

	/// <summary>
	/// アセットの一覧と操作用UIを構築する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="assets">一覧を表示するアセットデータベース</param>
	/// <returns>配置を要求したモデルの相対パス。要求がない場合は空のパス</returns>
	std::filesystem::path Draw(const ImGuiSystem &ui, AssetDatabase &assets);

	/// <summary>
	/// 選択・検索・表示要求をクリアする
	/// </summary>
	void Clear();

  private:
	/// <summary>
	/// 同じファイルを指す一覧の項目を選択し、検索を解除する
	/// </summary>
	/// <param name="path">探すモデルのパス</param>
	/// <param name="assets">確認するアセット一覧</param>
	/// <returns>true:選択成功 false:一覧に該当なし</returns>
	bool SelectModel(const std::filesystem::path &path, const AssetDatabase &assets);

	std::filesystem::path selectedPath_;
	std::string filter_;
	std::filesystem::path revealPath_;
	std::string revealMessage_;
	bool scrollToSelected_ = false;
};
