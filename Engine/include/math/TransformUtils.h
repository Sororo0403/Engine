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

/// <summary>
/// 拡大・回転・移動で表現できるワールド行列を変形情報へ戻す
/// </summary>
/// <param name="matrix">分解するワールド行列</param>
/// <param name="transform">成功した場合に設定する変形情報。回転角はラジアン</param>
/// <returns>true:成功 false:無効な行列、または拡大・回転・移動で表現できない</returns>
bool TryFromWorldMatrix(const DirectX::XMFLOAT4X4 &matrix,
                        Transform &transform);

/// <summary>
/// 2つの変形情報の全成分が一致するか確認する
/// </summary>
/// <param name="left">比較する変形情報</param>
/// <param name="right">比較する変形情報</param>
/// <returns>true:一致 false:不一致</returns>
bool AreEqual(const Transform &left, const Transform &right);

} // namespace TransformUtils
