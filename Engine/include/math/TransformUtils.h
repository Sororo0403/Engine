#pragma once

#include "math/Transform.h"

namespace TransformUtils {
/// <summary>
/// 全成分が有限で、拡大率に0を含まない変形情報か確認する
/// </summary>
/// <param name="transform">確認する変形情報</param>
/// <returns>true:有効 false:無限大・非数・拡大率0を含む</returns>
bool IsValid(const Transform &transform);

/// <summary>
/// 拡大・回転・移動の順で適用するワールド行列を作成する
/// </summary>
/// <param name="transform">有効な変形情報。回転角の単位はラジアン</param>
/// <returns>ワールド行列</returns>
DirectX::XMMATRIX MakeWorldMatrix(const Transform &transform);
} // namespace TransformUtils
