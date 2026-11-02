#pragma once

#include <filesystem>
#include <string>
#include <string_view>

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

/// <summary>
/// 実行ファイルのフォルダを基準にパスを解決する。ファイルの存在は確認しない
/// </summary>
/// <param name="path">絶対パス、またはドライブ名や先頭の区切り文字を持たない相対パス</param>
/// <returns>正規化した絶対パス。引数が無効、または取得失敗時は例外</returns>
std::filesystem::path ResolveFromExecutable(const std::filesystem::path &path);

/// <summary>
/// 正規化したパスを大文字・小文字を区別せず比較し、既存ファイルの別名も照合する
/// </summary>
/// <param name="first">比較するパス。相対パスは実行ファイルのフォルダ基準</param>
/// <param name="second">比較対象のパス</param>
/// <returns>true:同じパスまたはファイル false:一致を確認できない。無効なパスは例外</returns>
bool IsSameFile(const std::filesystem::path &first,
                const std::filesystem::path &second);

/// <summary>
/// ファイルパスをUTF-8の文字列に変換する
/// </summary>
/// <param name="path">変換するパス。空のパスも使用できる</param>
/// <returns>区切り文字にスラッシュを使うUTF-8文字列</returns>
std::string ToUtf8(const std::filesystem::path &path);

/// <summary>
/// UTF-8の文字列をファイルパスに変換する
/// </summary>
/// <param name="text">UTF-8の文字列。空文字列も使用できる</param>
/// <returns>変換したパス。UTF-8が無効な場合は例外</returns>
std::filesystem::path FromUtf8(std::string_view text);

/// <summary>
/// ローカルの相対URIを復号してファイルパスへ変換する。+は空白へ変換しない
/// </summary>
/// <param name="uri">UTF-8とパーセント表記を使う相対URI。親フォルダへの参照も使用できる</param>
/// <returns>復号した相対パス。無効な表記・UTF-8・NUL・絶対パス・スキームは例外</returns>
std::filesystem::path DecodeRelativeUri(std::string_view uri);

/// <summary>
/// ファイルの拡張子を、ASCIIの英字の大文字・小文字を区別せず比較する
/// </summary>
/// <param name="path">確認するパス</param>
/// <param name="extension">先頭のドットを含む拡張子。例：.json</param>
/// <returns>true:一致 false:不一致</returns>
bool HasExtension(const std::filesystem::path &path,
                  std::string_view extension);

} // namespace PathUtils
