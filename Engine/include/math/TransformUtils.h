#pragma once

#include "math/Transform.h"

namespace TransformUtils {
/// <summary>
/// 全成分が有限で、拡大率に0を含まず、回転が単位クォータニオンか確認する
/// </summary>
/// <param name="transform">確認する変形情報</param>
/// <returns>true:有効 false:無効な成分、拡大率0、または非単位回転を含む</returns>
bool IsValid(const Transform &transform);

/// <summary>
/// 回転クォータニオンを正規化する
/// </summary>
/// <param name="rotation">正規化する回転</param>
/// <param name="normalized">成功した場合に設定する単位クォータニオン</param>
/// <returns>true:成功 false:非数・無限大、または全成分が0</returns>
bool TryNormalizeRotation(const DirectX::XMFLOAT4 &rotation,
                          DirectX::XMFLOAT4 &normalized);

/// <summary>
/// オイラー角から単位クォータニオンを作成する
/// </summary>
/// <param name="radians">有限のX・Y・Z回転角。単位はラジアン</param>
/// <returns>Z・X・Yの順に適用する回転クォータニオン</returns>
DirectX::XMFLOAT4 RotationFromEuler(const DirectX::XMFLOAT3 &radians);

/// <summary>
/// 回転クォータニオンを表示用のオイラー角へ変換する
/// </summary>
/// <param name="rotation">単位クォータニオン</param>
/// <returns>X・Y・Z回転角。単位はラジアン</returns>
DirectX::XMFLOAT3 RotationToEuler(const DirectX::XMFLOAT4 &rotation);

/// <summary>
/// 拡大・回転・移動の順で適用するワールド行列を作成する
/// </summary>
/// <param name="transform">回転が単位クォータニオンの有効な変形情報</param>
/// <returns>ワールド行列</returns>
DirectX::XMMATRIX MakeWorldMatrix(const Transform &transform);

/// <summary>
/// 拡大・回転・移動で表現できるワールド行列を変形情報へ戻す
/// </summary>
/// <param name="matrix">分解するワールド行列</param>
/// <param name="transform">成功した場合に設定する変形情報。回転は単位クォータニオン</param>
/// <returns>true:成功 false:無効な行列、または拡大・回転・移動で表現できない</returns>
bool TryFromWorldMatrix(const DirectX::XMFLOAT4X4 &matrix,
                        Transform &transform);

/// <summary>
/// 移動・拡大率が一致し、回転が許容誤差内で等価か確認する
/// </summary>
/// <param name="left">比較する変形情報</param>
/// <param name="right">比較する変形情報</param>
/// <returns>true:一致 false:不一致。回転の符号反転は同じ向きとして扱う</returns>
bool AreEqual(const Transform &left, const Transform &right);

} // namespace TransformUtils
