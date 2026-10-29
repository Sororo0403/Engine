#pragma once

#include <filesystem>

namespace PathUtils {
/// <summary>
/// 実行ファイルの絶対パスを取得する
/// </summary>
/// <returns>実行ファイルのパス。取得失敗時は空のパス</returns>
std::filesystem::path GetExecutablePath();

/// <summary>
/// 実行ファイルが置かれているフォルダを取得する
/// </summary>
/// <returns>フォルダのパス。取得失敗時は空のパス</returns>
std::filesystem::path GetExecutableDirectory();
} // namespace PathUtils
