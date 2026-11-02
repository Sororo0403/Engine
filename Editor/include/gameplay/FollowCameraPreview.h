#pragma once

#include "graphics/render/DebugLine.h"
#include "scene/SceneObjectId.h"
#include <array>

class Scene;
class SceneRenderer;

/// <summary>
/// 選択したFollow Cameraの追従先・開始位置・注視点をSceneビューに表示する
/// </summary>
class FollowCameraPreview {
  public:
	using Lines = std::array<DebugLine, 8>;

	/// <summary>
	/// 追従先と注視点の十字、およびCameraの開始位置への接続線をワールド座標で作る
	/// </summary>
	/// <param name="scene">参照する編集シーン。追従するCameraの連鎖も解決する</param>
	/// <param name="id">表示するFollow CameraのID。Primaryや表示状態には依存しない</param>
	/// <param name="lines">成功時に置き換える線。表示対象なしまたは無効な場合は保持する</param>
	/// <returns>true:作成成功 false:Follow Cameraなし、または設定・追従先・座標が無効</returns>
	bool BuildLines(const Scene &scene, SceneObjectId id, Lines &lines) const;

	/// <summary>
	/// 有効なFollow Cameraの場合だけ、Scene描画へ追従関係の線を登録する
	/// </summary>
	/// <param name="renderer">更新済みのScene描画先</param>
	/// <param name="scene">編集シーン。値は変更しない</param>
	/// <param name="id">表示する物体のID</param>
	/// <returns>true:登録成功または表示対象なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const Scene &scene,
	            SceneObjectId id) const;

  private:
	static constexpr float kMarkerRadius = 0.25f;
	DirectX::XMFLOAT4 targetColor_{0.2f, 0.85f, 1.0f, 1.0f};
	DirectX::XMFLOAT4 lookColor_{1.0f, 0.75f, 0.2f, 1.0f};
};
