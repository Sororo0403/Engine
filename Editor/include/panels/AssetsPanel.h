#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

class AssetDatabase;
class ImGuiSystem;

/// <summary>
/// モデル・画像・音声・シーン・ゲーム設定の一覧・検索・選択と、配置・画像割り当て・シーンを開く要求を管理する
/// </summary>
class AssetsPanel {
  public:
	/// <summary>
	/// 選択物体のモデルから得た、番号を変えない画像スロットの表示候補
	/// </summary>
	struct TextureSlotChoice {
		int index = 0;
		std::string name;
		std::size_t usedBy = 0;
		std::size_t selectedCount = 0;
	};

	/// <summary>
	/// モデル・画像・音声・シーン・ゲーム設定をAssets内で選択し、次の描画時に表示する
	/// </summary>
	/// <param name="path">表示するアセットの相対パスまたは絶対パス</param>
	/// <param name="keepResults">表示中の検索や操作結果へ戻れるようにするか</param>
	void RequestReveal(const std::filesystem::path &path, bool keepResults = false);

	/// <summary>
	/// 移動・複製の結果を選択する。複数フォルダにまたがる場合は結果の一覧を表示する
	/// </summary>
	void RequestSelectResults(std::vector<std::filesystem::path> paths);

	/// <summary>
	/// 素材が現在の一覧にあれば選択し、Assetsパネルの表示を予約する
	/// </summary>
	bool RevealIndexedAsset(const std::filesystem::path &path,
	                       const AssetDatabase &assets);

	/// <summary>
	/// 移動が完了した素材と、その配下にある一覧・選択・クリップボードの参照を更新する
	/// </summary>
	void RelocatePaths(std::span<const std::pair<std::filesystem::path,
	                                            std::filesystem::path>> moves,
	                   const std::filesystem::path &root);

	/// <summary>
	/// 削除済みの素材を選択・操作結果・クリップボードから取り除く
	/// </summary>
	void RemoveMissingPaths();

	/// <summary>
	/// アセットの一覧と操作用UIを構築する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="assets">一覧を表示するアセットデータベース</param>
	/// <param name="canSetTexture">選択物体のスロットへ画像を割り当てる操作を許可するか</param>
	/// <param name="canEditAssets">素材の取り込み、画像・モデルの再読み込み、フォルダ作成・シーンを開く操作・保存済み物体の追加を開始できるか</param>
	/// <param name="canReplaceMesh">全選択物体の既存Meshを差し替えられるか</param>
	/// <param name="textureSlots">選択物体の材質名と使用状況。番号選択の補助に使い、空でも手入力は可能</param>
	/// <param name="canCreatePrefab">単独選択の階層をPrefabとして作成できるか</param>
	/// <param name="canReturnToExport">配布前チェックへ戻る操作を表示するか</param>
	/// <returns>配置を要求したモデルの相対パス。要求がない場合は空のパス</returns>
	std::filesystem::path
	Draw(const ImGuiSystem &ui, AssetDatabase &assets,
	     bool canSetTexture = false, bool canEditAssets = false,
	     bool canReplaceMesh = false,
	     std::span<const TextureSlotChoice> textureSlots = {},
	     bool canCreatePrefab = false, bool canReturnToExport = false);

	/// <summary>
	/// このフレームに配布前チェックへ戻る操作が選ばれたかを返す
	/// </summary>
	bool DidRequestExportCheck() const { return exportCheckRequested_; }

	/// <summary>
	/// このフレームに開くことを要求したシーンを取得する。保存確認と読み込みは呼び出し側で行う
	/// </summary>
	/// <returns>シーンの参照先。要求なしは空。次のDraw・種類切り替え・Clearまで有効</returns>
	const std::filesystem::path &GetSceneOpenRequest() const;

	/// <summary>
	/// 編集シーンを切り替えずに試遊するシーンを取得する
	/// </summary>
	const std::filesystem::path &GetScenePlayRequest() const;

	/// <summary>
	/// このフレームに現在のシーンへの物体追加を要求したシーンを取得する
	/// </summary>
	/// <returns>シーンの参照先。要求なしは空。次のDraw・種類切り替え・Clearまで有効</returns>
	const std::filesystem::path &GetSceneAppendRequest() const;

	/// <summary>
	/// このフレームで配置を要求したPrefabを取得する
	/// </summary>
	/// <returns>Prefabの参照先。要求なしは空。次のDraw・種類切り替え・Clearまで有効</returns>
	const std::filesystem::path &GetPrefabAppendRequest() const;

	/// <summary>
	/// 単独選択の階層からPrefabを作成する要求があったか確認する
	/// </summary>
	/// <returns>true:作成要求あり false:要求なし。次のDraw・種類切り替え・Clearで解除する</returns>
	bool IsPrefabCreateRequested() const;

	/// <summary>
	/// このフレームに、ゲーム設定の下書きへ読み込むことを要求したパスを取得する
	/// </summary>
	/// <returns>設定ファイル。要求なしは空。次のDraw・種類切り替え・Clearまで有効</returns>
	const std::filesystem::path &GetGameSettingsRequest() const;

	/// <summary>
	/// このフレームに再読み込みを要求したモデルを取得する
	/// </summary>
	/// <returns>モデルの相対パス。要求なしは空。次のDraw・種類切り替え・Clearまで有効</returns>
	const std::filesystem::path &GetModelReloadRequest() const;

	/// <summary>
	/// このフレームに選択物体のMeshへの差し替えを要求したモデルを取得する
	/// </summary>
	/// <returns>モデルの相対パス。要求なしは空。次のDraw・種類切り替え・Clearまで有効</returns>
	const std::filesystem::path &GetMeshReplacementRequest() const;

	/// <summary>
	/// このフレームに共有する画像の再読み込みを要求したパスを取得する
	/// </summary>
	/// <returns>画像の相対パス。要求なしは空。次のDraw・種類切り替え・Clearまで有効</returns>
	const std::filesystem::path &GetTextureReloadRequest() const;

	/// <summary>
	/// このフレームに一括再読み込みを要求したモデルと画像を取得する
	/// </summary>
	const std::vector<std::filesystem::path> &GetBatchReloadRequest() const {
		return batchReloadPaths_;
	}

	/// <summary>
	/// このフレームに選択物体へ割り当てることを要求した画像を取得する
	/// </summary>
	/// <returns>画像の相対パス。要求なしは空。次のDrawまたはClearまで有効</returns>
	const std::filesystem::path &GetTextureSelectionRequest() const;

	/// <summary>
	/// このフレームに、選択物体の指定スロットを白へ戻す要求があったか確認する
	/// </summary>
	/// <returns>true:解除要求あり false:要求なし。次のDraw・種類切り替え・Clearで解除する</returns>
	bool IsTextureClearRequested() const;

	/// <summary>
	/// このフレームに外部画像の取り込みを要求したか確認する
	/// </summary>
	/// <returns>true:要求あり false:要求なし。次のDraw・種類切り替え・Clearで解除する</returns>
	bool IsTextureImportRequested() const;

	/// <summary>
	/// このフレームに外部WAVの取り込みを要求したか確認する
	/// </summary>
	/// <returns>true:要求あり false:要求なし。次のDraw・種類切り替え・Clearで解除する</returns>
	bool IsAudioImportRequested() const;

	/// <summary>
	/// このフレームに外部モデルの取り込みを要求したか確認する
	/// </summary>
	/// <returns>true:要求あり false:要求なし。次のDraw・種類切り替え・Clearで解除する</returns>
	bool IsModelImportRequested() const;

	/// <summary>
	/// このフレームに新しいAssetsフォルダの作成を要求したか確認する
	/// </summary>
	/// <returns>true:要求あり false:要求なし。次のDraw・種類切り替え・Clearで解除する</returns>
	bool IsFolderCreateRequested() const;

	/// <summary>
	/// 今回の描画中に素材一覧の再読み込みが成功したか確認する
	/// </summary>
	bool DidRefresh() const { return refreshed_; }

	/// <summary>
	/// 次の描画で既存のAssetsフォルダへ移動し、検索とアセット選択を解除する
	/// </summary>
	/// <param name="folder">検索先基準の相対フォルダ。空は検索先自身。呼び出し前に一覧を更新しておく</param>
	void RequestOpenDirectory(const std::filesystem::path &folder);

	/// <summary>
	/// Assets基準のフォルダを開く。一覧取得に成功し、フォルダが消えていれば親へ戻る
	/// </summary>
	void RestoreDirectory(std::filesystem::path folder, const AssetDatabase &assets);

	/// <summary>
	/// 開いているAssetsフォルダを取得する
	/// </summary>
	/// <returns>検索先を基準にした相対パス。空は検索先自身</returns>
	const std::filesystem::path &GetOpenDirectory() const;

	/// <summary>
	/// 選択・検索・表示要求をクリアし、モデル一覧と検索先のルートフォルダへ戻る
	/// </summary>
	void Clear();

	/// <summary>
	/// コンテキストメニューでごみ箱への移動を要求されたファイルを取得する
	/// </summary>
	const std::vector<std::filesystem::path> &GetRecycleRequest() const;

	/// <summary>
	/// 参照元を調べる素材・フォルダの選択を取得する
	/// </summary>
	const std::vector<std::filesystem::path> &GetReferenceRequest() const;

	/// <summary>
	/// 名前変更が要求された素材ファイルを取得する
	/// </summary>
	const std::filesystem::path &GetRenameRequest() const;

	/// <summary>
	/// フォルダへのドロップで要求された移動元と移動先
	/// </summary>
	struct MoveRequest {
		std::vector<std::filesystem::path> sources;
		std::filesystem::path directory;
	};

	const MoveRequest &GetMoveRequest() const;

	/// <summary>
	/// Assets内のクリップボードから、現在のフォルダへの貼り付け要求を取得する
	/// </summary>
	const MoveRequest &GetPasteRequest() const;
	bool IsCutPaste() const;
	void FinishPaste();

	/// <summary>
	/// このフレームで確認用ウィンドウに開くことを要求した画像を取得する
	/// </summary>
	/// <returns>画像パス。要求なしは空。次のDrawまたはClearまで有効</returns>
	const std::filesystem::path &GetTexturePreviewRequest() const;

	/// <summary>
	/// このフレームで試聴用ウィンドウに開くことを要求したWAVを取得する
	/// </summary>
	/// <returns>音声パス。要求なしは空。次のDraw・種類切り替え・Clearまで有効</returns>
	const std::filesystem::path &GetAudioPreviewRequest() const;

	/// <summary>
	/// Assetsから画像を割り当てるスロット番号を取得する
	/// </summary>
	/// <returns>0以上のスロット番号。不正入力中は割り当て要求を生成しない</returns>
	std::uint32_t GetTextureSlot() const;

	/// <summary>
	/// このフレームにLuaアセットの作成を要求したか確認する
	/// </summary>
	/// <returns>true:作成要求。次のDraw・種類切り替え・Clearで解除する</returns>
	bool IsScriptCreateRequested() const {
		return scriptCreateRequested_;
	}

	/// <summary>
	/// このフレームにコード編集を要求したLuaアセットを取得する
	/// </summary>
	/// <returns>対象パス。次のDraw・種類切り替え・Clearで解除する</returns>
	const std::filesystem::path &GetScriptEditRequest() const {
		return scriptEditPath_;
	}

	bool IsAnimationClipCreateRequested() const { return clipCreateRequested_; }
	const std::filesystem::path &GetAnimationClipEditRequest() const {
		return clipEditPath_;
	}

  private:
	/// <summary>
	/// 一覧に表示するアセットの種類
	/// </summary>
	enum class AssetType {
		MODEL,
		TEXTURE,
		AUDIO,
		SCENE,
		GAME_SETTINGS,
		PREFAB,
		SCRIPT,
		ANIMATION_CLIP,
		MATERIAL
	};

	/// <summary>
	/// 表示の種類を切り替え、前の種類の選択と操作要求を解除する
	/// </summary>
	/// <param name="type">表示するモデル・画像・音声・シーン・Prefab・ゲーム設定</param>
	void SetAssetType(AssetType type);

	/// <summary>
	/// 表示するフォルダを切り替え、選択・検索を解除する
	/// </summary>
	/// <param name="directory">アセットの検索先を基準にしたパス。空は検索先自身</param>
	void OpenDirectory(std::filesystem::path directory);

	/// <summary>
	/// 同じファイルを指す一覧の項目を選択し、親フォルダを開いて検索を解除する
	/// </summary>
	/// <param name="path">探すモデル・画像・音声・シーン・Prefab・ゲーム設定のパス</param>
	/// <param name="assets">確認するアセット一覧</param>
	/// <returns>true:選択成功 false:一覧に該当なし</returns>
	bool SelectAsset(const std::filesystem::path &path,
	                 const AssetDatabase &assets);

	AssetType type_ = AssetType::MODEL;
	std::filesystem::path textureSelectionPath_;
	std::filesystem::path texturePreviewPath_;
	std::filesystem::path audioPreviewPath_;
	std::filesystem::path selectedPath_;
	std::vector<std::filesystem::path> selectedItems_;
	std::filesystem::path selectionAnchor_;
	std::filesystem::path sceneOpenPath_;
	std::filesystem::path scenePlayPath_;
	std::filesystem::path gameSettingsPath_;
	std::filesystem::path sceneAppendPath_;
	std::filesystem::path prefabAppendPath_;
	std::filesystem::path modelReloadPath_;
	std::filesystem::path meshReplacementPath_;
	std::filesystem::path textureReloadPath_;
	std::vector<std::filesystem::path> batchReloadPaths_;
	/// <summary>
	/// コピー・切り取り元を検査し、欠損時は貼り付けを保留する
	/// </summary>
	bool ValidateClipboard();

	struct ResultView {
		std::string search;
		std::vector<std::filesystem::path> results;
		std::vector<std::filesystem::path> selection;
		std::filesystem::path directory;
		std::filesystem::path anchor;
		AssetType type = AssetType::MODEL;
		bool allTypes = false;
		bool currentFolderOnly = false;
	};

	/// <summary>
	/// 表示中の検索や操作結果を記録する。必要な場合は通常のフォルダ表示も記録する
	/// </summary>
	std::optional<ResultView> CaptureResults(bool includeFolder = false) const;

	/// <summary>
	/// 新しい表示先を受け付ける前に、古い表示と再試行の予約を取り消す
	/// </summary>
	void ClearNavigationRequests();

	std::filesystem::path currentDirectory_;
	bool refreshAfterExplorer_ = false;
	bool explorerTookFocus_ = false;
	std::string filter_;
	bool searchAllTypes_ = false;
	bool searchCurrentFolderOnly_ = false;
	std::optional<ResultView> previousView_;
	std::filesystem::path revealPath_;
	bool keepRevealResults_ = false;
	std::vector<std::filesystem::path> resultPaths_;
	std::vector<std::filesystem::path> displayedResults_;
	std::filesystem::path unavailableRevealPath_;
	bool focusRequested_ = false;
	std::string revealMessage_;
	int textureSlot_ = 0;
	std::vector<std::filesystem::path> recyclePaths_;
	std::vector<std::filesystem::path> referencePaths_;
	std::filesystem::path renamePath_;
	MoveRequest moveRequest_;
	MoveRequest pasteRequest_;
	std::vector<std::filesystem::path> clipboardItems_;
	std::filesystem::path clipboardRoot_;
	bool clipboardCut_ = false;
	bool clipboardHasMissingItems_ = false;
	bool textureClearRequested_ = false;
	bool textureImportRequested_ = false;
	bool audioImportRequested_ = false;
	bool modelImportRequested_ = false;
	bool folderCreateRequested_ = false;
	bool refreshed_ = false;
	bool exportCheckRequested_ = false;
	bool scriptCreateRequested_ = false;
	std::filesystem::path scriptEditPath_, clipEditPath_;
	bool clipCreateRequested_ = false;
	bool prefabCreateRequested_ = false;
	bool directoryOpenRequested_ = false;
	std::filesystem::path requestedDirectory_;
	bool scrollToSelected_ = false;
	bool scrollToTop_ = true;
};
