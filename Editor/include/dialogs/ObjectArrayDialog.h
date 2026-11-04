#pragma once

#include "history/ObjectArraySettings.h"
#include "physics/ColliderPlacement.h"
#include "scene/SceneObjectId.h"
#include <optional>
#include <span>
#include <string>
#include <vector>

class ImGuiSystem;
class Scene;

/// <summary>
/// 等間隔複製ダイアログの操作結果
/// </summary>
enum class ObjectArrayResult { NONE, ACCEPTED, CANCELLED };

/// <summary>
/// 複製対象・組数・ワールド座標の間隔を指定するダイアログ
/// </summary>
class ObjectArrayDialog {
  public:
	/// <summary>
	/// 重複のない対象IDを保持して表示を予約する。シーンは変更しない
	/// </summary>
	/// <param name="scene">元の物体が存在するシーン</param>
	/// <param name="ids">複製順に並べた元の物体ID。アクティブ物体は最後</param>
	/// <returns>true:表示予約成功 false:対象なし、重複、削除済み、または上限超過</returns>
	bool Show(const Scene &scene, std::span<const SceneObjectId> ids);

	/// <summary>
	/// 組数・間隔・Collider間の最小すき間を編集し、確定・取消の要求を返す
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="scene">現在の元シーン。Colliderによる間隔の計算と確定前の再検証に使用する</param>
	/// <returns>未確定の場合はNONE。ACCEPTEDでもシーンは変更しない</returns>
	ObjectArrayResult Draw(const ImGuiSystem &ui, const Scene &scene);

	/// <summary>
	/// 複製対象を取得する
	/// </summary>
	/// <returns>次のShow成功まで有効な元のID一覧</returns>
	std::span<const SceneObjectId> GetSourceIds() const;

	/// <summary>
	/// 入力した複製設定を取得する
	/// </summary>
	/// <returns>現在の入力値。適用側でもIsValidによる検証が必要</returns>
	const ObjectArraySettings &GetSettings() const;

	/// <summary>
	/// 入力設定が有効で、未解決のCollider間隔エラーがないか確認する
	/// </summary>
	/// <returns>true:下書きの外枠を計算できる設定 false:設定または間隔の計算が無効。元の形状は計算側で再確認する</returns>
	bool CanPreview() const {
		return settings_.IsValid(sourceIds_.size()) && spacingError_.empty();
	}

  private:
	std::vector<SceneObjectId> sourceIds_;
	ObjectArraySettings settings_;
	bool openRequested_ = false;
	std::optional<ColliderPlacement::Face> spacingFace_;
	int fittedCopies_ = 0;
	double minimumGap_ = 0;
	std::string spacingError_;
};
