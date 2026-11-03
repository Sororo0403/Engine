#pragma once

#include "graphics/render/DebugLine.h"
#include "scene/SceneObjectId.h"
#include <array>
#include <cstddef>
#include <vector>

class Scene;
class SceneRenderer;

/// <summary>
/// 選択したFollow Cameraの追従先・開始位置・注視点と、壁回避の余裕幅をSceneビューに表示する
/// </summary>
class FollowCameraPreview {
  public:
	using Lines = std::vector<DebugLine>;

	/// <summary>
	/// 追従関係の線と、回避有効時の余裕幅のボックスをワールド座標で作る。初期配置できない場合は赤で示す
	/// </summary>
	/// <param name="scene">参照する編集シーン。追従するCameraの連鎖も解決する</param>
	/// <param name="id">表示するFollow CameraのID。Primaryや表示状態には依存しない</param>
	/// <param name="lines">成功時に置き換える線。表示対象なし・座標や参照が無効な場合は保持する。壁回避の初期配置が不成立でも赤いガイドは取得できる</param>
	/// <returns>true:作成成功 false:Follow Cameraなし、または設定・追従先・座標が無効</returns>
	bool BuildLines(const Scene &scene, SceneObjectId id, Lines &lines) const;

	/// <summary>
	/// 描画する経路・余裕幅・希望位置の全てを囲む、Sceneカメラのフレーム用範囲を取得する
	/// </summary>
	/// <param name="scene">参照する編集シーン。値は変更しない</param>
	/// <param name="id">表示するFollow CameraのID</param>
	/// <param name="corners">成功時に置き換える8頂点。失敗時は保持する</param>
	/// <returns>true:取得成功。初期配置できない経路も含む false:設定・参照・座標が無効</returns>
	bool GetFrameBounds(const Scene &scene, SceneObjectId id,
	                    std::array<DirectX::XMFLOAT3, 8> &corners) const;

	/// <summary>
	/// 参照と座標が有効なFollow Cameraの追従関係と余裕幅を、Scene描画へ登録する
	/// </summary>
	/// <param name="renderer">更新済みのScene描画先</param>
	/// <param name="scene">編集シーン。値は変更しない</param>
	/// <param name="id">表示する物体のID</param>
	/// <returns>true:登録成功または表示対象なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const Scene &scene,
	            SceneObjectId id) const;

  private:
	static constexpr float kMarkerRadius = 0.25f;
	static constexpr std::size_t kMaximumLineCount = 45;
	DirectX::XMFLOAT4 targetColor_{0.2f, 0.85f, 1.0f, 1.0f};
	DirectX::XMFLOAT4 lookColor_{1.0f, 0.75f, 0.2f, 1.0f};
	DirectX::XMFLOAT4 clearanceColor_{0.7f, 0.4f, 1.0f, 1.0f};
	DirectX::XMFLOAT4 requestedColor_{0.6f, 0.6f, 0.6f, 1.0f};
	DirectX::XMFLOAT4 blockedColor_{1.0f, 0.2f, 0.2f, 1.0f};
};
