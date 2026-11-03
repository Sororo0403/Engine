#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
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
/// <remarks>対応環境では削除共有された読み手の旧内容を保って置換する。置換を禁じるロックや読み取り専用の保存先は保持して失敗する</remarks>
bool WriteTextAtomically(const std::filesystem::path &path,
                         std::string_view text, std::string &error,
                         bool overwrite = true);
/// <summary>
/// バイト列を新規の一時ファイルへ書き込み、完了後に保存先へ確定する
/// </summary>
/// <param name="path">保存先。親フォルダは事前に作成する</param>
/// <param name="bytes">変更せず保存するバイト列</param>
/// <param name="error">失敗理由。後片付け失敗時は残った場所も含む</param>
/// <param name="overwrite">falseの場合は既存ファイルを変更しない</param>
/// <returns>true:成功 false:保存失敗。既存ファイルと他の一時ファイルは保持する</returns>
bool WriteBytesAtomically(const std::filesystem::path &path,
                          std::span<const std::byte> bytes, std::string &error,
                          bool overwrite = true);

} // namespace FileUtils
