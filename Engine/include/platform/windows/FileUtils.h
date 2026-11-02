#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace FileUtils {
/// <summary>
/// 新規に作成した一時ファイルへ書き込み、完了後にハンドルで保存先へ置き換える
/// </summary>
/// <param name="path">保存先。相対パスは実行ファイル基準。親フォルダは事前に作成する</param>
/// <param name="text">そのまま書き込むバイト列。改行や文字コードは変更しない</param>
/// <param name="error">失敗理由。後片付け失敗時は残った場所も含む。成功時は空</param>
/// <param name="overwrite">falseの場合は既存の保存先を置き換えない</param>
/// <returns>true:保存成功 false:作成・書き込み・置換失敗。既存の一時ファイルは変更しない</returns>
bool WriteTextAtomically(const std::filesystem::path &path,
                         std::string_view text, std::string &error,
                         bool overwrite = true);
} // namespace FileUtils
