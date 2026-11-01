#pragma once

#include <DirectXMath.h>
#include <array>
#include <span>

struct Ray;

/// <summary>
/// モデルをSceneの地面へ配置する位置と移動量を求める
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

	/// <summary>
	/// 全ボックスの最下端を地面へ合わせる、共通の上下移動量を求める。微小な誤差は移動なしとする
	/// </summary>
	/// <param name="bounds">回転・拡縮を反映済みのワールド座標の8頂点一覧</param>
	/// <param name="translation">成功した場合に設定する移動量。XとZは0</param>
	/// <returns>true:成功 false:対象なし、または値が無効。失敗時は出力を変更しない</returns>
	static bool GetGroundTranslation(std::span<const std::array<DirectX::XMFLOAT3, 8>> bounds,
	                                 DirectX::XMFLOAT3 &translation);

  private:
	static constexpr float kGroundTolerance = 0.00001f;
	static constexpr float kDefaultDistance = 3.0f;
	static constexpr float kParallelTolerance = 0.000001f;
	static constexpr float kDirectionTolerance = 0.001f;
};
