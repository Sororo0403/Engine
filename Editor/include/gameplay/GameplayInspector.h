#pragma once

#include "gameplay/FollowTargetPicker.h"
#include "scene/SceneObjectId.h"

class ImGuiSystem;
class Scene;
class EditorHistory;
struct SceneObject;

/// <summary>
/// 移動・追従カメラ・落下復帰の設定をInspectorで編集する
/// </summary>
class GameplayInspector {
  public:
	/// <summary>
	/// 描画後に表示対象の復帰設定・復帰位置への移動へ適用する操作
	/// </summary>
	enum class RespawnAction { NONE, COPY, PASTE, MOVE_TO_POSITION };

	/// <summary>
	/// 復帰設定の操作と、Inspectorに表示している対象ID
	/// </summary>
	struct RespawnRequest {
		RespawnAction action = RespawnAction::NONE;
		SceneObjectId objectId = 0;
	};

	/// <summary>
	/// 存在するコンポーネントを表示し、編集を履歴へ記録する
	/// </summary>
	/// <param name="ui">描画用UI</param>
	/// <param name="scene">追従対象・ジャンプの着地先を選ぶシーン</param>
	/// <param name="id">表示対象ID</param>
	/// <param name="object">編集する物体</param>
	/// <param name="history">編集履歴</param>
	/// <param name="targetSelectionRequest">追従先の選択要求。要求なしの場合は0。シーンの選択変更は描画後に行う</param>
	/// <param name="canPasteTransform">復帰先へ貼り付けるTransformを保持しているか</param>
	/// <param name="canPasteRespawn">貼り付ける復帰設定を保持しているか</param>
	/// <returns>true:変更あり false:変更なし。選択・貼り付け要求だけでは変更ありにしない</returns>
	bool Draw(const ImGuiSystem &ui, const Scene &scene, SceneObjectId id,
	          SceneObject &object, EditorHistory &history,
	          SceneObjectId &targetSelectionRequest,
	          bool canPasteTransform = false, bool canPasteRespawn = false);
	/// <summary>
	/// コンポーネント選択UIの検索状態を解除する
	/// </summary>
	void Clear() noexcept;

	/// <summary>
	/// このフレームでSceneカメラへ収めることを要求した復帰設定の物体を取得する
	/// </summary>
	/// <returns>Inspectorの表示対象ID。要求なしは0。選択やシーンは変更しない</returns>
	SceneObjectId GetRespawnFrameRequest() const;

	/// <summary>
	/// このフレームにコピー済みの位置を復帰先へ貼り付けることを要求した物体を取得する
	/// </summary>
	/// <returns>Inspectorの表示対象ID。要求なしは0。貼り付けは描画後に行う</returns>
	SceneObjectId GetRespawnPasteRequest() const;

	/// <summary>
	/// このフレームに要求した復帰設定のコピー・貼り付けを取得する
	/// </summary>
	/// <returns>表示対象への要求。要求なしはNONE。実際の操作は描画後に行う</returns>
	RespawnRequest GetRespawnRequest() const;

  private:
	RespawnRequest respawnRequest_;
	FollowTargetPicker targetPicker_;
	SceneObjectId respawnPasteRequest_ = 0;
	SceneObjectId respawnFrameRequest_ = 0;
};
