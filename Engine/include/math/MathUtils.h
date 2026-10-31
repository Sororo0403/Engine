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
} // namespace MathUtils
