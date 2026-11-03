#pragma once

#include <filesystem>
#include <string>

namespace ShellUtils {
/// <summary>
/// 起動するアプリへ指定する初期表示。実際の表示方法は起動先が決める
/// </summary>
enum class WindowVisibility { NORMAL, HIDDEN };

/// <summary>
/// 実在するフォルダをWindowsのシェルで開く
/// </summary>
/// <param name="path">絶対パス、または実行ファイル基準のフォルダ</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>true:開く要求に成功 false:パスやシェルの操作が無効</returns>
/// <remarks>呼び出し側でCOMを初期化する。フォルダや内容は作成・変更しない</remarks>
bool OpenDirectory(const std::filesystem::path &path, std::string &error);

/// <summary>
/// 実在する.exeを引数なしで独立して起動し、作業ディレクトリをその親フォルダにする
/// </summary>
/// <param name="path">絶対パス、または実行ファイル基準の実行ファイル</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <param name="visibility">起動先へ指定する初期表示</param>
/// <returns>true:起動要求に成功 false:パスや起動処理が無効</returns>
/// <remarks>呼び出し側でCOMを初期化する。起動先の初期化・終了は待たず、終了状態や生存期間は管理しない</remarks>
bool RunExecutable(const std::filesystem::path &path, std::string &error,
                   WindowVisibility visibility = WindowVisibility::NORMAL);
} // namespace ShellUtils
