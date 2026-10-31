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
	/// アセットの一覧と操作用UIを構築する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="assets">一覧を表示するアセットデータベース</param>
	/// <returns>配置を要求したモデルの相対パス。要求がない場合は空のパス</returns>
	std::filesystem::path Draw(const ImGuiSystem &ui, AssetDatabase &assets);

	/// <summary>
	/// 選択と検索文字列をクリアする
	/// </summary>
	void Clear();

  private:
	std::filesystem::path selectedPath_;
	std::string filter_;
};
