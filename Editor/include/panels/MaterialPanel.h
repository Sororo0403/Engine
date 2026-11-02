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

	/// <summary>
	/// 現在の編集を保存する。外部変更がある場合は上書きせず失敗する
	/// </summary>
	/// <returns>true:保存成功 false:設定や保存先が無効</returns>
	bool Save();

	/// <summary>
	/// 保存した材質と異なる編集、または未解釈のテクスチャ入力が残っているか確認する
	/// </summary>
	/// <returns>true:未保存の編集あり false:保存済み、または材質を選択していない</returns>
	bool HasUnsavedChanges() const;

	/// <summary>
	/// 編集している材質の保存先を取得する
	/// </summary>
	/// <returns>材質ファイルのパス。未選択の場合は空</returns>
	const std::filesystem::path &GetFilePath() const;

	/// <summary>
	/// 材質の読み込み・編集・保存で発生した最後のエラーを取得する
	/// </summary>
	/// <returns>失敗理由。エラーなしの場合は空</returns>
	const std::string &GetError() const;

	/// <summary>
	/// 材質を読み込み、成功した場合だけ編集中のデータと履歴を置き換える
	/// </summary>
	/// <param name="path">実行ファイル基準または絶対パスの材質ファイル</param>
	/// <param name="discardChanges">保存確認で破棄を選択した場合のみtrueを指定する</param>
	/// <returns>true:成功 false:未保存編集あり、または読み込み失敗。失敗時は現在値を保持する</returns>
	bool Open(const std::filesystem::path &path, bool discardChanges = false);

	/// <summary>
	/// 次の描画で材質パネルを開き、フォーカスする。編集中の値は変更しない
	/// </summary>
	void RequestFocus();

  private:
	/// <summary>
	/// このパネルで保存した材質の状態へ戻す。外部変更は上書きしない
	/// </summary>
	/// <param name="index">ローカル履歴の移動先</param>
	/// <returns>true:復元成功 false:無効な履歴または保存失敗</returns>
	bool Restore(std::size_t index);

	std::filesystem::path path_;
	MaterialAsset draft_;
	std::string texture_;
	bool textureValid_ = true;
	bool focusRequested_ = false;
	std::string newName_ = "New Material";
	std::string error_;
	std::vector<MaterialAsset> states_;
	std::size_t cursor_ = 0;
};
