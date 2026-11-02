#pragma once

#include "graphics/material/MaterialAsset.h"
#include <cstdint>
#include <filesystem>
#include <optional>
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
	/// <param name="canAssignMaterial">選択した全物体へ材質を割り当てる操作を許可するか</param>
	void Draw(const ImGuiSystem &ui, AssetDatabase &assets,
	          bool canAssignMaterial = false);

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

	/// <summary>
	/// このフレームに使用物体の選択を要求した材質を取得する
	/// </summary>
	/// <returns>材質パス。要求なしの場合は空。次のDrawまで有効</returns>
	const std::filesystem::path &GetUsageSelectionRequest() const;

	/// <summary>
	/// 選択物体の材質スロットへ適用する要求。シーンの変更は描画後に行う
	/// </summary>
	struct AssignmentRequest {
		std::uint32_t slot = 0;
		std::filesystem::path path;
	};

	/// <summary>
	/// この描画で要求した材質の一括割り当てを取得する
	/// </summary>
	/// <returns>次のDrawまで有効な要求。pathが空の場合はモデル既定材質へ戻す。要求なしはnullopt</returns>
	const std::optional<AssignmentRequest> &GetAssignmentRequest() const;

  private:
	/// <summary>
	/// 保存済みの材質またはモデル既定材質を、現在指定したスロットへ割り当てる要求を用意する
	/// </summary>
	/// <param name="reset">明示的な割り当てを解除する場合はtrue。材質のファイルは変更しない</param>
	/// <returns>true:要求を準備 false:未保存編集・外部変更・無効なスロット。現在の下書きは保持する</returns>
	bool RequestAssignment(bool reset);

	/// <summary>
	/// 新しい材質を作成して開く。複製時は保存済みの設定と画像参照を引き継ぐ
	/// </summary>
	/// <param name="assets">作成先と一覧を管理するアセット管理</param>
	/// <param name="duplicate">現在の材質を複製する場合はtrue。画像の参照は複製先を基準に変換する</param>
	/// <returns>true:作成成功 false:未保存の変更・外部変更・無効な名前や参照・作成先の重複。元ファイルは変更しない</returns>
	bool CreateMaterial(AssetDatabase &assets, bool duplicate);

	/// <summary>
	/// 画像を材質ファイル基準の相対パスに変換して下書きへ設定する。ファイルは保存しない
	/// </summary>
	/// <param name="path">実行ファイル基準または絶対パスの画像。空は白い画像へ戻す</param>
	/// <returns>true:設定成功 false:材質が未選択、または画像や相対パスが無効。失敗時は現在値を保持する</returns>
	bool SelectTexture(const std::filesystem::path &path);

	/// <summary>
	/// このパネルで保存した材質の状態へ戻す。外部変更は上書きしない
	/// </summary>
	/// <param name="index">ローカル履歴の移動先</param>
	/// <returns>true:復元成功 false:無効な履歴または保存失敗</returns>
	bool Restore(std::size_t index);

	std::filesystem::path path_;
	std::filesystem::path usageSelectionPath_;
	MaterialAsset draft_;
	std::string texture_;
	bool textureValid_ = true;
	bool focusRequested_ = false;
	std::string newName_ = "New Material";
	std::string error_;
	std::vector<MaterialAsset> states_;
	std::size_t cursor_ = 0;
	int assignmentSlot_ = 0;
	std::optional<AssignmentRequest> assignmentRequest_;
};
