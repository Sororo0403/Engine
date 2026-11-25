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

  private:
	// 再生時の衝突移動と同じ微小な隙間を残し、接触面への丸め込みを避ける
	static constexpr double kSurfaceClearance = 0.0001;
};
