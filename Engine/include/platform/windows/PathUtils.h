#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

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
/// <returns>true:同じパスまたはファイル false:空のパス、または一致を確認できない。空以外の無効なパスは例外</returns>
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
/// 入力欄に貼り付けたパスを読み取る。パス全体を囲む引用符は除く
/// </summary>
std::filesystem::path FromPathInput(std::string_view text);

/// <summary>
/// 検索欄の前後の空白と引用符を除き、Windowsの区切りにそろえる。変換できなければ空を返す
/// </summary>
std::filesystem::path FromPathSearch(std::string_view text);

/// <summary>
/// パスに検索語が含まれるかを大文字小文字と区切りの違いを無視して調べる
/// </summary>
bool MatchesPathSearch(std::filesystem::path path,
                       const std::filesystem::path &searchPath);

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

/// <summary>
/// 対応する画像テクスチャの拡張子か、大文字・小文字を区別せず確認する
/// </summary>
/// <param name="path">確認するパス。ファイルの実在や画像データは確認しない</param>
/// <returns>true:PNG・JPEG・BMP・TIFF・GIF false:その他</returns>
bool IsImageFile(const std::filesystem::path &path);
bool IsFontFile(const std::filesystem::path &path);

/// <summary>
/// Windowsの通常ファイルとして使用できる単独の名前か確認する。予約名・区切り・制御文字を拒否する
/// </summary>
/// <param name="name">親フォルダを含まない名前。ファイルの実在は確認しない</param>
/// <returns>true:使用可能な名前 false:空・予約名・無効な文字や末尾・255文字超過</returns>
bool IsValidFileName(const std::filesystem::path &name);

/// <summary>
/// 正規化済みの対象が基準フォルダ内にあれば、表記の大小に依存しない相対パスを返す
/// </summary>
/// <param name="root">空でない正規化済みの基準フォルダ</param>
/// <param name="path">同じ形式で正規化した対象パス</param>
/// <returns>基準からの相対パス。外部・空・NULを含む参照では例外。同じ場所では空</returns>
std::filesystem::path RelativeInside(const std::filesystem::path &root,
                                     const std::filesystem::path &path);

/// <summary>
/// ファイルまたは親フォルダの移動をパスに反映する。ファイルの存在は確認しない
/// </summary>
/// <returns>最初に一致した移動先のパス。一致しない場合は元のパス</returns>
std::filesystem::path AfterMove(
    const std::filesystem::path &path,
    std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves);

/// <summary>
/// 対応するモデルファイルの拡張子か確認する。ファイルの実在や内容は確認しない
/// </summary>
/// <param name="path">確認するパス</param>
/// <returns>true:GLB・glTF・OBJ false:その他</returns>
bool IsModelFile(const std::filesystem::path &path);

} // namespace PathUtils
