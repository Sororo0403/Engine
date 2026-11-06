#pragma once
#include "graphics/ShadowSettings.h"
#include <DirectXMath.h>

namespace ShadowCamera {
/// <summary>
/// 描画カメラの前方を覆う平行光源の行列を作り、影の画素単位に位置を揃える
/// </summary>
/// <param name="view">更新済みの描画用ビュー行列</param>
/// <param name="direction">光が進むワールド方向。有限な非零ベクトル</param>
/// <param name="settings">検証済みの影設定</param>
/// <param name="matrix">成功時だけ置き換える行ベクトル用の光源ビュー投影行列</param>
/// <returns>true:成功 false:設定・カメラ・結果が無効</returns>
bool Build(const DirectX::XMFLOAT4X4 &view, const DirectX::XMFLOAT3 &direction,
           const ShadowSettings &settings, DirectX::XMFLOAT4X4 &matrix);
} // namespace ShadowCamera
