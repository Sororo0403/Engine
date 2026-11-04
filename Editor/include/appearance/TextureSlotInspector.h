#pragma once

#include "scene/SceneObjectId.h"
#include <filesystem>
class ImGuiSystem;
class EditorHistory;
class ModelManager;
class AssetDatabase;
struct SceneObject;

/// <summary>
/// 画像スロットの割り当てと、物体ごとのUV調整を編集するUI
/// </summary>
class TextureSlotInspector {
  public:
	/// <summary>
	/// 描画後にAssetsで表示する画像と、UV適用・未使用画像割り当ての整理要求
	/// </summary>
	struct Request {
		std::filesystem::path revealTexture;
		bool applyUvToSelection = false;
		bool copyUv = false;
		bool pasteUvToSelection = false;
		bool clearUnusedTextures = false;
	};

	/// <summary>
	/// UVの連続編集とリセット・画像の割り当てを表示し、履歴へ記録する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="id">表示中の物体</param>
	/// <param name="object">変更する物体データ</param>
	/// <param name="history">UVと割り当ての編集履歴</param>
	/// <param name="models">材質スロット数を取得するモデル管理</param>
	/// <param name="assets">割り当て可能な画像の一覧</param>
	/// <param name="request">画像を表示する要求。操作なしの場合はクリアする</param>
	/// <param name="canApplyUvToSelection">全選択物体へUVを適用する要求を許可するか</param>
	/// <param name="canPasteUv">コピー済みの有効なUVがあるか</param>
	/// <returns>true:UVまたは割り当て変更あり false:変更なし、または編集要求のみ</returns>
	bool Draw(const ImGuiSystem &ui, SceneObjectId id, SceneObject &object,
	          EditorHistory &history, ModelManager &models,
	          const AssetDatabase &assets, Request &request,
	          bool canApplyUvToSelection = false,
	          bool canPasteUv = false) const;
};
