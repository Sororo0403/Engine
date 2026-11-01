#pragma once

#include <DirectXCollision.h>
#include <array>

/// <summary>
/// Scene画像内の選択矩形と、物体の外接ボックスとの交差を判定する
/// </summary>
class SelectionFrustum {
  public:
	/// <summary>
	/// 更新済みのカメラ行列と正規化した矩形から選択範囲を作成する
	/// </summary>
	/// <param name="view">描画に使用するビュー行列</param>
	/// <param name="projection">左手座標系の透視投影行列</param>
	/// <param name="minimum">矩形の左上。画像の左上0、右下1</param>
	/// <param name="maximum">矩形の右下。画像の左上0、右下1</param>
	/// <returns>true:成功 false:無効な行列、または面積のない矩形</returns>
	bool Initialize(const DirectX::XMFLOAT4X4 &view,
	                const DirectX::XMFLOAT4X4 &projection,
	                const DirectX::XMFLOAT2 &minimum,
	                const DirectX::XMFLOAT2 &maximum);

	/// <summary>
	/// ワールド座標の外接ボックスが選択範囲と交差するか確認する
	/// </summary>
	/// <param name="corners">物体の外接ボックスの8頂点</param>
	/// <returns>true:交差 false:交差なし、未初期化、または無効な頂点</returns>
	bool Intersects(const std::array<DirectX::XMFLOAT3, 8> &corners) const;

  private:
	DirectX::BoundingFrustum frustum_;
	bool initialized_ = false;
};
