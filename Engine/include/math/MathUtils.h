#pragma once

#include <DirectXMath.h>

namespace MathUtils {
/// <summary>
/// 2成分がすべて有限の値か確認する
/// </summary>
/// <param name="value">確認する値</param>
/// <returns>true:すべて有限 false:無限大または非数を含む</returns>
bool IsFinite(const DirectX::XMFLOAT2 &value);

/// <summary>
/// 3成分がすべて有限の値か確認する
/// </summary>
/// <param name="value">確認する値</param>
/// <returns>true:すべて有限 false:無限大または非数を含む</returns>
bool IsFinite(const DirectX::XMFLOAT3 &value);

/// <summary>
/// 4成分がすべて有限の値か確認する
/// </summary>
/// <param name="value">確認する値</param>
/// <returns>true:すべて有限 false:無限大または非数を含む</returns>
bool IsFinite(const DirectX::XMFLOAT4 &value);

/// <summary>
/// 各成分を、0を基準としたグリッド上の最近点へ丸める
/// </summary>
/// <param name="position">丸める座標</param>
/// <param name="step">各軸の正で有限の間隔</param>
/// <param name="snapped">成功した場合に設定する座標。positionと同じ変数でもよい</param>
/// <returns>true:成功 false:値が無効、または結果がfloatの範囲外</returns>
/// <remarks>中間点は0から遠い方へ丸める。失敗時は出力を変更しない</remarks>
bool SnapToGrid(const DirectX::XMFLOAT3 &position,
                const DirectX::XMFLOAT3 &step, DirectX::XMFLOAT3 &snapped);
} // namespace MathUtils
