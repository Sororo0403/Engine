#pragma once
#include "platform/windows/StringConversion.h"
#include <string_view>

namespace ObjectTag {
/// <summary>
/// 物体のタグとタグによる条件を検証する。空は未設定として扱う
/// </summary>
/// <param name="value">最大64バイトのUTF-8文字列</param>
/// <returns>true:有効 false:長すぎる、NULを含む、またはUTF-8ではない</returns>
inline bool IsValid(std::string_view value) {
	return value.empty() ||
	       (value.size() <= 64 && value.find('\0') == std::string_view::npos &&
	        !StringConversion::Utf8ToWide(value).empty());
}
} // namespace ObjectTag
