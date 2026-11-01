#pragma once

#include "graphics/render/DebugLine.h"
#include "scene/SceneObjectId.h"
#include <array>

class Scene;
class SceneRenderer;

/// <summary>
/// 選択したCameraの開始時の撮影範囲をSceneビューに表示する
/// </summary>
class CameraFrustum {
  public:
	using Lines = std::array<DebugLine, 12>;

	/// <summary>
	/// NearとFarの四角形および対応する頂点を結ぶ線をワールド座標で組み立てる
	/// </summary>
	/// <param name="scene">参照する編集シーン。追従対象もこのシーンから取得する</param>
	/// <param name="id">表示するCameraのID。PrimaryでないCameraも対象とする</param>
	/// <param name="width">ゲームの描画幅</param>
	/// <param name="height">ゲームの描画高さ</param>
	/// <param name="lines">成功時に置き換える線。失敗時は保持する</param>
	/// <returns>true:作成成功 false:Camera・追従先・設定・サイズが無効</returns>
	bool BuildLines(const Scene &scene, SceneObjectId id, int width, int height,
	                Lines &lines) const;

	/// <summary>
	/// 有効なCameraの場合だけ、Scene描画へ撮影範囲の線を登録する
	/// </summary>
	/// <param name="renderer">更新済みのScene描画先</param>
	/// <param name="scene">編集シーン</param>
	/// <param name="id">表示する物体のID</param>
	/// <param name="width">ゲームの描画幅</param>
	/// <param name="height">ゲームの描画高さ</param>
	/// <returns>true:登録成功または表示対象なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const Scene &scene, SceneObjectId id,
	            int width, int height) const;

  private:
	DirectX::XMFLOAT4 color_{1.0f, 0.75f, 0.2f, 1.0f};
};
