#pragma once

#include <filesystem>
#include <string>

class AssetDatabase;
class ImGuiSystem;

/// <summary>
/// モデル・材質・画像アセットの一覧・検索・選択と、配置・材質編集要求を管理する
/// </summary>
class AssetsPanel {
  public:
	/// <summary>
	/// モデル・材質・画像をAssets内で選択し、次の描画時に表示する
	/// </summary>
	/// <param name="path">表示するアセットの相対パスまたは絶対パス</param>
	void RequestReveal(const std::filesystem::path &path);

	/// <summary>
	/// アセットの一覧と操作用UIを構築する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="assets">一覧を表示するアセットデータベース</param>
	/// <param name="canSetTexture">材質の下書きへ画像を設定する操作を許可するか</param>
	/// <returns>配置を要求したモデルの相対パス。要求がない場合は空のパス</returns>
	std::filesystem::path Draw(const ImGuiSystem &ui, AssetDatabase &assets,
	                           bool canSetTexture = false);

	/// <summary>
	/// このフレームに使用物体の選択を要求したモデルを取得する
	/// </summary>
	/// <returns>モデルの相対パス。要求なしの場合は空。次のDrawまたはClearまで有効</returns>
	const std::filesystem::path &GetInstanceSelectionRequest() const;

	/// <summary>
	/// このフレームに編集を要求した材質を取得する
	/// </summary>
	/// <returns>材質の相対パス。要求なしは空。次のDrawまたはClearまで有効</returns>
	const std::filesystem::path &GetMaterialEditRequest() const;

	/// <summary>
	/// このフレームに使用物体の選択を要求した材質を取得する
	/// </summary>
	/// <returns>材質の相対パス。要求なしは空。次のDrawまたはClearまで有効</returns>
	const std::filesystem::path &GetMaterialUsageSelectionRequest() const;

	/// <summary>
	/// このフレームに材質の下書きへ設定することを要求した画像を取得する
	/// </summary>
	/// <returns>画像の相対パス。要求なしは空。次のDrawまたはClearまで有効</returns>
	const std::filesystem::path &GetTextureSelectionRequest() const;

	/// <summary>
	/// 選択・検索・表示要求をクリアし、モデル一覧と検索先のルートフォルダへ戻る
	/// </summary>
	void Clear();

	/// <summary>
	/// このフレームで確認用ウィンドウに開くことを要求した画像を取得する
	/// </summary>
	/// <returns>画像パス。要求なしは空。次のDrawまたはClearまで有効</returns>
	const std::filesystem::path &GetTexturePreviewRequest() const;

  private:
	/// <summary>
	/// 一覧に表示するアセットの種類
	/// </summary>
	enum class AssetType { MODEL, MATERIAL, TEXTURE };

	/// <summary>
	/// 表示の種類を切り替え、前の種類の選択と操作要求を解除する
	/// </summary>
	/// <param name="type">表示するモデル・材質・画像</param>
	void SetAssetType(AssetType type);

	/// <summary>
	/// 表示するフォルダを切り替え、選択・検索を解除する
	/// </summary>
	/// <param name="directory">アセットの検索先を基準にしたパス。空は検索先自身</param>
	void OpenDirectory(std::filesystem::path directory);

	/// <summary>
	/// 同じファイルを指す一覧の項目を選択し、親フォルダを開いて検索を解除する
	/// </summary>
	/// <param name="path">探すモデル・材質・画像のパス</param>
	/// <param name="assets">確認するアセット一覧</param>
	/// <returns>true:選択成功 false:一覧に該当なし</returns>
	bool SelectAsset(const std::filesystem::path &path,
	                 const AssetDatabase &assets);

	AssetType type_ = AssetType::MODEL;
	std::filesystem::path textureSelectionPath_;
	std::filesystem::path texturePreviewPath_;
	std::filesystem::path materialEditPath_;
	std::filesystem::path materialUsageSelectionPath_;
	std::filesystem::path selectedPath_;
	std::filesystem::path instanceSelectionPath_;
	std::filesystem::path currentDirectory_;
	std::string filter_;
	std::filesystem::path revealPath_;
	std::string revealMessage_;
	bool scrollToSelected_ = false;
	bool scrollToTop_ = true;
};
