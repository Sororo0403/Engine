#pragma once

#include <DirectXMath.h>

/// <summary>
/// 親からの変形を合成したワールド行列と向き。非一様な拡縮から生じるせん断も行列に保持する
/// </summary>
struct SceneWorldTransform {
	DirectX::XMFLOAT4X4 matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	// 拡縮やせん断を含めずに親から合成した単位クォータニオン
	DirectX::XMFLOAT4 rotation{0, 0, 0, 1};

	/// <summary>
	/// 行列の移動成分からワールド座標の位置を取得する
	/// </summary>
	/// <returns>親の回転と拡縮を含めて合成した位置</returns>
	DirectX::XMFLOAT3 GetPosition() const {
		return {matrix._41, matrix._42, matrix._43};
	}
};
