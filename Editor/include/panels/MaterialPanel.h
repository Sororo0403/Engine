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
	/// 未保存の編集を1操作戻す。現在の入力は先に確定し、ファイルは変更しない
	/// </summary>
	/// <returns>true:復元成功 false:下書き履歴なし。メモリ確保失敗は例外を送出し、編集内容は保持する</returns>
	bool UndoDraft();

	/// <summary>
	/// 未保存の編集を1操作やり直す。新たな入力があれば先に確定してRedoを解除する
	/// </summary>
	/// <returns>true:復元成功 false:やり直す下書き履歴なし。メモリ確保失敗は例外を送出し、編集内容は保持する</returns>
	bool RedoDraft();

	/// <summary>
	/// 最後のDrawでMaterialsまたはその子ウィンドウにフォーカスがあったか取得する
	/// </summary>
	/// <returns>true:材質編集が入力先 false:別のパネルが入力先</returns>
	bool HasFocus() const;

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
	/// このフレームにAssetsで表示することを要求した材質または画像を取得する
	/// </summary>
	/// <returns>実行ファイル基準または絶対パス。要求なしは空。次のDrawまで有効</returns>
	const std::filesystem::path &GetAssetRevealRequest() const;

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

	/// <summary>
	/// 画像を材質ファイル基準の相対パスに変換して下書きへ設定する。1操作の下書き履歴へ記録し、ファイルは保存しない
	/// </summary>
	/// <param name="path">実行ファイル基準または絶対パスの画像。空は白い画像へ戻す</param>
	/// <returns>true:設定成功 false:材質が未選択、または画像や相対パスが無効。失敗時は現在値を保持する</returns>
	bool SelectTexture(const std::filesystem::path &path);

  private:
	/// <summary>
	/// 未保存の材質と、未解釈の入力を含む画像パス欄の状態
	/// </summary>
	struct DraftState {
		MaterialAsset asset;
		std::string texture;
		bool textureValid = true;

		/// <summary>
		/// 表示と設定が一致するか確認する。同じNaN値の入力は変更とみなさない
		/// </summary>
		/// <param name="value">比較する材質の設定</param>
		/// <param name="text">比較する画像パスの入力文字列</param>
		/// <param name="valid">画像パスの解釈に成功しているか</param>
		/// <returns>true:一致 false:不一致</returns>
		bool Matches(const MaterialAsset &value, const std::string &text,
		             bool valid) const;
	};

	/// <summary>
	/// 現在の下書きを1操作として記録する。同値なら何もしない。新規編集ではRedoを解除する
	/// </summary>
	/// <remarks>ドラッグや入力の確定後に呼ぶ。メモリ確保に失敗した場合は下書きと既存履歴を保持する</remarks>
	void CommitDraft();

	/// <summary>
	/// 下書き履歴の状態へ戻す。ファイルと保存済みの材質履歴は変更しない
	/// </summary>
	/// <param name="index">確定済みの下書き履歴の移動先</param>
	/// <returns>true:復元成功 false:範囲外。値の準備に失敗した場合は現在値を保持して例外を送出する</returns>
	bool RestoreDraft(std::size_t index);

	/// <summary>
	/// 保存済みの材質またはモデル既定材質を、現在指定したスロットへ割り当てる要求を用意する
	/// </summary>
	/// <param name="reset">明示的な割り当てを解除する場合はtrue。材質のファイルは変更しない</param>
	/// <returns>true:要求を準備 false:未保存編集・外部変更・無効なスロット。現在の下書きは保持する</returns>
	bool RequestAssignment(bool reset);

	/// <summary>
	/// 指定したAssets内のフォルダへ材質を作成して開く。複製時は保存済みの設定と画像参照を引き継ぐ
	/// </summary>
	/// <param name="assets">作成先と一覧を管理するアセット管理</param>
	/// <param name="duplicate">現在の材質を複製する場合はtrue。画像の参照は複製先を基準に変換する</param>
	/// <returns>true:作成成功 false:未保存の変更・外部変更・無効な名前や参照・作成先の重複。元ファイルは変更しない</returns>
	bool CreateMaterial(AssetDatabase &assets, bool duplicate);

	/// <summary>
	/// このパネルで保存した材質の状態へ戻す。外部変更は上書きしない
	/// </summary>
	/// <param name="index">ローカル履歴の移動先</param>
	/// <returns>true:復元成功 false:無効な履歴または保存失敗</returns>
	bool Restore(std::size_t index);

	std::filesystem::path path_;
	std::filesystem::path usageSelectionPath_;
	std::filesystem::path assetRevealPath_;
	MaterialAsset draft_;
	std::string texture_;
	bool textureValid_ = true;
	bool focusRequested_ = false;
	bool focused_ = false;
	std::string newName_ = "New Material";
	std::string newFolder_ = "materials";
	std::string error_;
	// ファイルへ保存した状態の履歴と、未保存の編集履歴は独立して保持する。
	std::vector<DraftState> draftStates_;
	std::size_t draftCursor_ = 0;
	std::vector<MaterialAsset> states_;
	std::size_t cursor_ = 0;
	int assignmentSlot_ = 0;
	std::optional<AssignmentRequest> assignmentRequest_;
};
