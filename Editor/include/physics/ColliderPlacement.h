#pragma once

#include "scene/SceneObjectId.h"
#include <DirectXMath.h>
#include <span>
#include <string>

class Scene;

/// <summary>
/// 選択した衝突形状を、別の衝突形状の面へ配置する
/// </summary>
class ColliderPlacement {
  public:
	/// <summary>
	/// 選択した全物体が存在し、衝突形状を持つか確認する。表示状態には依存しない
	/// </summary>
	/// <param name="scene">確認する編集シーン</param>
	/// <param name="objectIds">選択物体のID一覧</param>
	/// <returns>true:全件に形状あり false:選択なし、対象なし、または形状なし</returns>
	static bool CanDrop(const Scene &scene,
	                    std::span<const SceneObjectId> objectIds);

	/// <summary>
	/// 全選択を同じ距離だけ下げ、最初に接触する上面までの移動量を求める。
	/// 箱・球・カプセルの実形状を直線移動し、選択外のTriggerは支持面から除く。表示状態には依存しない
	/// </summary>
	/// <param name="scene">変更しない編集シーン。選択物体どうしは障害物から除外する</param>
	/// <param name="objectIds">衝突形状を持つ、重複しない選択物体ID一覧</param>
	/// <param name="translation">成功時に設定する共通の移動量。XとZは0</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:取得成功 false:対象や形状が無効、開始時に重なる、支持面なし、または移動結果を表現できない。失敗時は移動量を保持する</returns>
	static bool GetDropTranslation(const Scene &scene,
	                               std::span<const SceneObjectId> objectIds,
	                               DirectX::XMFLOAT3 &translation,
	                               std::string &error);

	/// <summary>
	/// 全選択Colliderの最下端を水平な作業平面へ合わせる共通の上下移動量を求める
	/// </summary>
	/// <param name="scene">変更しない編集シーン。表示状態やモデルの有無には依存しない</param>
	/// <param name="objectIds">Colliderを持つ、重複しない選択物体ID</param>
	/// <param name="height">有限な作業平面のワールドY座標</param>
	/// <param name="translation">成功時だけ設定する共通移動量。XとZは0</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:計算成功または配置済み false:対象・形状・高さ・座標精度が無効。失敗時は出力を保持する</returns>
	/// <remarks>回転・拡縮を反映した軸平行ボックスを使用し、面に微小な余白を残す。上方向への移動も可能。選択外との衝突や移動経路は検証しない</remarks>
	static bool GetPlaneTranslation(const Scene &scene,
	                                std::span<const SceneObjectId> objectIds,
	                                float height,
	                                DirectX::XMFLOAT3 &translation,
	                                std::string &error);

	/// <summary>
	/// 基準物体に対して配置するワールド座標の方向
	/// </summary>
	enum class Face { RIGHT, LEFT, ABOVE, BELOW, FRONT, BACK };

	/// <summary>
	/// 基準以外の選択物体を1群として扱い、指定方向の面へ合わせる共通の移動量を求める。
	/// 回転・拡縮を反映した軸平行のColliderを使用し、面の丸め誤差を避ける微小な余白を残す
	/// </summary>
	/// <param name="scene">変更しない編集シーン。表示状態には依存しない</param>
	/// <param name="objectIds">基準を含む、重複しない2件以上のCollider付き物体ID</param>
	/// <param name="referenceId">位置を変更しない基準物体ID。一覧に含まれていること</param>
	/// <param name="face">基準の右・左・上・下・前・後。前はワールド＋Z</param>
	/// <param name="translation">成功時に設定する、基準以外の全対象に共通の移動量</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:取得成功または配置済み false:対象・形状・方向が無効、または移動を表現できない。失敗時は移動量を保持する</returns>
	static bool GetAdjacentTranslation(const Scene &scene,
	                                   std::span<const SceneObjectId> objectIds,
	                                   SceneObjectId referenceId, Face face,
	                                   DirectX::XMFLOAT3 &translation,
	                                   std::string &error);

  private:
	// 再生時の衝突移動と同じ微小な隙間を残し、接触面への丸め込みを避ける
	static constexpr double kSurfaceClearance = 0.0001;
	static constexpr double kFaceRoundoffFactor = 4.0;
};
