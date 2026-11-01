#pragma once

#include <DirectXMath.h>

struct Ray;

/// <summary>
/// Sceneビューへドロップしたモデルの配置位置を求める
/// </summary>
class ModelPlacement {
  public:
	/// <summary>
	/// レイと地面の交点を求める。交差しない場合はレイ上の手前に配置する
	/// </summary>
	/// <param name="ray">描画範囲内の正規化されたワールド座標レイ</param>
	/// <param name="position">成功した場合に設定する配置位置</param>
	/// <returns>true:成功 false:レイが無効</returns>
	static bool GetPosition(const Ray &ray, DirectX::XMFLOAT3 &position);

  private:
	static constexpr float kDefaultDistance = 3.0f;
	static constexpr float kParallelTolerance = 0.000001f;
	static constexpr float kDirectionTolerance = 0.001f;
};
