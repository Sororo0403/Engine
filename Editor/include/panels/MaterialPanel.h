#pragma once

#include "graphics/material/MaterialAsset.h"
#include <filesystem>
#include <string>
#include <vector>
class ImGuiSystem;
class AssetDatabase;

/// <summary>
/// モデルと独立した材質ファイルの作成・選択・編集・保存を管理する
/// </summary>
class MaterialPanel {
  public:
	/// <summary>
	/// 材質アセットの一覧と編集UIを表示する。保存すると共有する全物体へ反映される
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="assets">材質ファイルを列挙するアセット管理</param>
	void Draw(const ImGuiSystem &ui, AssetDatabase &assets);

  private:
	/// <summary>
	/// 未保存の編集がない場合に、指定した材質を読み込む
	/// </summary>
	/// <param name="path">実行ファイル基準の材質パス</param>
	/// <returns>true:選択成功 false:編集中または読み込み失敗</returns>
	bool Select(const std::filesystem::path &path);

	/// <summary>
	/// 現在の編集を保存する。外部変更がある場合は上書きせず失敗する
	/// </summary>
	/// <returns>true:保存成功 false:設定や保存先が無効</returns>
	bool Save();

	/// <summary>
	/// このパネルで保存した材質の状態へ戻す。外部変更は上書きしない
	/// </summary>
	/// <param name="index">ローカル履歴の移動先</param>
	/// <returns>true:復元成功 false:無効な履歴または保存失敗</returns>
	bool Restore(std::size_t index);

	std::filesystem::path path_;
	MaterialAsset draft_;
	std::string texture_;
	std::string newName_ = "New Material";
	std::string error_;
	std::vector<MaterialAsset> states_;
	std::size_t cursor_ = 0;
};
